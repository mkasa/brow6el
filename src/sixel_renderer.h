#pragma once

#include "image_renderer.h"
#include "include/internal/cef_types_wrappers.h"
#include <cstdint>
#include <mutex>
#include <sixel.h>
#include <vector>

class SixelRenderer : public ImageRenderer {
public:
  SixelRenderer(int width, int height, int cell_width, int cell_height);
  ~SixelRenderer();

  void render(const void *buffer, int width, int height, bool hasAlpha,
              const std::vector<CefRect> &dirtyRects = {}) override;
  void clear() override;
  void setTiledRenderingEnabled(bool enabled) override {
    tiled_rendering_enabled_ = enabled;
  }
  void forceFullRender() override { force_full_render_ = true; }
  void resetFrameCache() override {
    prev_buffer_.clear();
  } // Force next render to be full

  static std::mutex &getTerminalMutex();

private:
  void renderMonolithic(const unsigned char *buffer, int width, int height);
  void renderTile(const unsigned char *buffer, int buffer_width,
                  int buffer_height, int tile_x, int tile_y, int tile_width,
                  int tile_height);
  bool isTileDirty(const unsigned char *current_buffer, int buffer_width,
                   int tile_x, int tile_y, int tile_width, int tile_height);

  int width_;
  int height_;
  int cell_width_;
  int cell_height_;
  int tile_width_;
  int tile_height_;

  sixel_output_t *output_;
  sixel_dither_t *dither_;

  // Previous frame buffer for dirty detection
  std::vector<unsigned char> prev_buffer_;

  // Tiled rendering control
  bool tiled_rendering_enabled_;
  bool force_full_render_;
};
