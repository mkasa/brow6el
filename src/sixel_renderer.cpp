#include "sixel_renderer.h"
#include <cstdio>
#include <cstring>
#include <iostream>
#include <mutex>

// Global terminal mutex
static std::mutex g_terminal_mutex;

// C-style callback for sixel output
static int sixel_write_callback(char* data, int size, void* priv) {
    return fwrite(data, 1, size, stdout);
}

SixelRenderer::SixelRenderer(int width, int height) 
    : width_(width), height_(height), output_(nullptr) {
    
    SIXELSTATUS status;
    
    status = sixel_output_new(&output_, sixel_write_callback, nullptr, nullptr);
    
    if (SIXEL_FAILED(status)) {
        std::cerr << "Failed to create sixel output: " << status << std::endl;
        throw std::runtime_error("Failed to create sixel output");
    }
}

SixelRenderer::~SixelRenderer() {
    if (output_) {
        sixel_output_unref(output_);
    }
}

std::mutex& SixelRenderer::getTerminalMutex() {
    return g_terminal_mutex;
}

void SixelRenderer::render(const void* buffer, int width, int height, bool hasAlpha) {
    if (!buffer || !output_) {
        return;
    }
    
    std::lock_guard<std::mutex> lock(g_terminal_mutex);
    
    // Move cursor to top-left (row 1, col 1)
    // This preserves any content below the sixel area (like status bar)
    printf("\033[1;1H");
    fflush(stdout);
    
    // Make a copy of the buffer
    size_t buffer_size = width * height * 4;
    unsigned char* safe_buffer = new unsigned char[buffer_size];
    memcpy(safe_buffer, buffer, buffer_size);
    
    // Create a NEW dither for each frame
    sixel_dither_t* frame_dither = nullptr;
    SIXELSTATUS status = sixel_dither_new(&frame_dither, 256, nullptr);
    
    if (SIXEL_FAILED(status) || !frame_dither) {
        delete[] safe_buffer;
        return;
    }
    
    // Initialize the dither with the actual image data
    // CEF uses BGRA format, not RGBA!
    status = sixel_dither_initialize(frame_dither, safe_buffer, width, height,
                                     SIXEL_PIXELFORMAT_BGRA8888,
                                     SIXEL_LARGE_AUTO, SIXEL_REP_AUTO,
                                     SIXEL_QUALITY_HIGH);
    
    if (SIXEL_FAILED(status)) {
        sixel_dither_unref(frame_dither);
        delete[] safe_buffer;
        return;
    }
    
    // Encode
    status = sixel_encode(safe_buffer, width, height, 4, frame_dither, output_);
    
    sixel_dither_unref(frame_dither);
    delete[] safe_buffer;
    
    // Reposition cursor to top-left after sixel output
    // This prevents scrolling in terminals where cursor advances after sixel
    printf("\033[H");
    
    fflush(stdout);
}

void SixelRenderer::clear() {
    std::lock_guard<std::mutex> lock(g_terminal_mutex);
    printf("\033[2J\033[H");
    fflush(stdout);
}
