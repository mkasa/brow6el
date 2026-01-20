#pragma once

#include "include/internal/cef_types_wrappers.h"
#include <mutex>
#include <vector>

// Base interface for image renderers
class ImageRenderer {
public:
  virtual ~ImageRenderer() = default;

  virtual void render(const void *buffer, int width, int height, bool hasAlpha,
                      const std::vector<CefRect> &dirtyRects = {}) = 0;
  virtual void clear() = 0;
  virtual void setTiledRenderingEnabled(bool enabled) = 0;
  virtual void forceFullRender() = 0;
  virtual void resetFrameCache() = 0;

  static std::mutex &getTerminalMutex();
};
