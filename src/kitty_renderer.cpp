#include "kitty_renderer.h"
#include <algorithm>
#include <cstdio>
#include <cstring>
#include <fcntl.h>
#include <iostream>
#include <mutex>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <termios.h>
#include <unistd.h>
#include <zlib.h>

// Global terminal mutex (shared with sixel)
extern std::mutex g_terminal_mutex;

// Global instance for dialog cropping
static KittyRenderer* g_kitty_renderer_instance = nullptr;

// Whether the terminal accepted a shared-memory probe at startup
static bool g_kitty_shm_enabled = false;

void KittyRenderer::setSharedMemoryEnabled(bool enabled) {
  g_kitty_shm_enabled = enabled;
}

static bool g_kitty_partial_enabled = true;

void KittyRenderer::setPartialUpdatesEnabled(bool enabled) {
  g_kitty_partial_enabled = enabled;
}

// Base frames sit below the patches; both stay below text (z < 0) and above
// cell backgrounds (z > -1073741824)
static const int kBaseZ = -1000;
// Past this many patches, the next update is a full frame that drops them all
static const size_t kMaxPatches = 64;

void KittyRenderer::setGlobalInstance(KittyRenderer* instance) {
  g_kitty_renderer_instance = instance;
}

KittyRenderer* KittyRenderer::getGlobalInstance() {
  return g_kitty_renderer_instance;
}

static std::string base64_encode(const unsigned char *data, size_t len) {
  static const char encoding_table[] =
      "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
  std::string result;
  result.reserve(((len + 2) / 3) * 4);

  for (size_t i = 0; i < len; i += 3) {
    uint32_t octet_a = i < len ? data[i] : 0;
    uint32_t octet_b = i + 1 < len ? data[i + 1] : 0;
    uint32_t octet_c = i + 2 < len ? data[i + 2] : 0;
    uint32_t triple = (octet_a << 16) + (octet_b << 8) + octet_c;

    result.push_back(encoding_table[(triple >> 18) & 0x3F]);
    result.push_back(encoding_table[(triple >> 12) & 0x3F]);
    result.push_back(i + 1 < len ? encoding_table[(triple >> 6) & 0x3F] : '=');
    result.push_back(i + 2 < len ? encoding_table[triple & 0x3F] : '=');
  }

  return result;
}

static std::vector<unsigned char> compress_zlib(const unsigned char *data,
                                                 size_t len, size_t &out_len) {
  uLongf compressed_len = compressBound(len);
  std::vector<unsigned char> compressed(compressed_len);

  // Use Z_BEST_SPEED (1) instead of Z_BEST_COMPRESSION (9) for better performance
  // Trade-off: slightly larger but MUCH faster compression
  int ret = compress2(compressed.data(), &compressed_len, data, len,
                      Z_BEST_SPEED);
  if (ret != Z_OK) {
    out_len = 0;
    return {};
  }

  out_len = compressed_len;
  compressed.resize(compressed_len);
  return compressed;
}

KittyRenderer::KittyRenderer(int width, int height, int cell_width,
                             int cell_height)
    : width_(width), height_(height), cell_width_(cell_width),
      cell_height_(cell_height), tiled_rendering_enabled_(false),
      next_image_id_(1), main_image_id_(0), alt_image_id_(0),
      use_alt_buffer_(false), last_rendered_height_(0) {
  // Set global instance for dialog cropping
  setGlobalInstance(this);
  
  // Hide terminal cursor
  printf("\033[?25l");
  fflush(stdout);
}

KittyRenderer::~KittyRenderer() {
  // Clear global instance
  if (g_kitty_renderer_instance == this) {
    setGlobalInstance(nullptr);
  }
  // Clean up - delete all images
  std::lock_guard<std::mutex> lock(g_terminal_mutex);
  printf("\033_Ga=d,d=a;\033\\");
  reapSharedMemory(true);
  
  // Show terminal cursor again
  printf("\033[?25h");
  fflush(stdout);
}

std::mutex &KittyRenderer::getTerminalMutex() { return g_terminal_mutex; }

void KittyRenderer::render(const void *buffer, int width, int height,
                           bool hasAlpha,
                           const std::vector<CefRect> &dirtyRects) {
  if (!buffer) {
    return;
  }

  std::lock_guard<std::mutex> lock(g_terminal_mutex);

  const unsigned char *src_buffer = static_cast<const unsigned char *>(buffer);
  size_t buffer_size = width * height * 4;

  // Update dimensions if changed
  width_ = width;
  height_ = height;
  
  bool force_this_render = force_render_next_;
  
  if (force_render_next_) {
    force_render_next_ = false;
  }

  // Decide whether to render
  bool should_render = false;
  
  if (force_this_render || prev_buffer_.empty()) {
    // Always render on force or first frame
    should_render = true;
  } else if (!dirtyRects.empty()) {
    // CEF provided specific dirty rects - trust them
    should_render = true;
  } else {
    // CEF provided no dirty rects - do our own dirty detection
    // Compare entire buffer to detect actual changes
    // Note: In practice CEF always provides dirtyRects, but keep as defensive fallback
    bool buffer_changed = memcmp(src_buffer, prev_buffer_.data(), buffer_size) != 0;
    if (buffer_changed) {
      should_render = true;
    }
  }
  
  if (should_render) {
    // Synchronized update: the terminal shows the frame's images all at once
    printf("\033[?2026h");
    if (force_this_render ||
        !renderPartial(src_buffer, width, height, dirtyRects)) {
      renderMonolithic(src_buffer, width, height);
    }
    printf("\033[?2026l");
    fflush(stdout);
    
    // Store buffer ONLY when we actually rendered
    // This ensures we compare against the last RENDERED frame, not last received
    if (prev_buffer_.size() != buffer_size) {
      prev_buffer_.resize(buffer_size);
    }
    memcpy(prev_buffer_.data(), src_buffer, buffer_size);
  }
}

void KittyRenderer::renderCropped(int exclude_bottom_rows) {
  if (prev_buffer_.empty()) {
    return; // Nothing to render
  }
  
  // NOTE: Do NOT lock here - caller already holds the lock!
  // std::lock_guard<std::mutex> lock(g_terminal_mutex);
  
  // If exclude_bottom_rows is 0, render full frame
  if (exclude_bottom_rows <= 0) {
    renderMonolithic(prev_buffer_.data(), width_, height_);
    return;
  }
  
  // Calculate cropped height in pixels
  struct winsize w;
  ioctl(STDOUT_FILENO, TIOCGWINSZ, &w);
  int total_rows = w.ws_row;
  int visible_rows = total_rows - exclude_bottom_rows;
  
  if (visible_rows <= 0) {
    return; // Can't crop that much
  }
  
  // Calculate pixel height for visible area
  int pixels_per_row = height_ / total_rows;
  int cropped_pixel_height = visible_rows * pixels_per_row;
  
  if (cropped_pixel_height <= 0 || cropped_pixel_height > height_) {
    return;
  }
  
  // DON'T delete images - let renderMonolithic use double buffering to avoid flicker
  // The double buffer will smoothly replace the old image with the cropped one
  
  // Render cropped version (only top portion)
  // NOTE: renderMonolithic does NOT acquire lock, so this is safe
  renderMonolithic(prev_buffer_.data(), width_, cropped_pixel_height);
}

void KittyRenderer::clear() {
  std::lock_guard<std::mutex> lock(g_terminal_mutex);
  printf("\033[2J\033[H");
  printf("\033_Ga=d,d=a;\033\\");
  fflush(stdout);
}

void KittyRenderer::renderMonolithic(const unsigned char *buffer, int width,
                                     int height) {
  // Double buffering: alternate between two image IDs
  // This keeps old image visible while new one loads (no flicker)
  
  if (main_image_id_ == 0) {
    // First render: allocate both buffer IDs and clear screen
    main_image_id_ = next_image_id_++;
    alt_image_id_ = next_image_id_++;
    printf("\033[2J\033[H");
    fflush(stdout);
    last_rendered_height_ = height;
  }
  
  // Clear screen if forced (e.g., page navigation) or height changed
  if (force_clear_on_next_render_ || height != last_rendered_height_) {
    FILE* log = fopen("/tmp/kitty_render.log", "a");
    if (log) {
      fprintf(log, "[KITTY] FORCE CLEAR - force_flag=%d, height=%d, last_height=%d\n", 
              force_clear_on_next_render_, height, last_rendered_height_);
      fclose(log);
    }
    printf("\033[2J\033[H"); // Clear screen to remove old text/status bar
    // Screen clear also removes images, so delete image IDs to force recreation
    printf("\033_Ga=d,d=I,i=%u;\033\\", main_image_id_);
    printf("\033_Ga=d,d=I,i=%u;\033\\", alt_image_id_);
    fflush(stdout);
    deletePatches();
    force_clear_on_next_render_ = false;
    last_rendered_height_ = height;
    use_alt_buffer_ = false; // Reset to main buffer
  }
  
  // Toggle between buffers
  use_alt_buffer_ = !use_alt_buffer_;
  uint32_t current_id = use_alt_buffer_ ? alt_image_id_ : main_image_id_;
  uint32_t previous_id = use_alt_buffer_ ? main_image_id_ : alt_image_id_;
  
  // Transmit to current buffer (old buffer stays visible during transmission)
  printf("\033[H");
  transmitImage(buffer, width, 0, 0, width, height, current_id, kBaseZ);

  // The new frame covers everything: drop the previous frame and all patches
  printf("\033_Ga=d,d=I,i=%u,q=2;\033\\", previous_id);
  deletePatches();

  // Move cursor to top-left
  printf("\033[H");
  fflush(stdout);
}

bool KittyRenderer::renderPartial(const unsigned char *buffer, int width,
                                  int height,
                                  const std::vector<CefRect> &dirtyRects) {
  // Patches need an up-to-date, full-height base frame underneath
  if (!g_kitty_partial_enabled || dirtyRects.empty() || main_image_id_ == 0 ||
      force_clear_on_next_render_ || height != last_rendered_height_ ||
      cell_width_ <= 0 || cell_height_ <= 0) {
    return false;
  }

  // Grow each dirty rect to whole cells so every patch is placed exactly on
  // the cell grid without scaling
  std::vector<Patch> rects;
  long long area = 0;
  for (const CefRect &r : dirtyRects) {
    int x0 = std::max(0, r.x) / cell_width_ * cell_width_;
    int y0 = std::max(0, r.y) / cell_height_ * cell_height_;
    int x1 = std::min(width, r.x + r.width);
    int y1 = std::min(height, r.y + r.height);
    x1 = std::min(width, (x1 + cell_width_ - 1) / cell_width_ * cell_width_);
    y1 = std::min(height, (y1 + cell_height_ - 1) / cell_height_ * cell_height_);
    if (x1 <= x0 || y1 <= y0) {
      continue;
    }
    rects.push_back({0, x0, y0, x1 - x0, y1 - y0});
    area += (long long)(x1 - x0) * (y1 - y0);
  }
  if (rects.empty()) {
    return true; // Nothing visible changed
  }

  // Large changes (scrolling, navigation) are cheaper as one full frame
  if (area * 2 > (long long)width * height ||
      patches_.size() + rects.size() > kMaxPatches) {
    return false;
  }

  for (Patch &p : rects) {
    p.id = next_patch_id_++;
    printf("\033[%d;%dH", p.y / cell_height_ + 1, p.x / cell_width_ + 1);
    transmitImage(buffer, width, p.x, p.y, p.w, p.h, p.id,
                  kBaseZ + ++next_patch_z_);

    // Older patches hidden entirely under this one are no longer needed
    auto covered = [&p](const Patch &old) {
      return old.x >= p.x && old.y >= p.y && old.x + old.w <= p.x + p.w &&
             old.y + old.h <= p.y + p.h;
    };
    for (const Patch &old : patches_) {
      if (covered(old)) {
        printf("\033_Ga=d,d=I,i=%u,q=2;\033\\", old.id);
      }
    }
    patches_.erase(std::remove_if(patches_.begin(), patches_.end(), covered),
                   patches_.end());
    patches_.push_back(p);
  }

  printf("\033[H");
  return true;
}

void KittyRenderer::deletePatches() {
  for (const Patch &p : patches_) {
    printf("\033_Ga=d,d=I,i=%u,q=2;\033\\", p.id);
  }
  patches_.clear();
  next_patch_z_ = 0;
}

void KittyRenderer::transmitImage(const unsigned char *buffer, int stride,
                                  int x, int y, int width, int height,
                                  uint32_t image_id, int z_index) {
  if (width <= 0 || height <= 0)
    return;

  // Calculate number of columns and rows
  int cols = (width + cell_width_ - 1) / cell_width_;
  int rows = (height + cell_height_ - 1) / cell_height_;

  if (g_kitty_shm_enabled &&
      transmitImageShm(buffer, stride, x, y, width, height, cols, rows,
                       image_id, z_index)) {
    return;
  }

  // Convert BGRA to RGB (skip alpha channel to reduce bandwidth by 25%)
  size_t rgb_size = width * height * 3;
  std::vector<unsigned char> rgb_buffer(rgb_size);
  
  unsigned char* dst = rgb_buffer.data();
  
  // Optimized BGRA->RGB conversion
  for (int row = 0; row < height; row++) {
    const unsigned char *src = buffer + ((size_t)(y + row) * stride + x) * 4;
    for (int i = 0; i < width; i++, dst += 3) {
      dst[0] = src[i * 4 + 2]; // R
      dst[1] = src[i * 4 + 1]; // G
      dst[2] = src[i * 4 + 0]; // B
      // Skip alpha channel (src[i * 4 + 3])
    }
  }

  // Skip compression - send uncompressed for better performance during typing
  // Base64 encode the raw RGB data
  std::string encoded = base64_encode(rgb_buffer.data(), rgb_size);

  // Transmit in larger chunks for better performance (reduce protocol overhead)
  const size_t chunk_size = 16384;  // 16KB chunks instead of 4KB
  size_t offset = 0;
  bool first_chunk = true;

  while (offset < encoded.length()) {
    size_t len = std::min(chunk_size, encoded.length() - offset);
    std::string chunk = encoded.substr(offset, len);
    offset += len;
    bool last_chunk = (offset >= encoded.length());

    if (first_chunk) {
      // First chunk - NO compression for speed
      // a=T: transmit and display (replaces existing image with same ID)
      // f=24: RGB format (no alpha channel = 25% less bandwidth)
      // s=width, v=height: image dimensions
      // c=cols, r=rows: display size in cells
      // i=image_id: image identifier (reused to replace atomically)
      // z<0: below text layer (dialogs appear on top)
      // C=1: leave the cursor where it is
      // m=1/0: more chunks follow
      // q=2: quiet mode (no responses)
      printf("\033_Ga=T,f=24,s=%d,v=%d,c=%d,r=%d,i=%u,z=%d,C=1,m=%d,q=2;%s\033\\",
             width, height, cols, rows, image_id, z_index, last_chunk ? 0 : 1,
             chunk.c_str());
      first_chunk = false;
    } else {
      // Subsequent chunks only have m flag and data
      printf("\033_Gm=%d;%s\033\\", last_chunk ? 0 : 1, chunk.c_str());
    }
  }
  
  // Ensure all image data is flushed to terminal
  fflush(stdout);
}

bool KittyRenderer::transmitImageShm(const unsigned char *buffer, int stride,
                                     int x, int y, int width, int height,
                                     int cols, int rows, uint32_t image_id,
                                     int z_index) {
  reapSharedMemory(false);

  // POSIX shm names are limited to 31 characters on macOS
  char name[32];
  snprintf(name, sizeof(name), "/b6el-%d-%u", (int)getpid(), shm_counter_++);

  size_t size = (size_t)width * height * 4;
  int fd = shm_open(name, O_CREAT | O_EXCL | O_RDWR, 0600);
  if (fd < 0) {
    return false;
  }
  if (ftruncate(fd, size) != 0) {
    close(fd);
    shm_unlink(name);
    return false;
  }
  void *map = mmap(nullptr, size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
  close(fd);
  if (map == MAP_FAILED) {
    shm_unlink(name);
    return false;
  }

  // CEF gives BGRA; Kitty's f=32 is RGBA. Force opaque alpha so transparent
  // page regions don't show the terminal background through.
  uint32_t *dst = static_cast<uint32_t *>(map);
  for (int row = 0; row < height; row++) {
    const uint32_t *src = reinterpret_cast<const uint32_t *>(buffer) +
                          (size_t)(y + row) * stride + x;
    for (int i = 0; i < width; i++) {
      uint32_t p = src[i]; // little-endian: 0xAARRGGBB
      *dst++ = 0xFF000000u | ((p & 0x000000FFu) << 16) | (p & 0x0000FF00u) |
               ((p & 0x00FF0000u) >> 16);
    }
  }
  munmap(map, size);

  std::string encoded =
      base64_encode(reinterpret_cast<const unsigned char *>(name), strlen(name));

  // t=s: terminal reads the pixels from shared memory, then unlinks it.
  // S= gives the exact size since macOS rounds shm objects up to a page.
  printf("\033_Ga=T,t=s,f=32,s=%d,v=%d,S=%zu,c=%d,r=%d,i=%u,z=%d,C=1,q=2;%s\033\\",
         width, height, size, cols, rows, image_id, z_index, encoded.c_str());

  pending_shm_.push_back({name, std::chrono::steady_clock::now()});
  return true;
}

void KittyRenderer::reapSharedMemory(bool all) {
  // Give the terminal a generous window to read each frame before unlinking
  // it ourselves; shm_unlink on an already-consumed name is a harmless ENOENT.
  const auto max_age = std::chrono::seconds(2);
  const size_t max_pending = 256;
  auto now = std::chrono::steady_clock::now();
  while (!pending_shm_.empty() &&
         (all || pending_shm_.size() > max_pending ||
          now - pending_shm_.front().created > max_age)) {
    shm_unlink(pending_shm_.front().name.c_str());
    pending_shm_.pop_front();
  }
}

// Static helper to draw dialog background overlay
void KittyRenderer::drawDialogOverlay(int start_row, int num_rows, uint32_t rgba_color) {
  // Get terminal dimensions
  struct winsize w;
  ioctl(STDOUT_FILENO, TIOCGWINSZ, &w);
  
  int term_width_px = w.ws_xpixel;
  int term_height_px = w.ws_ypixel;
  int cell_width = term_width_px / w.ws_col;
  int cell_height = term_height_px / w.ws_row;
  
  // Calculate overlay dimensions
  int overlay_width = term_width_px;
  int overlay_height = num_rows * cell_height;
  int overlay_y = (start_row - 1) * cell_height;
  
  // Create solid color buffer
  std::vector<unsigned char> buffer(overlay_width * overlay_height * 4);
  unsigned char r = (rgba_color >> 24) & 0xFF;
  unsigned char g = (rgba_color >> 16) & 0xFF;
  unsigned char b = (rgba_color >> 8) & 0xFF;
  unsigned char a = rgba_color & 0xFF;
  
  for (size_t i = 0; i < buffer.size(); i += 4) {
    buffer[i + 0] = r;
    buffer[i + 1] = g;
    buffer[i + 2] = b;
    buffer[i + 3] = a;
  }
  
  // Compress with zlib
  std::vector<unsigned char> compressed(compressBound(buffer.size()));
  uLongf compressed_size = compressed.size();
  compress2(compressed.data(), &compressed_size, buffer.data(), buffer.size(), Z_BEST_COMPRESSION);
  compressed.resize(compressed_size);
  
  // Base64 encode
  static const char base64_chars[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
  std::string encoded;
  encoded.reserve((compressed_size * 4 + 2) / 3);
  
  for (size_t i = 0; i < compressed_size; i += 3) {
    uint32_t n = compressed[i] << 16;
    if (i + 1 < compressed_size) n |= compressed[i + 1] << 8;
    if (i + 2 < compressed_size) n |= compressed[i + 2];
    
    encoded.push_back(base64_chars[(n >> 18) & 63]);
    encoded.push_back(base64_chars[(n >> 12) & 63]);
    encoded.push_back(i + 1 < compressed_size ? base64_chars[(n >> 6) & 63] : '=');
    encoded.push_back(i + 2 < compressed_size ? base64_chars[n & 63] : '=');
  }
  
  // Calculate display size in cells
  int cols = w.ws_col;
  int rows = num_rows;
  
  // Transmit in chunks (4096 byte limit per transmission)
  const size_t chunk_size = 4096;
  size_t offset = 0;
  bool first_chunk = true;
  static uint32_t overlay_id = 1000000; // Use high ID to avoid conflicts
  
  while (offset < encoded.size()) {
    size_t remaining = encoded.size() - offset;
    size_t current_chunk_size = std::min(chunk_size, remaining);
    std::string chunk = encoded.substr(offset, current_chunk_size);
    offset += current_chunk_size;
    bool last_chunk = (offset >= encoded.size());
    
    if (first_chunk) {
      // First chunk: include all metadata and z=2 for above webpage (NO compression)
      printf("\033_Ga=T,f=24,s=%d,v=%d,c=%d,r=%d,i=%u,z=2,m=%d,q=2;%s\033\\",
             overlay_width, overlay_height, cols, rows, overlay_id++, last_chunk ? 0 : 1, chunk.c_str());
      first_chunk = false;
    } else {
      printf("\033_Gm=%d;%s\033\\", last_chunk ? 0 : 1, chunk.c_str());
    }
  }
  
  fflush(stdout);
}
