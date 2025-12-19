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
    const char* term = getenv("TERM");
    
    // Check for known Sixel-capable terminals
    if (term && (strstr(term, "xterm") || strstr(term, "mlterm") || 
                 strstr(term, "yaft") || strstr(term, "sixel") ||
                 strstr(term, "foot") || strstr(term, "wezterm"))) {
        
        // Check if stdin is actually a terminal
        if (!isatty(STDIN_FILENO)) {
            // Not a terminal, but foot/wezterm should support sixel
            return (strstr(term, "foot") != NULL || strstr(term, "wezterm") != NULL);
        }
        
        // Try to query Sixel support
        struct termios old_tio, new_tio;
        if (tcgetattr(STDIN_FILENO, &old_tio) != 0) {
            // Can't get terminal attributes, assume support for known terminals
            return (strstr(term, "foot") != NULL || strstr(term, "wezterm") != NULL);
        }
        
        new_tio = old_tio;
        new_tio.c_lflag &= ~(ICANON | ECHO);
        tcsetattr(STDIN_FILENO, TCSANOW, &new_tio);
        
        // Query device attributes for Sixel support
        printf("\033[c");
        fflush(stdout);
        
        char response[128] = {0};
        fd_set fds;
        struct timeval tv;
        FD_ZERO(&fds);
        FD_SET(STDIN_FILENO, &fds);
        tv.tv_sec = 0;
        tv.tv_usec = 200000;  // Increased timeout
        
        bool supported = false;
        if (select(STDIN_FILENO + 1, &fds, NULL, NULL, &tv) > 0) {
            ssize_t n = read(STDIN_FILENO, response, sizeof(response) - 1);
            if (n > 0) {
                // Check for Sixel support (4 in device attributes)
                // Or if foot/wezterm, assume support
                supported = (strstr(response, ";4;") != NULL || 
                            strstr(response, ";4c") != NULL ||
                            strstr(term, "foot") != NULL ||
                            strstr(term, "wezterm") != NULL);
            }
        } else {
            // No response - for foot and wezterm, assume Sixel support
            if (strstr(term, "foot") != NULL || strstr(term, "wezterm") != NULL) {
                supported = true;
            }
        }
        
        tcsetattr(STDIN_FILENO, TCSANOW, &old_tio);
        
        // Clear any remaining input
        tcflush(STDIN_FILENO, TCIFLUSH);
        
        return supported;
    }
    
    return false;
}

void TerminalDetector::querySixelGeometry(int& width, int& height) {
    struct termios old_tio, new_tio;
    tcgetattr(STDIN_FILENO, &old_tio);
    new_tio = old_tio;
    new_tio.c_lflag &= ~(ICANON | ECHO);
    tcsetattr(STDIN_FILENO, TCSANOW, &new_tio);
    
    printf("\033[?2;1;0S");
    fflush(stdout);
    
    char response[64] = {0};
    fd_set fds;
    struct timeval tv;
    FD_ZERO(&fds);
    FD_SET(STDIN_FILENO, &fds);
    tv.tv_sec = 0;
    tv.tv_usec = 100000;
    
    if (select(STDIN_FILENO + 1, &fds, NULL, NULL, &tv) > 0) {
        read(STDIN_FILENO, response, sizeof(response) - 1);
        sscanf(response, "\033[?2;0;%d;%dS", &width, &height);
    }
    
    tcsetattr(STDIN_FILENO, TCSANOW, &old_tio);
    
    if (width <= 0 || height <= 0) {
        int cols, rows;
        getTerminalSize(cols, rows);
        width = cols * 8;
        height = rows * 16;
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
