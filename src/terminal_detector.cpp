#include "terminal_detector.h"
#include <cstdio>
#include <cstring>
#include <iostream>
#include <sys/ioctl.h>
#include <termios.h>
#include <unistd.h>

TerminalInfo TerminalDetector::detect() {
  TerminalInfo info = {0};

  info.supports_sixel = checkSixelSupport();
  info.supports_kitty = checkKittySupport();

  if (info.supports_sixel || info.supports_kitty) {
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
  const char *term = getenv("TERM");
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
  tv.tv_usec = 200000; // 200ms timeout

  bool supported = false;
  ssize_t got = 0;
  while (select(STDIN_FILENO + 1, &fds, NULL, NULL, &tv) > 0) {
    ssize_t n = read(STDIN_FILENO, &response[got], sizeof(response) - got - 1);
    if (n < 0) { break; }
    got += n;
    if (got > 0) {
      // Check for Sixel support in device attributes
      // DA1 response with ";4;" or ";4c" indicates Sixel support
      supported =
          (strstr(response, ";4;") != NULL || strstr(response, ";4c") != NULL);
    }
  }

  tcsetattr(STDIN_FILENO, TCSANOW, &old_tio);

  // Clear any remaining input
  tcflush(STDIN_FILENO, TCIFLUSH);

  return supported;
}

bool TerminalDetector::checkKittySupport() {
  // Check if stdin is actually a terminal
  if (!isatty(STDIN_FILENO)) {
    return false;
  }

  // Check TERM environment variable for known kitty-capable terminals
  const char *term = getenv("TERM");
  if (term) {
    std::string term_str(term);
    // Known terminals that support kitty graphics protocol
    if (term_str.find("kitty") != std::string::npos ||
        term_str.find("xterm-kitty") != std::string::npos) {
      return true;
    }
  }

  // Check TERM_PROGRAM for ghostty and wezterm
  const char *term_program = getenv("TERM_PROGRAM");
  if (term_program) {
    std::string program(term_program);
    if (program.find("ghostty") != std::string::npos ||
        program.find("WezTerm") != std::string::npos) {
      return true;
    }
  }

  // Try to query kitty graphics protocol support
  struct termios old_tio, new_tio;
  if (tcgetattr(STDIN_FILENO, &old_tio) != 0) {
    return false;
  }

  new_tio = old_tio;
  new_tio.c_lflag &= ~(ICANON | ECHO);
  tcsetattr(STDIN_FILENO, TCSANOW, &new_tio);

  // Send a query action with a small 1x1 image to test support
  printf("\033_Gi=31,s=1,v=1,a=q,t=d,f=24;AAAA\033\\");
  fflush(stdout);

  char response[512] = {0};
  fd_set fds;
  struct timeval tv;
  FD_ZERO(&fds);
  FD_SET(STDIN_FILENO, &fds);
  tv.tv_sec = 0;
  tv.tv_usec = 300000; // 300ms timeout

  bool has_graphics_response = false;
  ssize_t got = 0;

  // Read response with multiple attempts
  for (int attempt = 0; attempt < 3 && !has_graphics_response; attempt++) {
    if (select(STDIN_FILENO + 1, &fds, NULL, NULL, &tv) > 0) {
      ssize_t n = read(STDIN_FILENO, response + got, sizeof(response) - got - 1);
      if (n > 0) {
        got += n;
        // Check for kitty graphics protocol response (OK or error)
        if (strstr(response, "\033_Gi=31") != NULL || 
            strstr(response, "_Gi=31") != NULL) {
          has_graphics_response = true;
        }
      }
    }
    // Reset for next read
    FD_ZERO(&fds);
    FD_SET(STDIN_FILENO, &fds);
    tv.tv_sec = 0;
    tv.tv_usec = 100000;
  }

  tcsetattr(STDIN_FILENO, TCSANOW, &old_tio);
  tcflush(STDIN_FILENO, TCIFLUSH);

  return has_graphics_response;
}

void TerminalDetector::querySixelGeometry(int &width, int &height) {
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
  ssize_t got = 0;
  while (select(STDIN_FILENO + 1, &fds, NULL, NULL, &tv) > 0) {
    ssize_t n = read(STDIN_FILENO, &response[got], sizeof(response) - got - 1);
    if (n < 0) { break; }
    got += n;
    if (got > 0 && sscanf(response, "\033[?2;0;%d;%dS", &width, &height) == 2) {
      if (width > 0 && height > 0) {
        got_response = true;
      }
    }
  }

  // Method 2: Try XTerm window size query (works on Windows Terminal, many
  // others)
  if (!got_response) {
    tcflush(STDIN_FILENO, TCIFLUSH);
    memset(response, 0, sizeof(response));

    printf("\033[14t");
    fflush(stdout);

    FD_ZERO(&fds);
    FD_SET(STDIN_FILENO, &fds);
    tv.tv_sec = 0;
    tv.tv_usec = 100000;

    ssize_t got = 0;
    while (select(STDIN_FILENO + 1, &fds, NULL, NULL, &tv) > 0) {
      ssize_t n = read(STDIN_FILENO, &response[got], sizeof(response) - got - 1);
      if (n < 0) { break; }
      got += n;
      if (got > 0) {
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

void TerminalDetector::getTerminalSize(int &cols, int &rows) {
  struct winsize w;
  if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &w) == 0) {
    cols = w.ws_col;
    rows = w.ws_row;
  } else {
    cols = 80;
    rows = 24;
  }
}
