#pragma once

#include <vector>
#include <cstdint>
#include <mutex>
#include <sixel.h>

class SixelRenderer {
public:
    SixelRenderer(int width, int height);
    ~SixelRenderer();
    
    void render(const void* buffer, int width, int height, bool hasAlpha);
    void clear();
    
    static std::mutex& getTerminalMutex();
    
private:
    int width_;
    int height_;
    sixel_output_t* output_;
};
