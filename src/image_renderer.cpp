#include "image_renderer.h"
#include <mutex>

// Global terminal mutex shared by all renderers
std::mutex g_terminal_mutex;

std::mutex &ImageRenderer::getTerminalMutex() { return g_terminal_mutex; }
