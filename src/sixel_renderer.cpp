#include "sixel_renderer.h"
#include <algorithm>
#include <cstdio>
#include <cstring>
#include <iostream>
#include <mutex>

// Global terminal mutex (defined in image_renderer.cpp)
extern std::mutex g_terminal_mutex;

// C-style callback for sixel output
static int sixel_write_callback(char *data, int size, void *priv) {
  return fwrite(data, 1, size, stdout);
}

SixelRenderer::SixelRenderer(int width, int height, int cell_width,
                             int cell_height)
    : width_(width), height_(height), cell_width_(cell_width),
      cell_height_(cell_height), output_(nullptr), dither_(nullptr),
      tiled_rendering_enabled_(false), force_full_render_(false) {

  SIXELSTATUS status;

  status = sixel_output_new(&output_, sixel_write_callback, nullptr, nullptr);

  if (SIXEL_FAILED(status)) {
    std::cerr << "Failed to create sixel output: " << status << std::endl;
    throw std::runtime_error("Failed to create sixel output");
  }

  // Calculate tile dimensions: width and height can be different
  // tile_width must be multiple of cell_width
  // tile_height must be multiple of cell_height AND 6 (sixel band height)

  // Target tile size: aim for ~200-300 pixels per tile for good balance
  // This adapts to any screen resolution and cell size
  int target_tile_pixels = 250; // Sweet spot for most terminals

  // For width: round to nearest multiple of cell_width
  int tiles_h = std::max(1, width / target_tile_pixels);
  tile_width_ = width / tiles_h;
  tile_width_ = (tile_width_ / cell_width_) * cell_width_;
  if (tile_width_ < cell_width_) {
    tile_width_ = cell_width_;
  }

  // For height: must be multiple of BOTH cell_height AND 6
  int lcm_height = (cell_height_ * 6) / std::__gcd(cell_height_, 6);
  int tiles_v = std::max(1, height / target_tile_pixels);
  tile_height_ = height / tiles_v;
  tile_height_ = (tile_height_ / lcm_height) * lcm_height;
  if (tile_height_ < lcm_height) {
    tile_height_ = lcm_height;
  }

  // Don't allocate prev_buffer_ yet - leave it empty to detect first frame
  // It will be allocated on first render

  // Don't allocate prev_buffer_ yet - leave it empty to detect first frame
  // It will be allocated on first render
}

SixelRenderer::~SixelRenderer() {
  if (dither_) {
    sixel_dither_unref(dither_);
  }
  if (output_) {
    sixel_output_unref(output_);
  }
}

std::mutex &SixelRenderer::getTerminalMutex() { return g_terminal_mutex; }

void SixelRenderer::render(const void *buffer, int width, int height,
                           bool hasAlpha,
                           const std::vector<CefRect> &dirtyRects) {
  if (!buffer || !output_) {
    return;
  }

  std::lock_guard<std::mutex> lock(g_terminal_mutex);

  const unsigned char *src_buffer = static_cast<const unsigned char *>(buffer);

  // If tiled rendering is disabled, always use monolithic rendering
  if (!tiled_rendering_enabled_) {
    renderMonolithic(src_buffer, width, height);
    return;
  }

  // If forced full render, do monolithic and reset flag
  if (force_full_render_) {
    force_full_render_ = false;
    renderMonolithic(src_buffer, width, height);

    // Update prev_buffer
    if (prev_buffer_.empty()) {
      prev_buffer_.resize(width * height * 4);
    }
    memcpy(prev_buffer_.data(), src_buffer, width * height * 4);
    return;
  }

  // If no dirty rects provided, treat entire screen as dirty for tiled
  // rendering Only do monolithic on first frame (empty prev_buffer_)
  if (prev_buffer_.empty()) {
    renderMonolithic(src_buffer, width, height);

    // Allocate and update prev_buffer after first frame
    prev_buffer_.resize(width * height * 4);
    memcpy(prev_buffer_.data(), src_buffer, width * height * 4);
    return;
  }

  // If no dirty rects provided, treat entire screen as dirty
  std::vector<CefRect> rects_to_process;
  if (dirtyRects.empty()) {
    CefRect fullscreen;
    fullscreen.x = 0;
    fullscreen.y = 0;
    fullscreen.width = width;
    fullscreen.height = height;
    rects_to_process.push_back(fullscreen);
  } else {
    rects_to_process = dirtyRects;
  }

  // Tile-based rendering: process dirty rectangles
  for (const auto &rect : rects_to_process) {
    // Calculate affected tiles
    int tile_start_x = (rect.x / tile_width_) * tile_width_;
    int tile_start_y = (rect.y / tile_height_) * tile_height_;
    int tile_end_x =
        ((rect.x + rect.width + tile_width_ - 1) / tile_width_) * tile_width_;
    int tile_end_y =
        ((rect.y + rect.height + tile_height_ - 1) / tile_height_) *
        tile_height_;

    // If CEF provided specific dirty rects, trust them and render all affected
    // tiles Only use our own dirty detection when CEF gives us full screen (no
    // specific rects)
    bool use_dirty_detection = dirtyRects.empty();

    for (int ty = tile_start_y; ty < tile_end_y; ty += tile_height_) {
      for (int tx = tile_start_x; tx < tile_end_x; tx += tile_width_) {
        int tw = std::min(tile_width_, width_ - tx);
        int th = std::min(tile_height_, height_ - ty);

        if (tw <= 0 || th <= 0)
          continue;

        // Check if tile is dirty (only when no specific dirty rects from CEF)
        if (!use_dirty_detection ||
            isTileDirty(src_buffer, width, tx, ty, tw, th)) {
          renderTile(src_buffer, width, height, tx, ty, tw, th);
        }
      }
    }
  }

  // Update previous buffer
  memcpy(prev_buffer_.data(), src_buffer, width * height * 4);

  // Reposition cursor to top-left
  printf("\033[H");
  fflush(stdout);
}

void SixelRenderer::clear() {
  std::lock_guard<std::mutex> lock(g_terminal_mutex);
  printf("\033[2J\033[H");
  fflush(stdout);
}

void SixelRenderer::renderMonolithic(const unsigned char *buffer, int width,
                                     int height) {
  // Move cursor to top-left
  printf("\033[1;1H");
  fflush(stdout);

  // Create a copy of the buffer
  size_t buffer_size = width * height * 4;
  unsigned char *safe_buffer = new unsigned char[buffer_size];
  memcpy(safe_buffer, buffer, buffer_size);

  // Create a NEW dither for the frame
  sixel_dither_t *frame_dither = nullptr;
  SIXELSTATUS status = sixel_dither_new(&frame_dither, 256, nullptr);

  if (SIXEL_FAILED(status) || !frame_dither) {
    delete[] safe_buffer;
    return;
  }

  // Initialize the dither with the actual image data (CEF uses BGRA format)
  status = sixel_dither_initialize(frame_dither, safe_buffer, width, height,
                                   SIXEL_PIXELFORMAT_BGRA8888, SIXEL_LARGE_AUTO,
                                   SIXEL_REP_AUTO, SIXEL_QUALITY_HIGH);

  if (SIXEL_FAILED(status)) {
    sixel_dither_unref(frame_dither);
    delete[] safe_buffer;
    return;
  }

  // Encode
  status = sixel_encode(safe_buffer, width, height, 4, frame_dither, output_);

  sixel_dither_unref(frame_dither);
  delete[] safe_buffer;

  // Reposition cursor to top-left
  printf("\033[H");
  fflush(stdout);
}

bool SixelRenderer::isTileDirty(const unsigned char *current_buffer,
                                int buffer_width, int tile_x, int tile_y,
                                int tile_width, int tile_height) {
  // Align tile position to cell boundaries for consistent comparison
  int aligned_tile_x = (tile_x / cell_width_) * cell_width_;
  int aligned_tile_y = (tile_y / cell_height_) * cell_height_;

  // Compare current tile with previous frame
  for (int y = 0; y < tile_height; y++) {
    int row = aligned_tile_y + y;
    if (row >= height_)
      break;

    int offset = (row * buffer_width + aligned_tile_x) * 4;
    int tile_row_size = tile_width * 4;

    if (memcmp(current_buffer + offset, prev_buffer_.data() + offset,
               tile_row_size) != 0) {
      return true;
    }
  }
  return false;
}

void SixelRenderer::renderTile(const unsigned char *buffer, int buffer_width,
                               int buffer_height, int tile_x, int tile_y,
                               int tile_width, int tile_height) {
  // CRITICAL: Ensure tile position is aligned to cell boundaries
  // Round down to nearest cell boundary to avoid positioning artifacts
  int aligned_tile_x = (tile_x / cell_width_) * cell_width_;
  int aligned_tile_y = (tile_y / cell_height_) * cell_height_;

  // Position cursor to tile location (convert pixels to terminal coordinates)
  int term_row = 1 + (aligned_tile_y / cell_height_);
  int term_col = 1 + (aligned_tile_x / cell_width_);

  printf("\033[%d;%dH", term_row, term_col);
  fflush(stdout);

  // Extract tile data from buffer - use ALIGNED position so data matches cursor
  // position
  size_t tile_buffer_size = tile_width * tile_height * 4;
  unsigned char *tile_buffer = new unsigned char[tile_buffer_size];

  for (int y = 0; y < tile_height; y++) {
    int src_row = aligned_tile_y + y; // Use aligned position
    if (src_row >= buffer_height)
      break;

    int src_offset =
        (src_row * buffer_width + aligned_tile_x) * 4; // Use aligned position
    int dst_offset = y * tile_width * 4;
    memcpy(tile_buffer + dst_offset, buffer + src_offset, tile_width * 4);
  }

  // Create dither for this tile
  sixel_dither_t *tile_dither = nullptr;
  SIXELSTATUS status = sixel_dither_new(&tile_dither, 256, nullptr);

  if (SIXEL_FAILED(status) || !tile_dither) {
    delete[] tile_buffer;
    return;
  }

  // Initialize with tile data (CEF uses BGRA format)
  status =
      sixel_dither_initialize(tile_dither, tile_buffer, tile_width, tile_height,
                              SIXEL_PIXELFORMAT_BGRA8888, SIXEL_LARGE_AUTO,
                              SIXEL_REP_AUTO, SIXEL_QUALITY_HIGH);

  if (SIXEL_FAILED(status)) {
    sixel_dither_unref(tile_dither);
    delete[] tile_buffer;
    return;
  }

  // Encode and output tile
  status = sixel_encode(tile_buffer, tile_width, tile_height, 4, tile_dither,
                        output_);

  sixel_dither_unref(tile_dither);
  delete[] tile_buffer;

  fflush(stdout);
}
