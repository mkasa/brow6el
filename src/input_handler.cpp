#include "input_handler.h"
#include "browser_client.h"
#include "status_bar.h"
#include "include/internal/cef_types.h"
#include <iostream>
#include <unistd.h>
#include <cstring>

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

InputHandler::InputHandler(CefRefPtr<CefBrowser> browser, int term_width, int term_height,
                           int cell_width, int cell_height, int pixel_width, int pixel_height)
    : browser_(browser), term_width_(term_width), term_height_(term_height),
      cell_width_(cell_width), cell_height_(cell_height),
      pixel_width_(pixel_width), pixel_height_(pixel_height),
      running_(false), browser_client_(nullptr) {
}

InputHandler::~InputHandler() {
    stop();
}

void InputHandler::start() {
    if (running_) return;
    
    // Log startup
    FILE* log = fopen("/tmp/brow6el_debug.log", "w");
    if (log) {
        fprintf(log, "InputHandler starting...\n");
        fprintf(log, "Terminal: %dx%d cells, cell size: %dx%d, pixels: %dx%d\n",
                term_width_, term_height_, cell_width_, cell_height_, 
                pixel_width_, pixel_height_);
        fclose(log);
    }
    
    enableMouseTracking();
    running_ = true;
    reader_thread_ = std::thread(&InputHandler::readLoop, this);
}

void InputHandler::stop() {
    if (!running_) return;
    
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
    new_tio.c_lflag &= ~(ICANON | ECHO);  // Disable canonical mode and echo
    new_tio.c_cc[VMIN] = 0;   // Non-blocking read
    new_tio.c_cc[VTIME] = 0;
    tcsetattr(STDIN_FILENO, TCSANOW, &new_tio);
    
    // Enable SGR mouse tracking
    printf("\033[?1003h");  // Enable mouse motion tracking
    printf("\033[?1006h");  // Enable SGR extended mouse mode
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

void InputHandler::readLoop() {
    char buf[64];
    int pos = 0;
    int read_count = 0;
    int mouse_event_count = 0;
    int key_event_count = 0;
    
    FILE* log = fopen("/tmp/brow6el_debug.log", "a");
    if (log) {
        fprintf(log, "Read loop started\n");
        fclose(log);
    }
    
    while (running_) {
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
                    } else if (browser_client_ && browser_client_->IsPopupConfirmActive()) {
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
                    } else if (browser_client_ && browser_client_->IsUserScriptsActive()) {
                        // Close user scripts on ESC
                        browser_client_->SetUserScriptsActive(false);
                    } else {
                        sendKeyEvent(VKEY_ESCAPE, 0, false);
                    }
                    pos = 0;
                    continue;
                }
            }
            // Building escape sequence
            else if (pos > 0) {
                // If we have just ESC and the next char is not '[', treat ESC as standalone
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
                    } else if (browser_client_ && browser_client_->IsPopupConfirmActive()) {
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
                    } else if (browser_client_ && browser_client_->IsUserScriptsActive()) {
                        // Close user scripts on ESC
                        browser_client_->SetUserScriptsActive(false);
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
                                browser_client_->GetStatusBar()->showURLInput(url_input_buffer_);
                            }
                        } else if (console_input_active_) {
                            console_input_buffer_ += c;
                            if (browser_client_) {
                                browser_client_->GetStatusBar()->showConsole(
                                    browser_client_->GetConsoleLogs(), 
                                    console_input_buffer_, 
                                    console_scroll_offset_);
                            }
                        } else if (browser_client_ && browser_client_->IsPopupConfirmActive()) {
                            // Handle popup confirmation (y/n)
                            if (c == 'y' || c == 'Y') {
                                browser_client_->HandlePopupResponse(true);
                            } else if (c == 'n' || c == 'N') {
                                browser_client_->HandlePopupResponse(false);
                            }
                        } else {
                            sendKeyEvent(c, c, true);
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
                    if (buf[2] != '<' && ((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z'))) {
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
                FILE* keylog = fopen("/tmp/brow6el_debug.log", "a");
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
                                    browser_client_->GetConsoleLogs(), 
                                    console_input_buffer_, 
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
                    // Check if status bar is active and handle Enter
                    if (browser_client_ && browser_client_->HandleSelectConfirm()) {
                        // Status bar handled the Enter key - don't send to CEF
                        continue;
                    }
                    sendKeyEvent(VKEY_RETURN, '\r', false);
                } else if (c == '\t') {
                    if (!url_input_active_ && !console_input_active_) {
                        sendKeyEvent(VKEY_TAB, '\t', false);
                    }
                } else if (c == 127) { // Backspace/DEL
                    if (browser_client_ && browser_client_->IsJSDialogActive() && 
                        browser_client_->GetJSDialogType() == JSDIALOGTYPE_PROMPT) {
                        if (!js_prompt_input_.empty()) {
                            js_prompt_input_.pop_back();
                            if (browser_client_) {
                                browser_client_->GetStatusBar()->showJSPrompt(
                                    browser_client_->GetJSDialogMessage(), 
                                    js_prompt_input_);
                            }
                        }
                    } else if (console_input_active_) {
                        if (!console_input_buffer_.empty()) {
                            console_input_buffer_.pop_back();
                            if (browser_client_) {
                                browser_client_->GetStatusBar()->showConsole(
                                    browser_client_->GetConsoleLogs(), 
                                    console_input_buffer_, 
                                    console_scroll_offset_);
                            }
                        }
                    } else if (url_input_active_) {
                        if (!url_input_buffer_.empty()) {
                            url_input_buffer_.pop_back();
                            if (browser_client_) {
                                browser_client_->GetStatusBar()->showURLInput(url_input_buffer_);
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
                    } else if (browser_client_ && browser_client_->IsPopupConfirmActive()) {
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
                                    browser_client_->GetJSDialogMessage(), 
                                    js_prompt_input_);
                            }
                        }
                    } else if (browser_client_ && browser_client_->IsBookmarksActive()) {
                        // Handle bookmark deletion
                        if (c == 'd' || c == 'D') {
                            browser_client_->HandleBookmarkDelete();
                        }
                    } else if (console_input_active_) {
                        console_input_buffer_ += c;
                        if (browser_client_) {
                            browser_client_->GetStatusBar()->showConsole(
                                browser_client_->GetConsoleLogs(), 
                                console_input_buffer_, 
                                console_scroll_offset_);
                        }
                    } else if (url_input_active_) {
                        url_input_buffer_ += c;
                        if (browser_client_) {
                            browser_client_->GetStatusBar()->showURLInput(url_input_buffer_);
                        }
                    } else {
                        // Map special shifted characters to their base keys + shift modifier
                        int keycode = c;
                        bool needs_shift = false;
                        
                        // Map shifted number row characters
                        if (c == '!') { keycode = '1'; needs_shift = true; }
                        else if (c == '@') { keycode = '2'; needs_shift = true; }
                        else if (c == '#') { keycode = '3'; needs_shift = true; }
                        else if (c == '$') { keycode = '4'; needs_shift = true; }
                        else if (c == '%') { keycode = '5'; needs_shift = true; }
                        else if (c == '^') { keycode = '6'; needs_shift = true; }
                        else if (c == '&') { keycode = '7'; needs_shift = true; }
                        else if (c == '_') { keycode = '-'; needs_shift = true; }
                        else if (c == '.') { keycode = '.'; needs_shift = false; }
                        else if (c == '*') { keycode = '8'; needs_shift = true; }
                        else if (c == '(') { keycode = '9'; needs_shift = true; }
                        else if (c == ')') { keycode = '0'; needs_shift = true; }
                        else if (c == '_') { keycode = '-'; needs_shift = true; }
                        else if (c == '+') { keycode = '='; needs_shift = true; }
                        // Period and underscore need special handling - send as char events
                        else if (c == '.' || c == '_') {
                            // Send as CHAR event only for these problematic characters
                            sendKeyEvent(c, c, true, false);
                            continue;
                        }
                        // Uppercase letters
                        else if (c >= 'A' && c <= 'Z') { needs_shift = true; }
                        
                        sendKeyEvent(keycode, c, true, needs_shift);
                    }
                } else if (c >= 1 && c <= 26) {
                    // Ctrl+letter combinations
                    int letter = c + 'A' - 1;
                    
                    // Handle keyboard shortcuts
                    FILE* keylog = fopen("/tmp/brow6el_debug.log", "a");
                    if (keylog) {
                        fprintf(keylog, "Checking char: %d (0x%02x)\n", (int)c, (unsigned char)c);
                        fclose(keylog);
                    }
                    
                    if (c == 24) { // Ctrl+X
                        FILE* keylog2 = fopen("/tmp/brow6el_debug.log", "a");
                        if (keylog2) {
                            fprintf(keylog2, "Ctrl+X detected - requesting shutdown\n");
                            fclose(keylog2);
                        }
                        requestShutdown();
                    } else if (c == 18) { // Ctrl+R - Reload
                        if (!url_input_active_ && !console_input_active_ && browser_) {
                            browser_->Reload();
                        }
                    } else if (c == 12) { // Ctrl+L - Navigate to URL
                        if (!url_input_active_ && !console_input_active_) {
                            url_input_active_ = true;
                            url_input_buffer_.clear();
                            if (browser_client_) {
                                browser_client_->SetUrlInputActive(true);
                                // Small delay to ensure rendering stops
                                usleep(50000); // 50ms
                                browser_client_->GetStatusBar()->showURLInput("");
                            }
                        }
                    } else if (c == 11) { // Ctrl+K - Toggle console
                        if (!url_input_active_) {
                            console_input_active_ = !console_input_active_;
                            if (console_input_active_) {
                                console_input_buffer_.clear();
                                console_scroll_offset_ = 0;
                                if (browser_client_) {
                                    browser_client_->SetConsoleActive(true);
                                    usleep(50000); // 50ms
                                    browser_client_->GetStatusBar()->showConsole(
                                        browser_client_->GetConsoleLogs(), "", 0);
                                }
                            } else {
                                if (browser_client_) {
                                    browser_client_->SetConsoleActive(false);
                                    browser_client_->GetStatusBar()->clear();
                                    // Force a paint to refresh screen
                                    if (browser_) {
                                        browser_->GetHost()->Invalidate(PET_VIEW);
                                    }
                                }
                            }
                        }
                    } else if (c == 4) { // Ctrl+D - Add bookmark
                        if (!url_input_active_ && !console_input_active_ && browser_client_) {
                            browser_client_->AddCurrentPageToBookmarks();
                        }
                    } else if (c == 2) { // Ctrl+B - Open bookmarks
                        if (!url_input_active_ && !console_input_active_ && browser_client_) {
                            browser_client_->SetBookmarksActive(true);
                        }
                    } else if (c == 21) { // Ctrl+U - Open user scripts
                        if (!url_input_active_ && !console_input_active_ && browser_client_) {
                            browser_client_->SetUserScriptsActive(true);
                        }
                    } else if (c == 25) { // Ctrl+Y - Toggle auto-inject user scripts
                        if (!url_input_active_ && !console_input_active_ && browser_client_) {
                            browser_client_->ToggleAutoInjectUserScripts();
                        }
                    } else {
                        if (!url_input_active_ && !console_input_active_) {
                            sendKeyEvent(letter, c, false);
                        }
                    }
                }
            }
            
            // Log progress every 100 events
            if ((mouse_event_count + key_event_count) % 100 == 0) {
                FILE* log = fopen("/tmp/brow6el_debug.log", "a");
                if (log) {
                    fprintf(log, "Progress: %d chars read, %d mouse, %d keys\n",
                            read_count, mouse_event_count, key_event_count);
                    fclose(log);
                }
            }
        } else {
            // Check if status bar needs redraw (e.g., from async console messages)
            if (console_input_active_ && browser_client_) {
                StatusBar* statusBar = browser_client_->GetStatusBar();
                if (statusBar && statusBar->IsRedrawRequested()) {
                    statusBar->ClearRedrawRequest();
                    statusBar->showConsole(
                        browser_client_->GetConsoleLogs(), 
                        console_input_buffer_, 
                        console_scroll_offset_);
                }
            }
            usleep(1000); // 1ms
        }
    }
    
    FILE* log2 = fopen("/tmp/brow6el_debug.log", "a");
    if (log2) {
        fprintf(log2, "Read loop exited. Total: %d chars, %d mouse, %d keys\n",
                read_count, mouse_event_count, key_event_count);
        fclose(log2);
    }
}

void InputHandler::parseMouseEvent(const char* seq, int len) {
    // SGR format: ESC[<button;x;y;M (press) or m (release)
    int button = 0;
    int x = 0;
    int y = 0;
    
    const char* p = seq + 3; // Skip "ESC[<"
    
    button = atoi(p);
    p = strchr(p, ';');
    if (!p) return;
    p++;
    
    x = atoi(p);
    p = strchr(p, ';');
    if (!p) return;
    p++;
    
    y = atoi(p);
    
    bool pressed = (seq[len-1] == 'M');
    
    // Convert terminal coordinates to pixels
    int pixel_x = (x - 1) * cell_width_ + cell_width_ / 2;
    int pixel_y = (y - 1) * cell_height_ + cell_height_ / 2;
    
    if (pixel_x < 0) pixel_x = 0;
    if (pixel_y < 0) pixel_y = 0;
    if (pixel_x >= pixel_width_) pixel_x = pixel_width_ - 1;
    if (pixel_y >= pixel_height_) pixel_y = pixel_height_ - 1;
    
    if (!browser_ || !browser_->GetHost()) return;
    
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
        case 0: cef_button = MBT_LEFT; break;
        case 1: cef_button = MBT_MIDDLE; break;
        case 2: cef_button = MBT_RIGHT; break;
        default: return;
    }
    
    // Mouse motion (button & 32 means drag)
    if (button & 32) {
        // During drag, send move event and keep the button state if button is down
        browser_->GetHost()->SendMouseMoveEvent(mouse_event, false);
        // If we're dragging and a button was pressed, send click event to maintain selection
        if (mouse_button_down_) {
            // Keep sending mouse down events during drag for text selection
            browser_->GetHost()->SendMouseClickEvent(mouse_event, mouse_button_type_, false, 1);
        }
    }
    // Button press/release
    else {
        // Log all clicks for debugging
        FILE* log = fopen("/tmp/brow6el_debug.log", "a");
        if (log) {
            fprintf(log, "Mouse: btn=%d pos=(%d,%d) %s (pressed=%d button=%d)\n", 
                    button_type, pixel_x, pixel_y, pressed ? "DOWN" : "UP", pressed, button);
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
            browser_->GetHost()->SendMouseClickEvent(mouse_event, cef_button, false, click_count_at_pos);
        } else {
            // Mouse UP
            mouse_button_down_ = false;
            browser_->GetHost()->SendMouseClickEvent(mouse_event, cef_button, true, click_count_at_pos);
        }
    }
}

void InputHandler::parseKeySequence(const char* seq, int len) {
    if (len < 3 || seq[0] != '\033' || seq[1] != '[') {
        return;
    }
    
    // Check for Ctrl+Arrow (ESC[1;5C for Ctrl+Right, ESC[1;5D for Ctrl+Left)
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
                        console_scroll_offset_++;  // Scroll up (show older messages)
                    } else {
                        if (console_scroll_offset_ > 0) {
                            console_scroll_offset_--;  // Scroll down (show newer messages)
                        }
                    }
                    browser_client_->GetStatusBar()->showConsole(
                        browser_client_->GetConsoleLogs(), 
                        console_input_buffer_, 
                        console_scroll_offset_);
                    return;
                }
                // Check if bookmarks is showing - handle navigation there
                if (browser_client_ && browser_client_->IsBookmarksActive()) {
                    browser_client_->HandleBookmarkNavigation(seq[2] == 'A' ? -1 : 1);
                    return;
                }
                // Check if user scripts is showing - handle navigation there
                if (browser_client_ && browser_client_->IsUserScriptsActive()) {
                    browser_client_->HandleUserScriptNavigation(seq[2] == 'A' ? -1 : 1);
                    return;
                }
                // Check if status bar is showing - handle selection there
                if (browser_client_ && browser_client_->HandleSelectNavigation(seq[2] == 'A' ? -1 : 1)) {
                    return; // Status bar handled it
                }
                sendKeyEvent(seq[2] == 'A' ? VKEY_UP : VKEY_DOWN, 0, false);
                return;
            case 'C': sendKeyEvent(VKEY_RIGHT, 0, false); return;
            case 'D': sendKeyEvent(VKEY_LEFT, 0, false); return;
            case 'H': sendKeyEvent(VKEY_HOME, 0, false); return;
            case 'F': sendKeyEvent(VKEY_END, 0, false); return;
        }
    }
    
    // Sequences ending with ~
    if (seq[len-1] == '~') {
        int num = atoi(seq + 2);
        switch (num) {
            case 1: sendKeyEvent(VKEY_HOME, 0, false); return;
            case 3: sendKeyEvent(VKEY_DELETE, 0, false); return;
            case 4: sendKeyEvent(VKEY_END, 0, false); return;
            case 5: sendKeyEvent(0x21, 0, false); return; // Page Up
            case 6: sendKeyEvent(0x22, 0, false); return; // Page Down
        }
    }
}

void InputHandler::sendKeyEvent(int key_code, char character, bool is_char_event, bool shift_pressed) {
    if (!browser_ || !browser_->GetHost()) {
        return;
    }
    
    // Log all key events
    FILE* log = fopen("/tmp/brow6el_debug.log", "a");
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
