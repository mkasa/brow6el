#pragma once

#include "include/cef_browser.h"
#include <termios.h>
#include <thread>
#include <atomic>

class BrowserClient;

class InputHandler {
public:
    enum InputMode {
        MODE_STANDARD,  // h/j/k/l for arrows, shortcuts without Ctrl
        MODE_MOUSE,     // hjkl for mouse movement, q/f for clicks
        MODE_INSERT     // All keys pass through to CEF
    };
    
    InputHandler(CefRefPtr<CefBrowser> browser, int term_width, int term_height,
                 int cell_width, int cell_height, int pixel_width, int pixel_height);
    ~InputHandler();
    
    void start();
    void stop();
    void setBrowserClient(BrowserClient* client) { browser_client_ = client; }
    InputMode getCurrentMode() const { return current_mode_; }
    const char* getModeName() const;
    
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
    
    // File input mode
    bool file_input_active_ = false;
    std::string file_input_buffer_;
    
    // Console input mode
    bool console_input_active_ = false;
    std::string console_input_buffer_;
    int console_scroll_offset_ = 0;
    
    // JS prompt input
    std::string js_prompt_input_;
    
    // Hint mode input
    bool hint_mode_active_ = false;
    std::string hint_input_buffer_;
    
    // Mouse emulation mode
    bool mouse_emu_mode_active_ = false;
    
    // Mouse drag tracking
    bool mouse_button_down_ = false;
    CefBrowserHost::MouseButtonType mouse_button_type_ = MBT_LEFT;
    
    // Modal control state
    InputMode current_mode_ = MODE_STANDARD;
};
