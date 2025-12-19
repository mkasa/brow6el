#include "keyboard_handler.h"
#include "include/internal/cef_types.h"
#include <iostream>
#include <unistd.h>
#include <cstring>
#include <X11/keysym.h>

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

KeyboardHandler::KeyboardHandler(CefRefPtr<CefBrowser> browser)
    : browser_(browser), running_(false) {
}

KeyboardHandler::~KeyboardHandler() {
    stop();
}

void KeyboardHandler::start() {
    if (running_) return;
    
    FILE* log = fopen("/tmp/brow6el_debug.log", "a");
    if (log) {
        fprintf(log, "KeyboardHandler starting...\n");
        fclose(log);
    }
    
    running_ = true;
    reader_thread_ = std::thread(&KeyboardHandler::readLoop, this);
}

void KeyboardHandler::stop() {
    if (!running_) return;
    
    running_ = false;
    if (reader_thread_.joinable()) {
        reader_thread_.join();
    }
}

void KeyboardHandler::readLoop() {
    char buf[64];
    int pos = 0;
    int key_count = 0;
    
    FILE* log = fopen("/tmp/brow6el_debug.log", "a");
    if (log) {
        fprintf(log, "Keyboard read loop started\n");
        fclose(log);
    }
    
    while (running_) {
        char c;
        ssize_t n = read(STDIN_FILENO, &c, 1);
        
        if (n > 0) {
            // Check if this is start of escape sequence
            if (c == '\033') {
                pos = 0;
                buf[pos++] = c;
            }
            // Building escape sequence
            else if (pos > 0) {
                buf[pos++] = c;
                
                // Check if it's a complete escape sequence
                // Arrow keys: ESC[A, ESC[B, ESC[C, ESC[D
                // Function keys, Home, End, etc: ESC[...~
                if (pos >= 3 && buf[0] == '\033' && buf[1] == '[') {
                    // Single letter sequences (arrows, etc)
                    if ((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z')) {
                        parseKeySequence(buf, pos);
                        pos = 0;
                        key_count++;
                    }
                    // Tilde-terminated sequences
                    else if (c == '~') {
                        parseKeySequence(buf, pos);
                        pos = 0;
                        key_count++;
                    }
                    // Mouse sequence (skip it)
                    else if (buf[2] == '<' && (c == 'M' || c == 'm')) {
                        pos = 0; // Discard mouse events
                    }
                }
                
                // Buffer overflow protection
                if (pos >= 63) {
                    pos = 0;
                }
            }
            // Regular character (not escape sequence)
            else {
                key_count++;
                
                // Log occasionally
                if (key_count % 10 == 0) {
                    FILE* log = fopen("/tmp/brow6el_debug.log", "a");
                    if (log) {
                        fprintf(log, "Keyboard: %d keys processed, last char: %02x ('%c')\n", 
                                key_count, (unsigned char)c, (c >= 32 && c < 127) ? c : '?');
                        fclose(log);
                    }
                }
                
                // Handle special ASCII characters
                if (c == '\r' || c == '\n') {
                    sendKeyEvent(VKEY_RETURN, '\r', false);
                } else if (c == '\t') {
                    sendKeyEvent(VKEY_TAB, '\t', false);
                } else if (c == 127) { // Backspace/DEL
                    sendKeyEvent(VKEY_BACK, '\b', false);
                } else if (c == 27) { // Escape (standalone)
                    sendKeyEvent(VKEY_ESCAPE, 0, false);
                } else if (c >= 32 && c < 127) {
                    // Printable ASCII character
                    sendKeyEvent(c, c, true);
                } else if (c >= 1 && c <= 26) {
                    // Ctrl+letter combinations (Ctrl+A = 1, Ctrl+Z = 26)
                    FILE* log = fopen("/tmp/brow6el_debug.log", "a");
                    if (log) {
                        fprintf(log, "Ctrl+key detected: %d (Ctrl+%c)\n", c, c + 'A' - 1);
                        fclose(log);
                    }
                    
                    // Handle special shortcuts
                    if (c == 24) { // Ctrl+X - Quit
                        FILE* log = fopen("/tmp/brow6el_debug.log", "a");
                        if (log) {
                            fprintf(log, "Ctrl+X detected, exiting immediately...\n");
                            fclose(log);
                        }
                        // Use _exit() to avoid CEF shutdown crash
                        _exit(0);
                    } else if (c == 18) { // Ctrl+R - reload
                        FILE* log = fopen("/tmp/brow6el_debug.log", "a");
                        if (log) {
                            fprintf(log, "Ctrl+R detected, reloading...\n");
                            fclose(log);
                        }
                        if (browser_) {
                            browser_->ReloadIgnoreCache();
                        }
                    } else {
                        int letter = c + 'A' - 1;
                        sendKeyEvent(letter, c, false);
                    }
                }
            }
        } else {
            usleep(1000); // 1ms
        }
    }
    
    FILE* log2 = fopen("/tmp/brow6el_debug.log", "a");
    if (log2) {
        fprintf(log2, "Keyboard read loop exited, total keys: %d\n", key_count);
        fclose(log2);
    }
}

void KeyboardHandler::parseKeySequence(const char* seq, int len) {
    if (len < 3 || seq[0] != '\033' || seq[1] != '[') {
        return;
    }
    
    FILE* log = fopen("/tmp/brow6el_debug.log", "a");
    if (log) {
        fprintf(log, "Key sequence: ");
        for (int i = 0; i < len; i++) {
            fprintf(log, "%02x ", (unsigned char)seq[i]);
        }
        fprintf(log, "\n");
        fclose(log);
    }
    
    // Ctrl+Arrow keys: ESC[1;5C (right), ESC[1;5D (left), etc
    if (len >= 6 && seq[2] == '1' && seq[3] == ';' && seq[4] == '5') {
        FILE* log2 = fopen("/tmp/brow6el_debug.log", "a");
        switch (seq[5]) {
            case 'C': // Ctrl+Right - forward
                if (log2) {
                    fprintf(log2, "Ctrl+Right detected, can forward=%d\n", browser_ && browser_->CanGoForward());
                    fclose(log2);
                }
                if (browser_ && browser_->CanGoForward()) {
                    browser_->GoForward();
                }
                return;
            case 'D': // Ctrl+Left - back
                if (log2) {
                    fprintf(log2, "Ctrl+Left detected, can back=%d\n", browser_ && browser_->CanGoBack());
                    fclose(log2);
                }
                if (browser_ && browser_->CanGoBack()) {
                    browser_->GoBack();
                }
                return;
        }
        if (log2) fclose(log2);
    }
    
    // Arrow keys: ESC[A, ESC[B, ESC[C, ESC[D
    if (len == 3) {
        switch (seq[2]) {
            case 'A': sendKeyEvent(VKEY_UP, 0, false); return;
            case 'B': sendKeyEvent(VKEY_DOWN, 0, false); return;
            case 'C': sendKeyEvent(VKEY_RIGHT, 0, false); return;
            case 'D': sendKeyEvent(VKEY_LEFT, 0, false); return;
            case 'H': sendKeyEvent(VKEY_HOME, 0, false); return;
            case 'F': sendKeyEvent(VKEY_END, 0, false); return;
        }
    }
    
    // Sequences ending with ~: ESC[1~, ESC[3~, etc
    if (seq[len-1] == '~') {
        int num = atoi(seq + 2);
        switch (num) {
            case 1: sendKeyEvent(VKEY_HOME, 0, false); return;
            case 3: sendKeyEvent(VKEY_DELETE, 0, false); return;
            case 4: sendKeyEvent(VKEY_END, 0, false); return;
            case 5: // Page Up
                sendKeyEvent(0x21, 0, false);
                return;
            case 6: // Page Down
                sendKeyEvent(0x22, 0, false);
                return;
        }
    }
}

void KeyboardHandler::sendKeyEvent(int key_code, char character, bool is_char_event) {
    if (!browser_ || !browser_->GetHost()) {
        return;
    }
    
    FILE* log = fopen("/tmp/brow6el_debug.log", "a");
    if (log) {
        fprintf(log, "Sending key: code=%d char='%c' (%02x) is_char=%d\n",
                key_code, character ? character : '?', (unsigned char)character, is_char_event);
        fclose(log);
    }
    
    CefKeyEvent key_event;
    
    if (is_char_event) {
        // CHAR event for character input
        key_event.type = KEYEVENT_CHAR;
        key_event.windows_key_code = key_code;
        key_event.native_key_code = key_code;
        key_event.character = character;
        key_event.unmodified_character = character;
        key_event.modifiers = 0;
        key_event.is_system_key = 0;
        key_event.focus_on_editable_field = 1;
        
        browser_->GetHost()->SendKeyEvent(key_event);
    } else {
        // KEYDOWN event
        key_event.type = KEYEVENT_RAWKEYDOWN;
        key_event.windows_key_code = key_code;
        key_event.native_key_code = key_code;
        key_event.character = 0;
        key_event.unmodified_character = 0;
        key_event.modifiers = 0;
        key_event.is_system_key = 0;
        key_event.focus_on_editable_field = 1;
        
        browser_->GetHost()->SendKeyEvent(key_event);
        
        // KEYUP event
        key_event.type = KEYEVENT_KEYUP;
        browser_->GetHost()->SendKeyEvent(key_event);
        
        // For printable keys, also send CHAR event
        if (character != 0) {
            key_event.type = KEYEVENT_CHAR;
            key_event.character = character;
            key_event.unmodified_character = character;
            browser_->GetHost()->SendKeyEvent(key_event);
        }
    }
}
