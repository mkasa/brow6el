#include "input_handler.h"
#include "browser_client.h"
#include "include/internal/cef_types.h"
#include "profile_config.h"
#include "status_bar.h"
#include <cstring>
#include <iostream>
#include <unistd.h>

// External function to request shutdown
extern void requestShutdown();

// Map common keys to their codes
#define VKEY_BACK 0x08
#define VKEY_TAB 0x09
#define VKEY_RETURN 0x0D
#define VKEY_ESCAPE 0x1B
#define VKEY_SPACE 0x20
#define VKEY_END 0x23
#define VKEY_HOME 0x24
#define VKEY_LEFT 0x25
#define VKEY_UP 0x26
#define VKEY_RIGHT 0x27
#define VKEY_DOWN 0x28
#define VKEY_DELETE 0x2E

InputHandler::InputHandler(CefRefPtr<CefBrowser> browser, int term_width,
                           int term_height, int cell_width, int cell_height,
                           int pixel_width, int pixel_height)
    : browser_(browser), term_width_(term_width), term_height_(term_height),
      cell_width_(cell_width), cell_height_(cell_height),
      pixel_width_(pixel_width), pixel_height_(pixel_height), running_(false),
      browser_client_(nullptr) {}

InputHandler::~InputHandler() { stop(); }

const char *InputHandler::getModeName() const {
  // Create static buffer for mode string with rendering indicator
  static char mode_buffer[16];

  const char *mode_letter;
  switch (current_mode_) {
  case MODE_STANDARD:
    mode_letter = "S";
    break;
  case MODE_MOUSE:
    mode_letter = "M";
    break;
  case MODE_INSERT:
    mode_letter = "I";
    break;
  case MODE_VISUAL:
    mode_letter = "V";
    break;
  default:
    mode_letter = "?";
    break;
  }

  // Add rendering mode indicator only for sixel (Kitty doesn't support tiled)
  bool show_render_mode = true;
  if (browser_client_ && browser_client_->IsKittyRenderer()) {
    show_render_mode = false; // Kitty is always monolithic
  }

  if (show_render_mode) {
    const char *render_mode = tiled_rendering_enabled_ ? "T" : "M";
    snprintf(mode_buffer, sizeof(mode_buffer), "%s][%s", mode_letter,
             render_mode);
  } else {
    snprintf(mode_buffer, sizeof(mode_buffer), "%s", mode_letter);
  }

  return mode_buffer;
}

void InputHandler::updateDimensions(int cols, int rows, int cell_w, int cell_h,
                                    int pixel_w, int pixel_h) {
  term_width_ = cols;
  term_height_ = rows;
  cell_width_ = cell_w;
  cell_height_ = cell_h;
  pixel_width_ = pixel_w;
  pixel_height_ = pixel_h;

  FILE *log = fopen("/tmp/brow6el_debug.log", "a");
  if (log) {
    fprintf(log,
            "InputHandler dimensions updated: %dx%d cells, cell size: %dx%d, "
            "pixels: %dx%d\n",
            term_width_, term_height_, cell_width_, cell_height_, pixel_width_,
            pixel_height_);
    fclose(log);
  }
}

void InputHandler::start() {
  if (running_)
    return;

  // Log startup
  FILE *log = fopen("/tmp/brow6el_debug.log", "w");
  if (log) {
    fprintf(log, "InputHandler starting...\n");
    fprintf(log, "Terminal: %dx%d cells, cell size: %dx%d, pixels: %dx%d\n",
            term_width_, term_height_, cell_width_, cell_height_, pixel_width_,
            pixel_height_);
    fclose(log);
  }

  enableMouseTracking();
  running_ = true;
  reader_thread_ = std::thread(&InputHandler::readLoop, this);
}

void InputHandler::stop() {
  if (!running_)
    return;

  running_ = false;
  if (reader_thread_.joinable()) {
    reader_thread_.join();
  }
  disableMouseTracking();
}

void InputHandler::enableMouseTracking() {
  // Save current terminal settings
  tcgetattr(STDIN_FILENO, &old_tio_);

  // Set terminal to raw mode to capture mouse events and keyboard
  struct termios new_tio = old_tio_;
  new_tio.c_lflag &= ~(ICANON | ECHO); // Disable canonical mode and echo
  new_tio.c_cc[VMIN] = 0;              // Non-blocking read
  new_tio.c_cc[VTIME] = 0;
  tcsetattr(STDIN_FILENO, TCSANOW, &new_tio);

  // Enable SGR mouse tracking
  printf("\033[?1003h"); // Enable mouse motion tracking
  printf("\033[?1006h"); // Enable SGR extended mouse mode
  fflush(stdout);
}

void InputHandler::disableMouseTracking() {
  // Disable mouse tracking
  printf("\033[?1003l");
  printf("\033[?1006l");
  fflush(stdout);

  // Restore terminal settings
  tcsetattr(STDIN_FILENO, TCSANOW, &old_tio_);
}

int InputHandler::readUTF8Char(unsigned char first_byte,
                               std::string &utf8_char) {
  utf8_char.clear();
  utf8_char += (char)first_byte;

  int bytes_needed = 0;

  // Determine UTF-8 sequence length from first byte
  if ((first_byte & 0x80) == 0) {
    // Single byte ASCII (0xxxxxxx)
    return 1;
  } else if ((first_byte & 0xE0) == 0xC0) {
    // 2-byte sequence (110xxxxx)
    bytes_needed = 1;
  } else if ((first_byte & 0xF0) == 0xE0) {
    // 3-byte sequence (1110xxxx)
    bytes_needed = 2;
  } else if ((first_byte & 0xF8) == 0xF0) {
    // 4-byte sequence (11110xxx)
    bytes_needed = 3;
  } else {
    // Invalid UTF-8 start byte
    return -1;
  }

  // Read continuation bytes
  for (int i = 0; i < bytes_needed; i++) {
    unsigned char continuation_byte;
    ssize_t n = read(STDIN_FILENO, &continuation_byte, 1);

    if (n <= 0) {
      // Failed to read continuation byte
      return -1;
    }

    // Validate continuation byte (must be 10xxxxxx)
    if ((continuation_byte & 0xC0) != 0x80) {
      return -1;
    }

    utf8_char += (char)continuation_byte;
  }

  return 1 + bytes_needed;
}

void InputHandler::sendUTF8CharEvent(const std::string &utf8_char) {
  if (!browser_ || !browser_->GetHost() || utf8_char.empty()) {
    return;
  }

  // Convert UTF-8 to UTF-16 for CEF
  // Simple conversion for common cases (up to 3 bytes)
  unsigned char first = (unsigned char)utf8_char[0];
  char16_t utf16_char = 0;

  if (utf8_char.size() == 1) {
    // ASCII
    utf16_char = first;
  } else if (utf8_char.size() == 2) {
    // 2-byte UTF-8
    unsigned char second = (unsigned char)utf8_char[1];
    utf16_char = ((first & 0x1F) << 6) | (second & 0x3F);
  } else if (utf8_char.size() == 3) {
    // 3-byte UTF-8
    unsigned char second = (unsigned char)utf8_char[1];
    unsigned char third = (unsigned char)utf8_char[2];
    utf16_char =
        ((first & 0x0F) << 12) | ((second & 0x3F) << 6) | (third & 0x3F);
  } else if (utf8_char.size() == 4) {
    // 4-byte UTF-8 (surrogates needed for characters beyond U+FFFF)
    // For now, just log and skip - rare case
    FILE *log = fopen("/tmp/brow6el_debug.log", "a");
    if (log) {
      fprintf(log, "4-byte UTF-8 character not yet supported\n");
      fclose(log);
    }
    return;
  }

  CefKeyEvent key_event;
  key_event.modifiers = 0;
  key_event.is_system_key = 0;
  key_event.focus_on_editable_field = 1;
  key_event.windows_key_code = utf16_char;
  key_event.native_key_code = utf16_char;
  key_event.character = utf16_char;
  key_event.unmodified_character = utf16_char;
  key_event.type = KEYEVENT_CHAR;

  browser_->GetHost()->SendKeyEvent(key_event);
}

void InputHandler::removeLastUTF8Char(std::string &str) {
  if (str.empty())
    return;

  // Start from the end and work backwards
  size_t pos = str.length() - 1;

  // If the last byte is ASCII (< 0x80), just remove it
  if ((unsigned char)str[pos] < 0x80) {
    str.pop_back();
    return;
  }

  // Otherwise, we need to find the start of the UTF-8 sequence
  // Walk backwards while we see continuation bytes (10xxxxxx)
  while (pos > 0 && ((unsigned char)str[pos] & 0xC0) == 0x80) {
    pos--;
  }

  // Now pos points to the start byte of the UTF-8 character
  // Remove from this position to the end
  str.erase(pos);
}

void InputHandler::readLoop() {
  char buf[64];
  int pos = 0;
  int read_count = 0;
  int mouse_event_count = 0;
  int key_event_count = 0;

  FILE *log = fopen("/tmp/brow6el_debug.log", "a");
  if (log) {
    fprintf(log, "Read loop started\n");
    fclose(log);
  }

  while (running_) {
    // Sync file input state with browser client
    if (browser_client_ && browser_client_->IsFileInputActive() &&
        !file_input_active_) {
      file_input_active_ = true;
      file_input_buffer_.clear();
    }

    // Sync auth dialog state with browser client
    if (browser_client_ && browser_client_->IsAuthDialogActive() &&
        !auth_dialog_active_) {
      auth_dialog_active_ = true;
      auth_username_buffer_.clear();
      auth_password_buffer_.clear();
      auth_password_mode_ = false;
    }

    // Sync hint mode state bidirectionally
    if (browser_client_) {
      if (browser_client_->IsHintModeActive() && !hint_mode_active_) {
        hint_mode_active_ = true;
      } else if (!browser_client_->IsHintModeActive() && hint_mode_active_) {
        hint_mode_active_ = false;
        hint_input_buffer_.clear();
      }
    }

    // Sync mouse emu mode state bidirectionally
    if (browser_client_) {
      if (browser_client_->IsMouseEmuModeActive() && !mouse_emu_mode_active_) {
        mouse_emu_mode_active_ = true;
      } else if (!browser_client_->IsMouseEmuModeActive() &&
                 mouse_emu_mode_active_) {
        mouse_emu_mode_active_ = false;
      }

      // Check for mode switch request from mouse click detection
      if (browser_client_->GetModeSwitchRequest()) {
        if (browser_client_->GetSwitchToInsertMode()) {
          // Switch to INSERT mode
          current_mode_ = MODE_INSERT;
          browser_client_->SetInputMode(getModeName());
          if (browser_) {
            browser_->GetHost()->SetFocus(true); // Ensure caret visibility
            browser_->GetHost()->Invalidate(PET_VIEW);
          }
        } else {
          // Switch to STANDARD mode
          current_mode_ = MODE_STANDARD;
          browser_client_->SetInputMode(getModeName());
          if (browser_) {
            browser_->GetHost()->Invalidate(PET_VIEW);
          }
        }
        browser_client_->ClearModeSwitchRequest();
      }
    }

    char c;
    ssize_t n = read(STDIN_FILENO, &c, 1);

    if (n > 0) {
      read_count++;

      // Start of potential escape sequence
      if (c == '\033') {
        pos = 0;
        buf[pos++] = c;

        // Peek ahead to see if this is part of a sequence
        // Use a very short timeout to check for following bytes
        struct timeval tv;
        tv.tv_sec = 0;
        tv.tv_usec = 1000; // 1ms timeout

        fd_set readfds;
        FD_ZERO(&readfds);
        FD_SET(STDIN_FILENO, &readfds);

        int ready = select(STDIN_FILENO + 1, &readfds, NULL, NULL, &tv);

        // If no data follows within 1ms, treat as standalone ESC
        if (ready <= 0) {
          if (url_input_active_) {
            url_input_active_ = false;
            url_input_buffer_.clear();
            if (browser_client_) {
              browser_client_->SetUrlInputActive(false);
              browser_client_->GetStatusBar()->clear();
              // Invalidate to trigger immediate repaint
              if (browser_) {
                browser_->GetHost()->Invalidate(PET_VIEW);
              }
            }
          } else if (file_input_active_) {
            file_input_active_ = false;
            file_input_buffer_.clear();
            if (browser_client_) {
              browser_client_->HandleFileDialogResponse("");
              browser_client_->GetStatusBar()->clear();
              // Invalidate to trigger immediate repaint
              if (browser_) {
                browser_->GetHost()->Invalidate(PET_VIEW);
              }
            }
          } else if (console_input_active_) {
            console_input_active_ = false;
            console_input_buffer_.clear();
            if (browser_client_) {
              browser_client_->SetConsoleActive(false);
              browser_client_->GetStatusBar()->clear();
              // Force a paint to refresh screen
              if (browser_) {
                browser_->GetHost()->Invalidate(PET_VIEW);
              }
            }
          } else if (search_input_active_) {
            search_input_active_ = false;
            search_input_buffer_.clear();
            search_started_ = false;
            if (browser_client_) {
              browser_client_->SetSearchActive(false);
              browser_client_->GetStatusBar()->clear();
              if (browser_) {
                browser_->GetHost()->StopFinding(true); // Clear highlights
                browser_->GetHost()->Invalidate(PET_VIEW);
              }
            }
          } else if (auth_dialog_active_) {
            auth_dialog_active_ = false;
            auth_username_buffer_.clear();
            auth_password_buffer_.clear();
            auth_password_mode_ = false;
            if (browser_client_) {
              browser_client_->HandleAuthResponse(false);
              browser_client_->GetStatusBar()->clear();
              if (browser_) {
                browser_->GetHost()->Invalidate(PET_VIEW);
              }
            }
          } else if (browser_client_ &&
                     browser_client_->IsPopupConfirmActive()) {
            // Cancel popup confirmation on ESC
            browser_client_->HandlePopupResponse(false);
          } else if (browser_client_ && browser_client_->IsJSDialogActive()) {
            // Cancel JS dialog on ESC (for confirm and prompt)
            auto type = browser_client_->GetJSDialogType();
            if (type == JSDIALOGTYPE_CONFIRM || type == JSDIALOGTYPE_PROMPT) {
              browser_client_->HandleJSDialogResponse(false);
              js_prompt_input_.clear();
            }
          } else if (browser_client_ && browser_client_->IsBookmarksActive()) {
            // Close bookmarks on ESC
            browser_client_->SetBookmarksActive(false);
          } else if (browser_client_ &&
                     browser_client_->IsDownloadManagerActive()) {
            // Close download manager on ESC
            browser_client_->ToggleDownloadManager();
          } else if (browser_client_ &&
                     browser_client_->IsUserScriptsActive()) {
            // Close user scripts on ESC
            browser_client_->SetUserScriptsActive(false);
          } else if (hint_mode_active_) {
            // Cancel hint mode on ESC
            hint_mode_active_ = false;
            hint_input_buffer_.clear();
            if (browser_client_) {
              browser_client_->SetHintModeActive(false);
              browser_client_->GetStatusBar()->clear();
              if (browser_) {
                browser_->GetHost()->Invalidate(PET_VIEW);
              }
            }
          } else if (current_mode_ == MODE_INSERT) {
            // ESC in INSERT mode goes to STANDARD mode
            current_mode_ = MODE_STANDARD;
            if (browser_client_) {
              browser_client_->SetInputMode(getModeName());
              if (browser_) {
                // Blur the active element to remove focus from input
                std::string js = "if (document.activeElement && "
                                 "document.activeElement.blur) { "
                                 "document.activeElement.blur(); }";
                browser_->GetMainFrame()->ExecuteJavaScript(js, "", 0);
                // Remove CEF focus to prevent key events from going to input
                browser_->GetHost()->SetFocus(false);
                browser_->GetHost()->Invalidate(PET_VIEW);
              }
            }
          } else if (current_mode_ == MODE_VISUAL) {
            // ESC in VISUAL mode - exit visual mode, return to STANDARD
            current_mode_ = MODE_STANDARD;
            visual_mode_active_ = false;
            if (browser_client_) {
              browser_client_->SetVisualModeActive(false);
              browser_client_->SetInputMode(getModeName());
              if (browser_) {
                browser_->GetHost()->Invalidate(PET_VIEW);
              }
            }
          } else if (current_mode_ == MODE_MOUSE || mouse_emu_mode_active_) {
            // ESC in MOUSE mode - if grid mode is active, it will close grid
            // Otherwise exit mouse mode
            // If select options are showing, close them first, don't exit mouse
            // emu mode
            if (browser_client_ && browser_client_->IsSelectOptionsActive()) {
              browser_client_->GetStatusBar()->clear();
              if (browser_) {
                browser_->GetHost()->Invalidate(PET_VIEW);
              }
            } else if (browser_client_ && browser_client_->IsGridModeActive()) {
              // Send ESC to JS to close grid mode
              browser_client_->HandleMouseEmuKey("Escape");
            } else {
              // Exit mouse emulation mode on ESC
              current_mode_ = MODE_STANDARD;
              mouse_emu_mode_active_ = false;
              if (browser_client_) {
                browser_client_->SetMouseEmuModeActive(false);
                browser_client_->SetInputMode(getModeName());
                browser_client_->GetStatusBar()->clear();
                browser_client_->ForceFullRedraw();
                if (browser_) {
                  browser_->GetHost()->Invalidate(PET_VIEW);
                }
              }
            }
          } else {
            sendKeyEvent(VKEY_ESCAPE, 0, false);
          }
          pos = 0;
          continue;
        }
      }
      // Building escape sequence
      else if (pos > 0) {
        // If we have just ESC and the next char is not '[', treat ESC as
        // standalone
        if (pos == 1 && buf[0] == '\033' && c != '[') {
          // Handle standalone ESC
          if (url_input_active_) {
            url_input_active_ = false;
            url_input_buffer_.clear();
            if (browser_client_) {
              browser_client_->SetUrlInputActive(false);
              browser_client_->GetStatusBar()->clear();
              // Invalidate to trigger immediate repaint
              if (browser_) {
                browser_->GetHost()->Invalidate(PET_VIEW);
              }
            }
          } else if (search_input_active_) {
            search_input_active_ = false;
            search_input_buffer_.clear();
            search_started_ = false;
            if (browser_client_) {
              browser_client_->SetSearchActive(false);
              browser_client_->GetStatusBar()->clear();
              if (browser_) {
                browser_->GetHost()->StopFinding(true); // Clear highlights
                browser_->GetHost()->Invalidate(PET_VIEW);
              }
            }
          } else if (file_input_active_) {
            file_input_active_ = false;
            file_input_buffer_.clear();
            if (browser_client_) {
              browser_client_->HandleFileDialogResponse("");
              browser_client_->GetStatusBar()->clear();
              // Invalidate to trigger immediate repaint
              if (browser_) {
                browser_->GetHost()->Invalidate(PET_VIEW);
              }
            }
          } else if (console_input_active_) {
            console_input_active_ = false;
            console_input_buffer_.clear();
            if (browser_client_) {
              browser_client_->SetConsoleActive(false);
              browser_client_->GetStatusBar()->clear();
              // Force a paint to refresh screen
              if (browser_) {
                browser_->GetHost()->Invalidate(PET_VIEW);
              }
            }
          } else if (browser_client_ &&
                     browser_client_->IsPopupConfirmActive()) {
            // Cancel popup confirmation on ESC
            browser_client_->HandlePopupResponse(false);
          } else if (browser_client_ && browser_client_->IsJSDialogActive()) {
            // Cancel JS dialog on ESC (for confirm and prompt)
            auto type = browser_client_->GetJSDialogType();
            if (type == JSDIALOGTYPE_CONFIRM || type == JSDIALOGTYPE_PROMPT) {
              browser_client_->HandleJSDialogResponse(false);
              js_prompt_input_.clear();
            }
          } else if (browser_client_ && browser_client_->IsBookmarksActive()) {
            // Close bookmarks on ESC
            browser_client_->SetBookmarksActive(false);
          } else if (browser_client_ &&
                     browser_client_->IsUserScriptsActive()) {
            // Close user scripts on ESC
            browser_client_->SetUserScriptsActive(false);
          } else if (browser_client_ &&
                     browser_client_->IsDownloadManagerActive()) {
            // Close download manager on ESC
            browser_client_->ToggleDownloadManager();
          } else if (hint_mode_active_) {
            // Cancel hint mode on ESC
            hint_mode_active_ = false;
            hint_input_buffer_.clear();
            if (browser_client_) {
              browser_client_->SetHintModeActive(false);
              browser_client_->GetStatusBar()->clear();
              if (browser_) {
                browser_->GetHost()->Invalidate(PET_VIEW);
              }
            }
          } else if (mouse_emu_mode_active_) {
            // If select options are showing, close them first, don't exit mouse
            // emu mode
            if (browser_client_ && browser_client_->IsSelectOptionsActive()) {
              browser_client_->GetStatusBar()->clear();
              if (browser_) {
                browser_->GetHost()->Invalidate(PET_VIEW);
              }
            } else {
              // Cancel mouse emulation mode on ESC only if no select is active
              mouse_emu_mode_active_ = false;
              if (browser_client_) {
                browser_client_->SetMouseEmuModeActive(false);
                browser_client_->GetStatusBar()->clear();
                browser_client_->ForceFullRedraw();
                if (browser_) {
                  browser_->GetHost()->Invalidate(PET_VIEW);
                }
              }
            }
          } else {
            sendKeyEvent(VKEY_ESCAPE, 0, false);
          }
          pos = 0;
          // Now process the current character normally
          if (c >= 32 && c < 127) {
            key_event_count++;
            if (url_input_active_) {
              url_input_buffer_ += c;
              if (browser_client_) {
                browser_client_->GetStatusBar()->showURLInput(
                    url_input_buffer_);
              }
            } else if (file_input_active_) {
              file_input_buffer_ += c;
              if (browser_client_) {
                browser_client_->GetStatusBar()->showFileInput(
                    file_input_buffer_);
              }
            } else if (console_input_active_) {
              console_input_buffer_ += c;
              if (browser_client_) {
                browser_client_->GetStatusBar()->showConsole(
                    browser_client_->GetConsoleLogs(), console_input_buffer_,
                    console_scroll_offset_);
              }
            } else if (browser_client_ &&
                       browser_client_->IsPopupConfirmActive()) {
              // Handle popup confirmation (y/n)
              if (c == 'y' || c == 'Y') {
                browser_client_->HandlePopupResponse(true);
              } else if (c == 'n' || c == 'N') {
                browser_client_->HandlePopupResponse(false);
              }
            } else {
              sendKeyEvent(c, c, true);
            }
          } else if ((unsigned char)c >= 0x80) {
            // UTF-8 multi-byte character after ESC
            std::string utf8_char;
            int bytes_read = readUTF8Char((unsigned char)c, utf8_char);

            if (bytes_read > 0) {
              key_event_count++;
              if (url_input_active_) {
                url_input_buffer_ += utf8_char;
                if (browser_client_) {
                  browser_client_->GetStatusBar()->showURLInput(
                      url_input_buffer_);
                }
              } else if (search_input_active_) {
                search_input_buffer_ += utf8_char;
                if (browser_client_) {
                  browser_client_->GetStatusBar()->showSearchInput(
                      search_input_buffer_);
                }
              } else if (file_input_active_) {
                file_input_buffer_ += utf8_char;
                if (browser_client_) {
                  browser_client_->GetStatusBar()->showFileInput(
                      file_input_buffer_);
                }
              } else if (console_input_active_) {
                console_input_buffer_ += utf8_char;
                if (browser_client_) {
                  browser_client_->GetStatusBar()->showConsole(
                      browser_client_->GetConsoleLogs(), console_input_buffer_,
                      console_scroll_offset_);
                }
              } else if (current_mode_ == MODE_INSERT) {
                sendUTF8CharEvent(utf8_char);
              }
            }
          }
          continue;
        }

        buf[pos++] = c;

        // Check for mouse sequence: ESC[<...M or ESC[<...m
        if (pos >= 6 && buf[0] == '\033' && buf[1] == '[' && buf[2] == '<') {
          if (c == 'M' || c == 'm') {
            mouse_event_count++;
            parseMouseEvent(buf, pos);
            pos = 0;
          }
        }
        // Check for keyboard escape sequences
        else if (pos >= 3 && buf[0] == '\033' && buf[1] == '[') {
          // Single letter sequences (arrows, etc) - but not mouse '<'
          if (buf[2] != '<' &&
              ((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z'))) {
            key_event_count++;
            parseKeySequence(buf, pos);
            pos = 0;
          }
          // Tilde-terminated sequences (Home, End, Delete, etc)
          else if (c == '~') {
            key_event_count++;
            parseKeySequence(buf, pos);
            pos = 0;
          }
        }

        // Buffer overflow protection
        if (pos >= 63) {
          pos = 0;
        }
      }
      // Regular character (not escape sequence)
      else {
        key_event_count++;

        // Log key for debugging
        FILE *keylog = fopen("/tmp/brow6el_debug.log", "a");
        if (keylog && key_event_count % 10 == 0) {
          fprintf(keylog, "Key char: %02x ('%c')\n", (unsigned char)c,
                  (c >= 32 && c < 127) ? c : '?');
          fclose(keylog);
        }

        // Handle special ASCII characters
        if (c == '\r' || c == '\n') {
          // Check if JS dialog is active
          if (browser_client_ && browser_client_->IsJSDialogActive()) {
            auto type = browser_client_->GetJSDialogType();
            if (type == JSDIALOGTYPE_ALERT) {
              browser_client_->HandleJSDialogResponse(true);
            } else if (type == JSDIALOGTYPE_CONFIRM) {
              // Default to OK on Enter
              browser_client_->HandleJSDialogResponse(true);
            } else if (type == JSDIALOGTYPE_PROMPT) {
              browser_client_->HandleJSDialogResponse(true, js_prompt_input_);
              js_prompt_input_.clear();
            }
            continue;
          }
          // Check if console input is active
          if (console_input_active_) {
            if (!console_input_buffer_.empty() && browser_client_) {
              // Check if it's a browser command (starts with :)
              if (console_input_buffer_[0] == ':') {
                std::string command = console_input_buffer_.substr(1);
                if (command == "clear") {
                  // Clear console logs
                  browser_client_->ClearConsoleLogs();
                  console_scroll_offset_ = 0;
                }
                // Add more commands here in the future
                // else if (command == "help") { ... }
              } else {
                // Execute as JavaScript
                browser_client_->ExecuteJavaScript(console_input_buffer_);
              }
              console_input_buffer_.clear();
              // Keep console open, just clear input
              if (browser_client_) {
                browser_client_->GetStatusBar()->showConsole(
                    browser_client_->GetConsoleLogs(), console_input_buffer_,
                    console_scroll_offset_);
              }
            }
            continue;
          }
          // Check if URL input is active
          if (url_input_active_) {
            url_input_active_ = false;
            if (browser_client_) {
              browser_client_->SetUrlInputActive(false);
              browser_client_->GetStatusBar()->clear();
            }
            if (!url_input_buffer_.empty() && browser_) {
              browser_->GetMainFrame()->LoadURL(url_input_buffer_);
            }
            url_input_buffer_.clear();
            continue;
          }
          // Check if search input is active - Enter performs search
          if (search_input_active_) {
            FILE *log = fopen("/tmp/brow6el_search.log", "a");
            if (log) {
              fprintf(log, "Enter pressed in search, buffer='%s'\n", search_input_buffer_.c_str());
              fclose(log);
            }
            if (!search_input_buffer_.empty() && browser_) {
              // Perform search with CEF Find API
              browser_->GetHost()->Find(search_input_buffer_, true, false, false);
              search_started_ = true; // Mark that search has been initiated
            }
            continue;
          }
          // Check if auth dialog is active
          if (auth_dialog_active_) {
            if (!auth_password_mode_) {
              // First Enter - move to password
              auth_password_mode_ = true;
              if (browser_client_) {
                browser_client_->GetStatusBar()->showAuthDialog(
                    "Username: " + auth_username_buffer_ + " | Password:",
                    browser_client_->GetAuthRealm());
              }
            } else {
              // Second Enter - submit
              auth_dialog_active_ = false;
              if (browser_client_) {
                browser_client_->HandleAuthResponse(true, auth_username_buffer_,
                                                   auth_password_buffer_);
                browser_client_->GetStatusBar()->clear();
              }
              auth_username_buffer_.clear();
              auth_password_buffer_.clear();
              auth_password_mode_ = false;
            }
            continue;
          }
          // Check if File input is active
          if (file_input_active_) {
            file_input_active_ = false;
            if (browser_client_) {
              browser_client_->HandleFileDialogResponse(file_input_buffer_);
              browser_client_->GetStatusBar()->clear();
            }
            file_input_buffer_.clear();
            continue;
          }
          // Check if bookmarks is active and handle Enter
          if (browser_client_ && browser_client_->IsBookmarksActive()) {
            browser_client_->HandleBookmarkConfirm();
            continue;
          }
          // Check if user scripts is active and handle Enter
          if (browser_client_ && browser_client_->IsUserScriptsActive()) {
            browser_client_->HandleUserScriptConfirm();
            continue;
          }
          // Check if hint mode is active and handle Enter
          if (hint_mode_active_ && browser_client_) {
            if (!hint_input_buffer_.empty()) {
              browser_client_->HandleHintSelection(hint_input_buffer_);
            }
            hint_mode_active_ = false;
            hint_input_buffer_.clear();
            browser_client_->GetStatusBar()->clear();
            if (browser_) {
              browser_->GetHost()->Invalidate(PET_VIEW);
            }
            continue;
          }
          // Check if status bar is active (select options) - priority over
          // mouse emu
          if (browser_client_ && browser_client_->HandleSelectConfirm()) {
            // Status bar handled the Enter key - don't send to CEF
            continue;
          }
          // Check if mouse emulation mode is active and handle Enter
          if (mouse_emu_mode_active_ && browser_client_) {
            // Pass to JS to handle (click or drop)
            std::string key("Enter");
            browser_client_->HandleMouseEmuKey(key);
            continue;
          }
          sendKeyEvent(VKEY_RETURN, '\r', false);
        } else if (c == '\t') {
          if (!url_input_active_ && !file_input_active_ &&
              !console_input_active_) {
            sendKeyEvent(VKEY_TAB, '\t', false);
          }
        } else if (c == 127 ||
                   c == 8) { // Backspace/DEL (127 in xterm, 8 in mlterm)
          if (browser_client_ && browser_client_->IsJSDialogActive() &&
              browser_client_->GetJSDialogType() == JSDIALOGTYPE_PROMPT) {
            if (!js_prompt_input_.empty()) {
              removeLastUTF8Char(js_prompt_input_);
              if (browser_client_) {
                browser_client_->GetStatusBar()->showJSPrompt(
                    browser_client_->GetJSDialogMessage(), js_prompt_input_);
              }
            }
          } else if (mouse_emu_mode_active_ && browser_client_ &&
                     browser_client_->IsGridModeActive()) {
            // Backspace in grid mode - send to JS for grid navigation
            browser_client_->HandleMouseEmuKey("Backspace");
          } else if (console_input_active_) {
            if (!console_input_buffer_.empty()) {
              removeLastUTF8Char(console_input_buffer_);
              if (browser_client_) {
                browser_client_->GetStatusBar()->showConsole(
                    browser_client_->GetConsoleLogs(), console_input_buffer_,
                    console_scroll_offset_);
              }
            }
          } else if (url_input_active_) {
            if (!url_input_buffer_.empty()) {
              removeLastUTF8Char(url_input_buffer_);
              if (browser_client_) {
                browser_client_->GetStatusBar()->showURLInput(
                    url_input_buffer_);
              }
            }
          } else if (search_input_active_) {
            if (!search_input_buffer_.empty()) {
              removeLastUTF8Char(search_input_buffer_);
              if (browser_client_) {
                browser_client_->GetStatusBar()->showSearchInput(
                    search_input_buffer_);
              }
            }
          } else if (file_input_active_) {
            if (!file_input_buffer_.empty()) {
              removeLastUTF8Char(file_input_buffer_);
              if (browser_client_) {
                browser_client_->GetStatusBar()->showFileInput(
                    file_input_buffer_);
              }
            }
          } else if (auth_dialog_active_) {
            if (auth_password_mode_) {
              if (!auth_password_buffer_.empty()) {
                removeLastUTF8Char(auth_password_buffer_);
                std::string masked(auth_password_buffer_.length(), '*');
                if (browser_client_) {
                  browser_client_->GetStatusBar()->showAuthDialog(
                      "Username: " + auth_username_buffer_ + " | Password: " + masked,
                      browser_client_->GetAuthRealm());
                }
              }
            } else {
              if (!auth_username_buffer_.empty()) {
                removeLastUTF8Char(auth_username_buffer_);
                if (browser_client_) {
                  browser_client_->GetStatusBar()->showAuthDialog(
                      "Username: " + auth_username_buffer_,
                      browser_client_->GetAuthRealm());
                }
              }
            }
          } else if (hint_mode_active_) {
            if (!hint_input_buffer_.empty()) {
              hint_input_buffer_.pop_back();
              if (browser_client_ && browser_client_->GetStatusBar()) {
                browser_client_->GetStatusBar()->showHintInput(
                    hint_input_buffer_, 0);
              }
            }
          } else {
            sendKeyEvent(VKEY_BACK, '\b', false);
          }
        } else if (c == 27) { // Standalone Escape
          if (url_input_active_) {
            url_input_active_ = false;
            url_input_buffer_.clear();
            if (browser_client_) {
              browser_client_->SetUrlInputActive(false);
              browser_client_->GetStatusBar()->clear();
            }
          } else if (auth_dialog_active_) {
            auth_dialog_active_ = false;
            auth_username_buffer_.clear();
            auth_password_buffer_.clear();
            auth_password_mode_ = false;
            if (browser_client_) {
              browser_client_->HandleAuthResponse(false);
              browser_client_->GetStatusBar()->clear();
              if (browser_) {
                browser_->GetHost()->Invalidate(PET_VIEW);
              }
            }
          } else if (hint_mode_active_) {
            // Cancel hint mode on ESC
            hint_mode_active_ = false;
            hint_input_buffer_.clear();
            if (browser_client_) {
              browser_client_->SetHintModeActive(false);
              browser_client_->GetStatusBar()->clear();
              if (browser_) {
                browser_->GetHost()->Invalidate(PET_VIEW);
              }
            }
          } else if (mouse_emu_mode_active_) {
            // If select options are showing, close them first, don't exit mouse
            // emu mode
            if (browser_client_ && browser_client_->IsSelectOptionsActive()) {
              browser_client_->GetStatusBar()->clear();
              if (browser_) {
                browser_->GetHost()->Invalidate(PET_VIEW);
              }
            } else {
              // Cancel mouse emulation mode on ESC only if no select is active
              mouse_emu_mode_active_ = false;
              if (browser_client_) {
                browser_client_->SetMouseEmuModeActive(false);
                browser_client_->GetStatusBar()->clear();
                browser_client_->ForceFullRedraw();
                if (browser_) {
                  browser_->GetHost()->Invalidate(PET_VIEW);
                }
              }
            }
          } else {
            sendKeyEvent(VKEY_ESCAPE, 0, false);
          }
        } else if (c >= 32 && c < 127) {
          // Printable ASCII character
          if (browser_client_ && browser_client_->IsDownloadConfirmActive()) {
            // Handle download confirmation (y/n)
            if (c == 'y' || c == 'Y') {
              browser_client_->HandleDownloadResponse(true);
            } else if (c == 'n' || c == 'N') {
              browser_client_->HandleDownloadResponse(false);
            }
          } else if (browser_client_ &&
                     browser_client_->IsPopupConfirmActive()) {
            // Handle popup confirmation (y/n)
            if (c == 'y' || c == 'Y') {
              browser_client_->HandlePopupResponse(true);
            } else if (c == 'n' || c == 'N') {
              browser_client_->HandlePopupResponse(false);
            }
          } else if (browser_client_ && browser_client_->IsJSDialogActive()) {
            // Handle JS dialog
            auto type = browser_client_->GetJSDialogType();
            if (type == JSDIALOGTYPE_ALERT) {
              // Alert only needs Enter - ignore other keys
            } else if (type == JSDIALOGTYPE_CONFIRM) {
              if (c == 'y' || c == 'Y') {
                browser_client_->HandleJSDialogResponse(true);
              } else if (c == 'n' || c == 'N') {
                browser_client_->HandleJSDialogResponse(false);
              }
            } else if (type == JSDIALOGTYPE_PROMPT) {
              // Add character to prompt input
              js_prompt_input_ += c;
              if (browser_client_) {
                browser_client_->GetStatusBar()->showJSPrompt(
                    browser_client_->GetJSDialogMessage(), js_prompt_input_);
              }
            }
          } else if (browser_client_ && browser_client_->IsBookmarksActive()) {
            // Handle bookmark actions
            if (c == 'd' || c == 'D') {
              browser_client_->HandleBookmarkDelete();
            } else if (c == 'b' || c == 'B') {
              browser_client_->SetBookmarksActive(false);
            }
          } else if (browser_client_ &&
                     browser_client_->IsDownloadManagerActive()) {
            // Handle download manager actions (x=cancel, c=clear, m=close)
            browser_client_->HandleDownloadManagerAction(c);
          } else if (hint_mode_active_) {
            // Handle hint input (a-z only)
            if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z')) {
              hint_input_buffer_ += tolower(c);
              if (browser_client_ && browser_client_->GetStatusBar()) {
                browser_client_->GetStatusBar()->showHintInput(
                    hint_input_buffer_, 0);
              }
            }
          } else if (console_input_active_) {
            console_input_buffer_ += c;
            if (browser_client_) {
              browser_client_->GetStatusBar()->showConsole(
                  browser_client_->GetConsoleLogs(), console_input_buffer_,
                  console_scroll_offset_);
            }
          } else if (url_input_active_) {
            url_input_buffer_ += c;
            if (browser_client_) {
              browser_client_->GetStatusBar()->showURLInput(url_input_buffer_);
            }
          } else if (search_input_active_) {
            // Handle 'n' and 'p' for search navigation - only AFTER Enter was pressed
            if (search_started_ && c == 'n') {
              // Find next
              if (!search_input_buffer_.empty() && browser_) {
                browser_->GetHost()->Find(search_input_buffer_, true, false, true);
                // Don't Invalidate here - OnFindResult will trigger it when ready
              }
            } else if (search_started_ && c == 'p') {
              // Find previous
              if (!search_input_buffer_.empty() && browser_) {
                browser_->GetHost()->Find(search_input_buffer_, false, false, true);
                // Don't Invalidate here - OnFindResult will trigger it when ready
              }
            } else {
              // Regular character - add to search buffer
              search_input_buffer_ += c;
              search_started_ = false; // Reset since query changed
              if (browser_client_) {
                browser_client_->GetStatusBar()->showSearchInput(search_input_buffer_);
              }
            }
          } else if (file_input_active_) {
            file_input_buffer_ += c;
            if (browser_client_) {
              browser_client_->GetStatusBar()->showFileInput(
                  file_input_buffer_);
            }
          } else if (auth_dialog_active_) {
            if (auth_password_mode_) {
              auth_password_buffer_ += c;
              std::string masked(auth_password_buffer_.length(), '*');
              if (browser_client_) {
                browser_client_->GetStatusBar()->showAuthDialog(
                    "Username: " + auth_username_buffer_ + " | Password: " + masked,
                    browser_client_->GetAuthRealm());
              }
            } else {
              auth_username_buffer_ += c;
              if (browser_client_) {
                browser_client_->GetStatusBar()->showAuthDialog(
                    "Username: " + auth_username_buffer_,
                    browser_client_->GetAuthRealm());
              }
            }
          } else {
            // Mode-specific handling
            if (current_mode_ == MODE_INSERT) {
              // INSERT mode: pass everything to CEF
              int keycode = c;
              bool needs_shift = false;

              // Map shifted number row characters
              if (c == '!') {
                keycode = '1';
                needs_shift = true;
              } else if (c == '@') {
                keycode = '2';
                needs_shift = true;
              } else if (c == '#') {
                keycode = '3';
                needs_shift = true;
              } else if (c == '$') {
                keycode = '4';
                needs_shift = true;
              } else if (c == '%') {
                keycode = '5';
                needs_shift = true;
              } else if (c == '^') {
                keycode = '6';
                needs_shift = true;
              } else if (c == '&') {
                keycode = '7';
                needs_shift = true;
              } else if (c == '_') {
                keycode = '-';
                needs_shift = true;
              } else if (c == '.') {
                keycode = '.';
                needs_shift = false;
              } else if (c == '*') {
                keycode = '8';
                needs_shift = true;
              } else if (c == '(') {
                keycode = '9';
                needs_shift = true;
              } else if (c == ')') {
                keycode = '0';
                needs_shift = true;
              } else if (c == '+') {
                keycode = '=';
                needs_shift = true;
              } else if (c == '.' || c == '_') {
                sendKeyEvent(c, c, true, false);
                continue;
              } else if (c >= 'A' && c <= 'Z') {
                needs_shift = true;
              }

              sendKeyEvent(keycode, c, true, needs_shift);
            } else if (current_mode_ == MODE_MOUSE) {
              // MOUSE mode: hjkl for movement, q/f for speed, r for drag, g for
              // grid, space/enter for click, i for inspect, e to exit
              if (browser_client_ && browser_client_->IsGridModeActive()) {
                // Grid mode: pass configured grid keys and g directly without
                // mapping
                std::string grid_keys =
                    ProfileConfig::getInstance().getGridKeys();
                bool is_grid_key = false;
                for (char gk : grid_keys) {
                  if (tolower(c) == tolower(gk)) {
                    is_grid_key = true;
                    break;
                  }
                }

                if (is_grid_key || c == 'g' || c == 'G') {
                  std::string key(1, tolower(c));
                  browser_client_->HandleMouseEmuKey(key);
                  continue;
                }
              } else if (c == 'h' || c == 'H' || c == 'j' || c == 'J' ||
                         c == 'k' || c == 'K' || c == 'l' || c == 'L' ||
                         c == 'q' || c == 'Q' || c == 'f' || c == 'F' ||
                         c == 'r' || c == 'R' || c == 'g' || c == 'G') {
                // Not in grid mode: map hjkl to wasd for movement
                char mapped_key = c;
                if (c == 'h' || c == 'H')
                  mapped_key = 'a';
                else if (c == 'j' || c == 'J')
                  mapped_key = 's';
                else if (c == 'k' || c == 'K')
                  mapped_key = 'w';
                else if (c == 'l' || c == 'L')
                  mapped_key = 'd';
                // r, q, f, g stay as is

                std::string key(1, mapped_key);
                browser_client_->HandleMouseEmuKey(key);
                continue;
              } else if (c == ' ') {
                // Space in mouse mode - pass to JS to handle (click or drop)
                std::string key(1, ' ');
                browser_client_->HandleMouseEmuKey(key);
                continue;
              } else if (c == 'i' || c == 'I') {
                // Toggle inspect mode
                if (browser_client_) {
                  browser_client_->ToggleInspectMode();
                }
                continue;
              } else if (c == 'e' || c == 'E') {
                // Exit mouse mode
                current_mode_ = MODE_STANDARD;
                mouse_emu_mode_active_ = false;
                if (browser_client_) {
                  browser_client_->SetMouseEmuModeActive(false);
                  browser_client_->SetInputMode(getModeName());
                  browser_client_->GetStatusBar()->clear();
                  browser_client_->ForceFullRedraw();
                  if (browser_) {
                    browser_->GetHost()->Invalidate(PET_VIEW);
                  }
                }
              }
              // Ignore other keys in mouse mode
            } else if (current_mode_ == MODE_VISUAL) {
              // VISUAL mode: v to start selecting, h/l/w/b for word, j/k for
              // line, y/Enter to copy, ESC to cancel
              if (c == 'v' || c == 'V' || c == 'h' || c == 'l' || c == 'w' ||
                  c == 'b' || c == 'j' || c == 'k') {
                // Navigation and toggle keys - pass to visual mode JavaScript
                if (browser_client_) {
                  std::string key(1, tolower(c));
                  browser_client_->HandleVisualModeKey(key);
                }
                continue;
              } else if (c == 'y') {
                // Copy and exit visual mode
                if (browser_client_) {
                  browser_client_->HandleVisualModeKey("y");
                  // Exit visual mode
                  current_mode_ = MODE_STANDARD;
                  visual_mode_active_ = false;
                  browser_client_->SetVisualModeActive(false);
                  browser_client_->SetInputMode(getModeName());
                  if (browser_) {
                    browser_->GetHost()->Invalidate(PET_VIEW);
                  }
                }
                continue;
              } else if (c == '\r' || c == '\n') {
                // Enter - copy and exit
                if (browser_client_) {
                  browser_client_->HandleVisualModeKey("Enter");
                  // Exit visual mode
                  current_mode_ = MODE_STANDARD;
                  visual_mode_active_ = false;
                  browser_client_->SetVisualModeActive(false);
                  browser_client_->SetInputMode(getModeName());
                  if (browser_) {
                    browser_->GetHost()->Invalidate(PET_VIEW);
                  }
                }
                continue;
              }
              // ESC is handled separately in ESC section
            } else {
              // STANDARD mode: vim-like shortcuts
              if (c == 'h' || c == 'H') {
                sendKeyEvent(VKEY_LEFT, 0, false);
              } else if (c == 'j' || c == 'J') {
                sendKeyEvent(VKEY_DOWN, 0, false);
              } else if (c == 'k' || c == 'K') {
                sendKeyEvent(VKEY_UP, 0, false);
              } else if (c == 'l' || c == 'L') {
                sendKeyEvent(VKEY_RIGHT, 0, false);
              } else if (c == 'r' || c == 'R') {
                // Reload
                if (browser_)
                  browser_->Reload();
              } else if (c == '+' || c == '=') {
                // Zoom in (+ requires Shift, = doesn't)
                if (browser_) {
                  double step = ProfileConfig::getInstance().getZoomStep();
                  current_zoom_level_ += step;
                  if (current_zoom_level_ > 6.0) {
                    current_zoom_level_ = 6.0;
                  }
                  browser_->GetHost()->SetZoomLevel(current_zoom_level_);
                  browser_->GetHost()->Invalidate(PET_VIEW);
                }
              } else if (c == '-') {
                // Zoom out
                if (browser_) {
                  double step = ProfileConfig::getInstance().getZoomStep();
                  current_zoom_level_ -= step;
                  if (current_zoom_level_ < -6.0) {
                    current_zoom_level_ = -6.0;
                  }
                  browser_->GetHost()->SetZoomLevel(current_zoom_level_);
                  browser_->GetHost()->Invalidate(PET_VIEW);
                }
              } else if (c == '0') {
                // Reset zoom
                if (browser_) {
                  current_zoom_level_ = 0.0;
                  browser_->GetHost()->SetZoomLevel(current_zoom_level_);
                  browser_->GetHost()->Invalidate(PET_VIEW);
                }
              } else if (c == 'u') {
                // Navigate to URL (was Ctrl+L) - lowercase u only
                url_input_active_ = true;
                url_input_buffer_.clear();
                if (browser_client_) {
                  browser_client_->SetUrlInputActive(true);
                  usleep(50000);
                  browser_client_->GetStatusBar()->showURLInput("");
                }
              } else if (c == '/') {
                // Open search input
                search_input_active_ = true;
                search_input_buffer_.clear();
                if (browser_client_) {
                  browser_client_->SetSearchActive(true);
                  usleep(50000);
                  browser_client_->GetStatusBar()->showSearchInput("");
                }
              } else if (c == 'c' || c == 'C') {
                // Toggle console (was Ctrl+K)
                console_input_active_ = !console_input_active_;
                if (console_input_active_) {
                  console_input_buffer_.clear();
                  console_scroll_offset_ = 0;
                  if (browser_client_) {
                    browser_client_->SetConsoleActive(true);
                    usleep(50000);
                    browser_client_->GetStatusBar()->showConsole(
                        browser_client_->GetConsoleLogs(), "", 0);
                  }
                } else {
                  if (browser_client_) {
                    browser_client_->SetConsoleActive(false);
                    browser_client_->GetStatusBar()->clear();
                    if (browser_) {
                      browser_->GetHost()->Invalidate(PET_VIEW);
                    }
                  }
                }
              } else if (c == 'd' || c == 'D') {
                // Add bookmark (was Ctrl+D)
                if (browser_client_) {
                  browser_client_->AddCurrentPageToBookmarks();
                }
              } else if (c == 'b' || c == 'B') {
                // Toggle bookmarks (was Ctrl+B)
                if (browser_client_) {
                  if (browser_client_->IsBookmarksActive()) {
                    browser_client_->SetBookmarksActive(false);
                  } else {
                    browser_client_->SetBookmarksActive(true);
                  }
                }
              } else if (c == 'm' || c == 'M') {
                // Toggle download manager
                if (browser_client_) {
                  browser_client_->ToggleDownloadManager();
                }
              } else if (c == 'f' || c == 'F') {
                // Toggle hint mode (was Ctrl+F)
                if (browser_client_) {
                  if (hint_mode_active_) {
                    hint_mode_active_ = false;
                    hint_input_buffer_.clear();
                    browser_client_->SetHintModeActive(false);
                    browser_client_->GetStatusBar()->clear();
                    if (browser_) {
                      browser_->GetHost()->Invalidate(PET_VIEW);
                    }
                  } else {
                    hint_mode_active_ = true;
                    hint_input_buffer_.clear();
                    browser_client_->ActivateHintMode();
                  }
                }
              } else if (c == 'p' || c == 'P') {
                // Navigate back (was Ctrl+P)
                if (browser_ && browser_->CanGoBack()) {
                  browser_->GoBack();
                }
              } else if (c == 'n' || c == 'N') {
                // Navigate forward (was Ctrl+N)
                if (browser_ && browser_->CanGoForward()) {
                  browser_->GoForward();
                }
              } else if (c == 't' || c == 'T') {
                // Scroll up by 2/3 of window height
                if (browser_) {
                  CefMouseEvent mouse_event;
                  mouse_event.x = 0;
                  mouse_event.y = 0;
                  mouse_event.modifiers = 0;
                  browser_->GetHost()->SendMouseWheelEvent(mouse_event, 0, getScrollAmount());
                }
              } else if (c == 'g' || c == 'G') {
                // Scroll down by 2/3 of window height
                if (browser_) {
                  CefMouseEvent mouse_event;
                  mouse_event.x = 0;
                  mouse_event.y = 0;
                  mouse_event.modifiers = 0;
                  browser_->GetHost()->SendMouseWheelEvent(mouse_event, 0,
                                                           -getScrollAmount());
                }
              } else if (c == 'e' || c == 'E') {
                // Enter mouse emulation mode
                current_mode_ = MODE_MOUSE;
                mouse_emu_mode_active_ = true;
                if (browser_client_) {
                  browser_client_->SetInputMode(getModeName());
                  browser_client_->ActivateMouseEmuMode();
                }
              } else if (c == 'i' || c == 'I') {
                // Enter insert mode
                current_mode_ = MODE_INSERT;
                if (browser_client_) {
                  browser_client_->SetInputMode(getModeName());
                  // Update title display and ensure focus for caret visibility
                  if (browser_) {
                    browser_->GetHost()->SetFocus(true);
                    browser_->GetHost()->Invalidate(PET_VIEW);
                  }
                }
              } else if (c == 's' || c == 'S') {
                // User scripts (was Ctrl+U)
                if (browser_client_) {
                  browser_client_->SetUserScriptsActive(true);
                }
              } else if (c == 'y') {
                // Toggle auto-inject user scripts (lowercase y)
                if (browser_client_) {
                  browser_client_->ToggleAutoInjectUserScripts();
                }
              } else if (c == 'z') {
                // Toggle tiled rendering (z) - only for sixel
                if (browser_client_ && !browser_client_->IsKittyRenderer()) {
                  // Toggle the state
                  tiled_rendering_enabled_ = !tiled_rendering_enabled_;
                  browser_client_->SetTiledRenderingEnabled(
                      tiled_rendering_enabled_);

                  // Update mode display to show new rendering mode
                  browser_client_->SetInputMode(getModeName());

                  // Force a full redraw to show the change
                  if (browser_) {
                    browser_->GetHost()->Invalidate(PET_VIEW);
                  }
                }
              } else if (c == 'Z') {
                // Force full redraw (Z) - useful for both sixel and kitty
                if (browser_client_) {
                  browser_client_->ForceFullRedraw();
                  if (browser_) {
                    browser_->GetHost()->Invalidate(PET_VIEW);
                  }
                }
              } else if (c == 'U') {
                // Copy current URL to clipboard (uppercase U)
                if (browser_client_) {
                  browser_client_->CopyCurrentURL();
                }
              } else if (c == '?') {
                // Show tutorial help
                if (browser_) {
                  // Get executable directory
                  char exe_path[1024];
                  ssize_t len = readlink("/proc/self/exe", exe_path, sizeof(exe_path) - 1);
                  if (len != -1) {
                    exe_path[len] = '\0';
                    std::string exe_dir = exe_path;
                    size_t last_slash = exe_dir.find_last_of('/');
                    if (last_slash != std::string::npos) {
                      exe_dir = exe_dir.substr(0, last_slash);
                    }
                    std::string tutorial_path = exe_dir + "/tutorial.html";
                    std::string url = "file://" + tutorial_path;
                    browser_->GetMainFrame()->LoadURL(url);
                  }
                }
              } else if (c == 'v' || c == 'V') {
                // Enter visual mode (text selection)
                current_mode_ = MODE_VISUAL;
                visual_mode_active_ = true;
                if (browser_client_) {
                  browser_client_->SetInputMode(getModeName());
                  browser_client_->ActivateVisualMode();
                }
              } else if (c == 'x' || c == 'X') {
                // Exit/quit (was Ctrl+X)
                requestShutdown();
              }
              // Other keys in standard mode are ignored
            }
          }
        } else if (c >= 1 && c <= 26) {
          // Ctrl+letter combinations - removed, use vim-style single keys in
          // STANDARD mode instead In INSERT mode, these pass through to CEF
        } else if ((unsigned char)c >= 0x80) {
          // UTF-8 multi-byte character
          std::string utf8_char;
          int bytes_read = readUTF8Char((unsigned char)c, utf8_char);

          if (bytes_read > 0) {
            // Successfully read UTF-8 character
            key_event_count++;

            // Add to appropriate input buffer or send to CEF
            if (browser_client_ && browser_client_->IsJSDialogActive() &&
                browser_client_->GetJSDialogType() == JSDIALOGTYPE_PROMPT) {
              js_prompt_input_ += utf8_char;
              if (browser_client_) {
                browser_client_->GetStatusBar()->showJSPrompt(
                    browser_client_->GetJSDialogMessage(), js_prompt_input_);
              }
            } else if (console_input_active_) {
              console_input_buffer_ += utf8_char;
              if (browser_client_) {
                browser_client_->GetStatusBar()->showConsole(
                    browser_client_->GetConsoleLogs(), console_input_buffer_,
                    console_scroll_offset_);
              }
            } else if (url_input_active_) {
              url_input_buffer_ += utf8_char;
              if (browser_client_) {
                browser_client_->GetStatusBar()->showURLInput(
                    url_input_buffer_);
              }
            } else if (search_input_active_) {
              search_input_buffer_ += utf8_char;
              if (browser_client_) {
                browser_client_->GetStatusBar()->showSearchInput(
                    search_input_buffer_);
              }
            } else if (file_input_active_) {
              file_input_buffer_ += utf8_char;
              if (browser_client_) {
                browser_client_->GetStatusBar()->showFileInput(
                    file_input_buffer_);
              }
            } else if (auth_dialog_active_) {
              if (auth_password_mode_) {
                auth_password_buffer_ += utf8_char;
                std::string masked(auth_password_buffer_.length(), '*');
                if (browser_client_) {
                  browser_client_->GetStatusBar()->showAuthDialog(
                      "Username: " + auth_username_buffer_ + " | Password: " + masked,
                      browser_client_->GetAuthRealm());
                }
              } else {
                auth_username_buffer_ += utf8_char;
                if (browser_client_) {
                  browser_client_->GetStatusBar()->showAuthDialog(
                      "Username: " + auth_username_buffer_,
                      browser_client_->GetAuthRealm());
                }
              }
            } else if (current_mode_ == MODE_INSERT) {
              // In INSERT mode, send UTF-8 character to CEF
              sendUTF8CharEvent(utf8_char);
            }
            // In STANDARD and MOUSE modes, UTF-8 chars are ignored (vim-like
            // behavior)
          }
        }
      }

      // Log progress every 100 events
      if ((mouse_event_count + key_event_count) % 100 == 0) {
        FILE *log = fopen("/tmp/brow6el_debug.log", "a");
        if (log) {
          fprintf(log, "Progress: %d chars read, %d mouse, %d keys\n",
                  read_count, mouse_event_count, key_event_count);
          fclose(log);
        }
      }
    } else {
      // Check if status bar needs redraw (e.g., from async console messages)
      if (console_input_active_ && browser_client_) {
        StatusBar *statusBar = browser_client_->GetStatusBar();
        if (statusBar && statusBar->IsRedrawRequested()) {
          statusBar->ClearRedrawRequest();
          statusBar->showConsole(browser_client_->GetConsoleLogs(),
                                 console_input_buffer_, console_scroll_offset_);
        }
      }
      usleep(1000); // 1ms
    }
  }

  FILE *log2 = fopen("/tmp/brow6el_debug.log", "a");
  if (log2) {
    fprintf(log2, "Read loop exited. Total: %d chars, %d mouse, %d keys\n",
            read_count, mouse_event_count, key_event_count);
    fclose(log2);
  }
}

void InputHandler::parseMouseEvent(const char *seq, int len) {
  // SGR format: ESC[<button;x;y;M (press) or m (release)
  int button = 0;
  int x = 0;
  int y = 0;

  const char *p = seq + 3; // Skip "ESC[<"

  button = atoi(p);
  p = strchr(p, ';');
  if (!p)
    return;
  p++;

  x = atoi(p);
  p = strchr(p, ';');
  if (!p)
    return;
  p++;

  y = atoi(p);

  bool pressed = (seq[len - 1] == 'M');

  // Convert terminal coordinates to pixels
  int pixel_x = (x - 1) * cell_width_ + cell_width_ / 2;
  int pixel_y = (y - 1) * cell_height_ + cell_height_ / 2;

  if (pixel_x < 0)
    pixel_x = 0;
  if (pixel_y < 0)
    pixel_y = 0;
  if (pixel_x >= pixel_width_)
    pixel_x = pixel_width_ - 1;
  if (pixel_y >= pixel_height_)
    pixel_y = pixel_height_ - 1;

  if (!browser_ || !browser_->GetHost())
    return;

  CefMouseEvent mouse_event;
  mouse_event.x = pixel_x;
  mouse_event.y = pixel_y;
  mouse_event.modifiers = 0;

  int button_type = button & 0x03;

  // Handle scroll events
  if (button == 64 || button == 65) {
    int delta_y = (button == 64) ? 120 : -120;
    browser_->GetHost()->SendMouseWheelEvent(mouse_event, 0, delta_y);
    return;
  }

  // Handle button events
  CefBrowserHost::MouseButtonType cef_button;
  switch (button_type) {
  case 0:
    cef_button = MBT_LEFT;
    break;
  case 1:
    cef_button = MBT_MIDDLE;
    break;
  case 2:
    cef_button = MBT_RIGHT;
    break;
  default:
    return;
  }

  // Mouse motion (button & 32 means drag)
  if (button & 32) {
    // Physical mouse drag detected
    if (!physical_mouse_dragging_ && mouse_button_down_) {
      // Start drag via JavaScript
      physical_mouse_dragging_ = true;
      drag_start_x_ = pixel_x;
      drag_start_y_ = pixel_y;

      if (browser_client_ && browser_) {
        std::string js =
            "(function() {"
            "  if (window.__brow6el_physical_drag) {"
            "    window.__brow6el_physical_drag.startDrag(" +
            std::to_string(pixel_x) + "," + std::to_string(pixel_y) +
            ");"
            "  } else {"
            "    window.__brow6el_physical_drag = {"
            "      dragging: false,"
            "      draggedElement: null,"
            "      dragGhost: null,"
            "      startDrag: function(x, y) {"
            "        const el = document.elementFromPoint(x, y);"
            "        if (el && el.draggable) {"
            "          this.dragging = true;"
            "          this.draggedElement = el;"
            "          this.dragGhost = el.cloneNode(true);"
            "          this.dragGhost.style.cssText = "
            "'position:fixed!important;pointer-events:none!important;z-index:"
            "2147483646!important;opacity:0.7!important;transform:scale(0.8)!"
            "important;';"
            "          document.body.appendChild(this.dragGhost);"
            "          this.updateGhost(x, y);"
            "          const evt = new DragEvent('dragstart', {"
            "            bubbles: true, cancelable: true,"
            "            dataTransfer: new DataTransfer()"
            "          });"
            "          el.dispatchEvent(evt);"
            "        }"
            "      },"
            "      updateGhost: function(x, y) {"
            "        if (this.dragGhost) {"
            "          this.dragGhost.style.left = (x + 15) + 'px';"
            "          this.dragGhost.style.top = (y + 15) + 'px';"
            "        }"
            "      },"
            "      moveDrag: function(x, y) {"
            "        if (!this.dragging || !this.draggedElement) return;"
            "        this.updateGhost(x, y);"
            "        const dragEvt = new DragEvent('drag', {"
            "          bubbles: true, cancelable: true,"
            "          clientX: x, clientY: y"
            "        });"
            "        this.draggedElement.dispatchEvent(dragEvt);"
            "        const target = document.elementFromPoint(x, y);"
            "        if (target) {"
            "          const overEvt = new DragEvent('dragover', {"
            "            bubbles: true, cancelable: true,"
            "            clientX: x, clientY: y,"
            "            dataTransfer: new DataTransfer()"
            "          });"
            "          target.dispatchEvent(overEvt);"
            "        }"
            "      },"
            "      endDrag: function(x, y) {"
            "        if (!this.dragging || !this.draggedElement) return;"
            "        if (this.dragGhost) {"
            "          this.dragGhost.remove();"
            "          this.dragGhost = null;"
            "        }"
            "        const target = document.elementFromPoint(x, y);"
            "        if (target) {"
            "          const dropEvt = new DragEvent('drop', {"
            "            bubbles: true, cancelable: true,"
            "            dataTransfer: new DataTransfer()"
            "          });"
            "          target.dispatchEvent(dropEvt);"
            "        }"
            "        const endEvt = new DragEvent('dragend', {"
            "          bubbles: true, cancelable: true"
            "        });"
            "        this.draggedElement.dispatchEvent(endEvt);"
            "        this.dragging = false;"
            "        this.draggedElement = null;"
            "      }"
            "    };"
            "    window.__brow6el_physical_drag.startDrag(" +
            std::to_string(pixel_x) + "," + std::to_string(pixel_y) +
            ");"
            "  }"
            "})();";
        browser_->GetMainFrame()->ExecuteJavaScript(js, "", 0);
      }
    } else if (physical_mouse_dragging_) {
      // Continue drag via JavaScript
      if (browser_client_ && browser_) {
        std::string js = "if (window.__brow6el_physical_drag) {"
                         "  window.__brow6el_physical_drag.moveDrag(" +
                         std::to_string(pixel_x) + "," +
                         std::to_string(pixel_y) +
                         ");"
                         "}";
        browser_->GetMainFrame()->ExecuteJavaScript(js, "", 0);
      }
    }

    // Still send regular mouse events
    browser_->GetHost()->SendMouseMoveEvent(mouse_event, false);
    if (mouse_button_down_) {
      browser_->GetHost()->SendMouseClickEvent(mouse_event, mouse_button_type_,
                                               false, 1);
    }
  }
  // Button press/release
  else {
    // Log all clicks for debugging
    FILE *log = fopen("/tmp/brow6el_debug.log", "a");
    if (log) {
      fprintf(log, "Mouse: btn=%d pos=(%d,%d) %s (pressed=%d button=%d)\n",
              button_type, pixel_x, pixel_y, pressed ? "DOWN" : "UP", pressed,
              button);
      fclose(log);
    }

    // Always update mouse position first
    browser_->GetHost()->SendMouseMoveEvent(mouse_event, false);

    // Send click event: true = mouse up (release), false = mouse down (press)
    bool mouse_up = !pressed;

    // Track clicks at same position for double-click detection
    static int last_x = -1, last_y = -1;
    static int click_count_at_pos = 0;

    if (pressed) {
      // Mouse DOWN
      mouse_button_down_ = true;
      mouse_button_type_ = cef_button;

      if (pixel_x == last_x && pixel_y == last_y) {
        click_count_at_pos++;
      } else {
        click_count_at_pos = 1;
        last_x = pixel_x;
        last_y = pixel_y;
      }
      browser_->GetHost()->SendMouseClickEvent(mouse_event, cef_button, false,
                                               click_count_at_pos);
    } else {
      // Mouse UP
      mouse_button_down_ = false;

      // End physical drag if active
      if (physical_mouse_dragging_) {
        physical_mouse_dragging_ = false;
        if (browser_client_ && browser_) {
          std::string js = "if (window.__brow6el_physical_drag) {"
                           "  window.__brow6el_physical_drag.endDrag(" +
                           std::to_string(pixel_x) + "," +
                           std::to_string(pixel_y) +
                           ");"
                           "}";
          browser_->GetMainFrame()->ExecuteJavaScript(js, "", 0);
        }
      }

      browser_->GetHost()->SendMouseClickEvent(mouse_event, cef_button, true,
                                               click_count_at_pos);

      // Set focus after any physical mouse click to ensure caret visibility
      browser_->GetHost()->SetFocus(true);

      // Detect if we clicked on a text input and switch to INSERT mode
      if (browser_client_ && browser_ && current_mode_ != MODE_MOUSE) {
        std::string js = R"(
                    (function() {
                        var el = document.elementFromPoint()" +
                         std::to_string(pixel_x) + "," +
                         std::to_string(pixel_y) + R"();
                        if (el) {
                            var tagName = el.tagName;
                            var isTextArea = (tagName === 'TEXTAREA');
                            
                            // For INPUT elements, check the type
                            var isTextInput = false;
                            if (tagName === 'INPUT') {
                                var type = (el.type || 'text').toLowerCase();
                                // Text input types that should trigger INSERT mode
                                var textTypes = ['text', 'password', 'email', 'search', 'tel', 'url', 'number', 
                                               'date', 'time', 'datetime-local', 'month', 'week'];
                                isTextInput = textTypes.indexOf(type) !== -1;
                            }
                            
                            if (isTextInput || isTextArea) {
                                console.log('[Brow6el] PHYSICAL_MOUSE_TEXT_INPUT');
                            }
                        }
                    })();
                )";
        browser_->GetMainFrame()->ExecuteJavaScript(js, "", 0);
      }
    }
  }
}

void InputHandler::parseKeySequence(const char *seq, int len) {
  if (len < 3 || seq[0] != '\033' || seq[1] != '[') {
    return;
  }

  // Log the sequence for debugging
  FILE *log = fopen("/tmp/brow6el_debug.log", "a");
  if (log) {
    fprintf(log, "parseKeySequence: len=%d seq=", len);
    for (int i = 0; i < len; i++) {
      fprintf(log, "%02x ", (unsigned char)seq[i]);
    }
    fprintf(log, "(");
    for (int i = 0; i < len; i++) {
      fprintf(log, "%c", (seq[i] >= 32 && seq[i] < 127) ? seq[i] : '?');
    }
    fprintf(log, ")\n");
    fclose(log);
  }

  // Check for Ctrl+Arrow (ESC[1;5C for Ctrl+Right, ESC[1;5D for Ctrl+Left,
  // ESC[1;5A for Ctrl+Up, ESC[1;5B for Ctrl+Down)
  if (len == 6 && seq[2] == '1' && seq[3] == ';' && seq[4] == '5') {
    if (seq[5] == 'C') { // Ctrl+Right - Forward
      if (browser_ && browser_->CanGoForward()) {
        browser_->GoForward();
      }
      return;
    } else if (seq[5] == 'D') { // Ctrl+Left - Back
      if (browser_ && browser_->CanGoBack()) {
        browser_->GoBack();
      }
      return;
    } else if (seq[5] == 'A') { // Ctrl+Up - Scroll up (emulate mouse wheel)
      if (browser_) {
        CefMouseEvent mouse_event;
        mouse_event.x = 0;
        mouse_event.y = 0;
        mouse_event.modifiers = 0;
        browser_->GetHost()->SendMouseWheelEvent(mouse_event, 0,
                                                 120); // Scroll up
      }
      return;
    } else if (seq[5] == 'B') { // Ctrl+Down - Scroll down (emulate mouse wheel)
      if (browser_) {
        CefMouseEvent mouse_event;
        mouse_event.x = 0;
        mouse_event.y = 0;
        mouse_event.modifiers = 0;
        browser_->GetHost()->SendMouseWheelEvent(mouse_event, 0,
                                                 -120); // Scroll down
      }
      return;
    }
  }

  // Arrow keys: ESC[A, ESC[B, ESC[C, ESC[D
  if (len == 3) {
    switch (seq[2]) {
    case 'A': // Up arrow
    case 'B': // Down arrow
      // Check if console is active - handle scrolling there
      if (console_input_active_ && browser_client_) {
        if (seq[2] == 'A') {
          console_scroll_offset_++; // Scroll up (show older messages)
        } else {
          if (console_scroll_offset_ > 0) {
            console_scroll_offset_--; // Scroll down (show newer messages)
          }
        }
        browser_client_->GetStatusBar()->showConsole(
            browser_client_->GetConsoleLogs(), console_input_buffer_,
            console_scroll_offset_);
        return;
      }
      // Check if bookmarks is showing - handle navigation there
      if (browser_client_ && browser_client_->IsBookmarksActive()) {
        browser_client_->HandleBookmarkNavigation(seq[2] == 'A' ? -1 : 1);
        return;
      }
      // Check if download manager is showing - handle navigation there
      if (browser_client_ && browser_client_->IsDownloadManagerActive()) {
        browser_client_->HandleDownloadManagerNavigation(seq[2] == 'A' ? -1
                                                                       : 1);
        return;
      }
      // Check if user scripts is showing - handle navigation there
      if (browser_client_ && browser_client_->IsUserScriptsActive()) {
        browser_client_->HandleUserScriptNavigation(seq[2] == 'A' ? -1 : 1);
        return;
      }
      // Check if status bar is showing (e.g., select options) - handle
      // selection there first This takes priority over mouse emulation so
      // select navigation works
      if (browser_client_ &&
          browser_client_->HandleSelectNavigation(seq[2] == 'A' ? -1 : 1)) {
        return; // Status bar handled it
      }
      // Check if mouse emulation mode is active - handle arrow keys there
      if (mouse_emu_mode_active_ && browser_client_) {
        browser_client_->HandleMouseEmuKey(seq[2] == 'A' ? "ArrowUp"
                                                         : "ArrowDown");
        return;
      }
      sendKeyEvent(seq[2] == 'A' ? VKEY_UP : VKEY_DOWN, 0, false);
      return;
    case 'C':
      // Check for status bar first (left/right might be used for something)
      if (browser_client_ && browser_client_->HandleSelectNavigation(0)) {
        // Status bar is active, but left/right don't navigate - let it fall
        // through
      }
      if (mouse_emu_mode_active_ && browser_client_) {
        browser_client_->HandleMouseEmuKey("ArrowRight");
        return;
      }
      sendKeyEvent(VKEY_RIGHT, 0, false);
      return;
    case 'D':
      // Check for status bar first
      if (browser_client_ && browser_client_->HandleSelectNavigation(0)) {
        // Status bar is active, but left/right don't navigate - let it fall
        // through
      }
      if (mouse_emu_mode_active_ && browser_client_) {
        browser_client_->HandleMouseEmuKey("ArrowLeft");
        return;
      }
      sendKeyEvent(VKEY_LEFT, 0, false);
      return;
    case 'H':
      sendKeyEvent(VKEY_HOME, 0, false);
      return;
    case 'F':
      sendKeyEvent(VKEY_END, 0, false);
      return;
    }
  }

  // Sequences ending with ~
  if (seq[len - 1] == '~') {
    int num = atoi(seq + 2);
    switch (num) {
    case 1:
      sendKeyEvent(VKEY_HOME, 0, false);
      return;
    case 3:
      sendKeyEvent(VKEY_DELETE, 0, false);
      return;
    case 4:
      sendKeyEvent(VKEY_END, 0, false);
      return;
    case 5:
      sendKeyEvent(0x21, 0, false);
      return; // Page Up
    case 6:
      sendKeyEvent(0x22, 0, false);
      return; // Page Down
    }
  }
}

void InputHandler::sendKeyEvent(int key_code, char character,
                                bool is_char_event, bool shift_pressed) {
  if (!browser_ || !browser_->GetHost()) {
    return;
  }

  // Log all key events
  FILE *log = fopen("/tmp/brow6el_debug.log", "a");
  if (log) {
    fprintf(log, "sendKeyEvent: code=%d char=%02x is_char=%d shift=%d\n",
            key_code, (unsigned char)character, is_char_event, shift_pressed);
    fclose(log);
  }

  CefKeyEvent key_event;
  key_event.modifiers = shift_pressed ? EVENTFLAG_SHIFT_DOWN : 0;
  key_event.is_system_key = 0;
  key_event.focus_on_editable_field = 1;

  if (is_char_event) {
    // For printable characters, just send CHAR event with the character
    // Don't use key_code as it conflicts with special keys
    key_event.windows_key_code = character;
    key_event.native_key_code = character;
    key_event.character = character;
    key_event.unmodified_character = character;

    key_event.type = KEYEVENT_CHAR;
    browser_->GetHost()->SendKeyEvent(key_event);
  } else {
    // For special keys: use key_code
    key_event.windows_key_code = key_code;
    key_event.native_key_code = key_code;
    key_event.character = character;
    key_event.unmodified_character = character;

    // For special keys: RAWKEYDOWN + CHAR (if provided) + KEYUP
    key_event.type = KEYEVENT_RAWKEYDOWN;
    browser_->GetHost()->SendKeyEvent(key_event);

    if (character != 0) {
      key_event.type = KEYEVENT_CHAR;
      browser_->GetHost()->SendKeyEvent(key_event);
    }

    key_event.type = KEYEVENT_KEYUP;
    browser_->GetHost()->SendKeyEvent(key_event);
  }
}
