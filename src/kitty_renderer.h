#pragma once

#include "image_renderer.h"
#include "include/internal/cef_types_wrappers.h"
#include <chrono>
#include <cstdint>
#include <deque>
#include <mutex>
#include <string>
#include <vector>

class KittyRenderer : public ImageRenderer {
public:
  KittyRenderer(int width, int height, int cell_width, int cell_height);
  ~KittyRenderer();

  void render(const void *buffer, int width, int height, bool hasAlpha,
              const std::vector<CefRect> &dirtyRects = {}) override;
  void clear() override;
  void setTiledRenderingEnabled(bool enabled) override {
    // Tiled rendering not supported for Kitty protocol - always monolithic
    tiled_rendering_enabled_ = false;
  }
  void forceFullRender() override { 
    FILE* log = fopen("/tmp/kitty_render.log", "a");
    if (log) {
      fprintf(log, "[KITTY::forceFullRender] Setting force_clear=true\n");
      fclose(log);
    }
    // Clear terminal text to remove old status bar lines
    force_clear_on_next_render_ = true;
    // Also force render even if buffer unchanged
    force_render_next_ = true;
  }
  void resetFrameCache() override {
    prev_buffer_.clear();
  }

  // Kitty-specific method to render cropped version (for dialogs)
  void renderCropped(int exclude_bottom_rows);

  // Get previous buffer for re-rendering
  const std::vector<unsigned char>& getPrevBuffer() const { return prev_buffer_; }

  // Static method to get renderer instance for cropping
  static void setGlobalInstance(KittyRenderer* instance);
  static KittyRenderer* getGlobalInstance();

  // Static helper to draw dialog background overlay (doesn't need renderer instance)
  static void drawDialogOverlay(int start_row, int num_rows, uint32_t rgba_color);

  static std::mutex &getTerminalMutex();

  // Enable transmission through POSIX shared memory (t=s). Set once at startup
  // after TerminalDetector::checkKittyShmSupport() confirms the terminal can
  // read it; otherwise frames are sent inline as base64.
  static void setSharedMemoryEnabled(bool enabled);

  // Send only the regions CEF reports as dirty, as small patches stacked on
  // the last full frame. On by default; BROW6EL_KITTY_PARTIAL=0 disables it.
  static void setPartialUpdatesEnabled(bool enabled);

private:
  void renderMonolithic(const unsigned char *buffer, int width, int height);
  bool renderPartial(const unsigned char *buffer, int width, int height,
                     const std::vector<CefRect> &dirtyRects);
  void deletePatches();
  // Transmit the w x h region at (x, y) of a BGRA buffer that is stride pixels
  // wide, placed at the cursor with the given z-index
  void transmitImage(const unsigned char *buffer, int stride, int x, int y,
                     int w, int h, uint32_t image_id, int z_index);
  bool transmitImageShm(const unsigned char *buffer, int stride, int x, int y,
                        int w, int h, int cols, int rows, uint32_t image_id,
                        int z_index);
  void reapSharedMemory(bool all);

  // Partial updates: small images stacked above the base frame, newest on top
  struct Patch {
    uint32_t id;
    int x, y, w, h;
  };
  std::vector<Patch> patches_;
  uint32_t next_patch_id_ = 0x40000000;
  int next_patch_z_ = 0;

  // Shared-memory objects handed to the terminal. The terminal unlinks each
  // one after reading it; we unlink stragglers so a terminal that never reads
  // them can't leak frames into kernel memory.
  struct PendingShm {
    std::string name;
    std::chrono::steady_clock::time_point created;
  };
  std::deque<PendingShm> pending_shm_;
  uint32_t shm_counter_ = 0;
  
  int width_;
  int height_;
  int cell_width_;
  int cell_height_;

  // Previous frame buffer (kept for consistency with interface)
  std::vector<unsigned char> prev_buffer_;

  // Tiled rendering not supported for Kitty
  bool tiled_rendering_enabled_;
  
  // Flag to clear screen on next render (for page navigation)
  bool force_clear_on_next_render_ = false;
  
  // Flag to force render even if buffer unchanged
  bool force_render_next_ = false;
  
  // Image ID management
  uint32_t next_image_id_;
  uint32_t main_image_id_;
  uint32_t alt_image_id_;  // Second buffer for double buffering
  bool use_alt_buffer_;    // Toggle between buffers
  int last_rendered_height_; // Track height to detect crop changes
};
