#pragma once

#include "include/cef_browser.h"
#include <termios.h>
#include <thread>
#include <atomic>

class BrowserClient;

class InputHandler {
public:
    InputHandler(CefRefPtr<CefBrowser> browser, int term_width, int term_height,
                 int cell_width, int cell_height, int pixel_width, int pixel_height);
    ~InputHandler();
    
    void start();
    void stop();
    void setBrowserClient(BrowserClient* client) { browser_client_ = client; }
    
private:
    void enableMouseTracking();
    void disableMouseTracking();
    void readLoop();
    void parseMouseEvent(const char* seq, int len);
    void parseKeySequence(const char* seq, int len);
    void sendKeyEvent(int key_code, char character, bool is_char_event, bool shift_pressed = false);
    
    CefRefPtr<CefBrowser> browser_;
    int term_width_;
    int term_height_;
    int cell_width_;
    int cell_height_;
    int pixel_width_;
    int pixel_height_;
    
    std::thread reader_thread_;
    std::atomic<bool> running_;
    
    struct termios old_tio_;
    
    BrowserClient* browser_client_;
    
    // URL input mode
    bool url_input_active_ = false;
    std::string url_input_buffer_;
    
    // Console input mode
    bool console_input_active_ = false;
    std::string console_input_buffer_;
    int console_scroll_offset_ = 0;
    
    // JS prompt input
    std::string js_prompt_input_;
};
