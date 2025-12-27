#include "terminal_detector.h"
#include <sys/ioctl.h>
#include <unistd.h>
#include <termios.h>
#include <cstdio>
#include <cstring>
#include <iostream>

TerminalInfo TerminalDetector::detect() {
    TerminalInfo info = {0};
    
    info.supports_sixel = checkSixelSupport();
    
    if (info.supports_sixel) {
        querySixelGeometry(info.width, info.height);
        
        int cols, rows;
        getTerminalSize(cols, rows);
        
        if (cols > 0 && rows > 0 && info.width > 0 && info.height > 0) {
            info.cell_width = info.width / cols;
            info.cell_height = info.height / rows;
            
            // Reserve one row to prevent scrolling after sixel output
            // This prevents the blank line issue in Windows Terminal and others
            info.height = info.height - info.cell_height;
        } else {
            info.cell_width = 8;
            info.cell_height = 16;
            info.width = cols * info.cell_width;
            info.height = rows * info.cell_height;
        }
    }
    
    return info;
}

bool TerminalDetector::checkSixelSupport() {
    // Check if stdin is actually a terminal
    if (!isatty(STDIN_FILENO)) {
        return false;
    }
    
    // Check TERM environment variable for known sixel-capable terminals
    const char* term = getenv("TERM");
    if (term) {
        std::string term_str(term);
        // yaft supports sixel but doesn't report it via DA1
        if (term_str.find("yaft") == 0) {
            return true;
        }
    }
    
    // Try to query Sixel support via device attributes
    struct termios old_tio, new_tio;
    if (tcgetattr(STDIN_FILENO, &old_tio) != 0) {
        return false;
    }
    
    new_tio = old_tio;
    new_tio.c_lflag &= ~(ICANON | ECHO);
    tcsetattr(STDIN_FILENO, TCSANOW, &new_tio);
    
    // Query device attributes (DA1)
    printf("\033[c");
    fflush(stdout);
    
    char response[128] = {0};
    fd_set fds;
    struct timeval tv;
    FD_ZERO(&fds);
    FD_SET(STDIN_FILENO, &fds);
    tv.tv_sec = 0;
    tv.tv_usec = 200000;  // 200ms timeout
    
    bool supported = false;
    if (select(STDIN_FILENO + 1, &fds, NULL, NULL, &tv) > 0) {
        ssize_t n = read(STDIN_FILENO, response, sizeof(response) - 1);
        if (n > 0) {
            // Check for Sixel support in device attributes
            // DA1 response with ";4;" or ";4c" indicates Sixel support
            supported = (strstr(response, ";4;") != NULL || 
                        strstr(response, ";4c") != NULL);
        }
    }
    
    tcsetattr(STDIN_FILENO, TCSANOW, &old_tio);
    
    // Clear any remaining input
    tcflush(STDIN_FILENO, TCIFLUSH);
    
    return supported;
}

void TerminalDetector::querySixelGeometry(int& width, int& height) {
    struct termios old_tio, new_tio;
    tcgetattr(STDIN_FILENO, &old_tio);
    new_tio = old_tio;
    new_tio.c_lflag &= ~(ICANON | ECHO);
    tcsetattr(STDIN_FILENO, TCSANOW, &new_tio);
    
    // Method 1: Try Sixel geometry query (works on mlterm, xterm)
    printf("\033[?2;1;0S");
    fflush(stdout);
    
    char response[128] = {0};
    fd_set fds;
    struct timeval tv;
    FD_ZERO(&fds);
    FD_SET(STDIN_FILENO, &fds);
    tv.tv_sec = 0;
    tv.tv_usec = 100000;
    
    bool got_response = false;
    if (select(STDIN_FILENO + 1, &fds, NULL, NULL, &tv) > 0) {
        ssize_t n = read(STDIN_FILENO, response, sizeof(response) - 1);
        if (n > 0 && sscanf(response, "\033[?2;0;%d;%dS", &width, &height) == 2) {
            if (width > 0 && height > 0) {
                got_response = true;
            }
        }
    }
    
    // Method 2: Try XTerm window size query (works on Windows Terminal, many others)
    if (!got_response) {
        tcflush(STDIN_FILENO, TCIFLUSH);
        memset(response, 0, sizeof(response));
        
        printf("\033[14t");
        fflush(stdout);
        
        FD_ZERO(&fds);
        FD_SET(STDIN_FILENO, &fds);
        tv.tv_sec = 0;
        tv.tv_usec = 100000;
        
        if (select(STDIN_FILENO + 1, &fds, NULL, NULL, &tv) > 0) {
            ssize_t n = read(STDIN_FILENO, response, sizeof(response) - 1);
            if (n > 0) {
                int h = 0, w = 0;
                if (sscanf(response, "\033[4;%d;%dt", &h, &w) == 2) {
                    if (w > 0 && h > 0) {
                        width = w;
                        height = h;
                        got_response = true;
                    }
                }
            }
        }
    }
    
    // Method 3: Try ioctl pixel dimensions (if available)
    if (!got_response) {
        struct winsize w;
        if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &w) == 0) {
            if (w.ws_xpixel > 0 && w.ws_ypixel > 0) {
                width = w.ws_xpixel;
                height = w.ws_ypixel;
                got_response = true;
            }
        }
    }
    
    tcsetattr(STDIN_FILENO, TCSANOW, &old_tio);
    tcflush(STDIN_FILENO, TCIFLUSH);
    
    // Fallback: Use better defaults for modern terminals
    if (!got_response || width <= 0 || height <= 0) {
        int cols, rows;
        getTerminalSize(cols, rows);
        // Modern terminals typically use 10x20 pixel cells (not 8x16)
        width = cols * 10;
        height = rows * 20;
    }
}

void TerminalDetector::getTerminalSize(int& cols, int& rows) {
    struct winsize w;
    if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &w) == 0) {
        cols = w.ws_col;
        rows = w.ws_row;
    } else {
        cols = 80;
        rows = 24;
    }
}
