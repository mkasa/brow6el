#pragma once

#include "include/cef_browser.h"
#include <atomic>
#include <termios.h>
#include <thread>

class BrowserClient;

class InputHandler {
public:
  enum InputMode {
    MODE_STANDARD, // h/j/k/l for arrows, shortcuts without Ctrl
    MODE_MOUSE,    // hjkl for mouse movement, q/f for clicks
    MODE_INSERT,   // All keys pass through to CEF
    MODE_VISUAL    // Text selection mode
  };

  InputHandler(CefRefPtr<CefBrowser> browser, int term_width, int term_height,
               int cell_width, int cell_height, int pixel_width,
               int pixel_height);
  ~InputHandler();

  void start();
  void stop();
  void setBrowserClient(BrowserClient *client) { browser_client_ = client; }
  void setTiledRenderingEnabled(bool enabled) {
    tiled_rendering_enabled_ = enabled;
  }
  void setZoomLevel(double level) { current_zoom_level_ = level; }
  double getZoomLevel() const { return current_zoom_level_; }
  InputMode getCurrentMode() const { return current_mode_; }
  const char *getModeName() const;
  bool IsSearchActive() const { return search_input_active_; }
  std::string GetSearchQuery() const { return search_input_buffer_; }
  void updateDimensions(int cols, int rows, int cell_w, int cell_h, int pixel_w,
                        int pixel_h);

private:
  void enableMouseTracking();
  void disableMouseTracking();
  void readLoop();
  void parseMouseEvent(const char *seq, int len);
  void parseKeySequence(const char *seq, int len);
  void sendKeyEvent(int key_code, char character, bool is_char_event,
                    bool shift_pressed = false);
  int readUTF8Char(unsigned char first_byte, std::string &utf8_char);
  void sendUTF8CharEvent(const std::string &utf8_char);
  void removeLastUTF8Char(std::string &str);

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

  BrowserClient *browser_client_;

  // URL input mode
  bool url_input_active_ = false;
  std::string url_input_buffer_;

  // File input mode
  bool file_input_active_ = false;
  std::string file_input_buffer_;

  // Search input mode
  bool search_input_active_ = false;
  std::string search_input_buffer_;
  bool search_started_ = false; // true after Enter pressed

  // Auth dialog mode
  bool auth_dialog_active_ = false;
  std::string auth_username_buffer_;
  std::string auth_password_buffer_;
  bool auth_password_mode_ = false; // true when entering password

  // Console input mode
  bool console_input_active_ = false;
  std::string console_input_buffer_;
  int console_scroll_offset_ = 0;
  
  // Calculate scroll amount (2/3 of window height)
  int getScrollAmount() const {
    // Use terminal rows (not pixels) for more accurate calculation
    // Subtract 1 for status bar to get actual viewport height
    int viewport_rows = term_height_ - 1;
    int scroll_rows = (viewport_rows * 2) / 3;
    // 30 units per line gives good 2/3 viewport scrolling
    return scroll_rows * 30;
  }

  // JS prompt input
  std::string js_prompt_input_;

  // Hint mode input
  bool hint_mode_active_ = false;
  std::string hint_input_buffer_;

  // Mouse emulation mode
  bool mouse_emu_mode_active_ = false;

  // Visual selection mode
  bool visual_mode_active_ = false;

  // Mouse drag tracking
  bool mouse_button_down_ = false;
  CefBrowserHost::MouseButtonType mouse_button_type_ = MBT_LEFT;
  bool physical_mouse_dragging_ = false;
  int drag_start_x_ = 0;
  int drag_start_y_ = 0;

  // Modal control state
  InputMode current_mode_ = MODE_STANDARD;

  // Tiled rendering state (toggled with 'z' key)
  bool tiled_rendering_enabled_ = false;
  
  // Zoom level tracking
  double current_zoom_level_ = 0.0;
};
