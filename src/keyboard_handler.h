#pragma once

#include "include/cef_browser.h"
#include <termios.h>
#include <thread>
#include <atomic>

class KeyboardHandler {
public:
    KeyboardHandler(CefRefPtr<CefBrowser> browser);
    ~KeyboardHandler();
    
    void start();
    void stop();
    
private:
    void readLoop();
    void parseKeySequence(const char* seq, int len);
    void sendKeyEvent(int key_code, char character, bool is_char_event);
    
    CefRefPtr<CefBrowser> browser_;
    std::thread reader_thread_;
    std::atomic<bool> running_;
};
