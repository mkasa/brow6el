#include "clipboard.h"
#include <cstdio>
#include <iostream>

namespace Clipboard {

std::string base64_encode(const std::string &input) {
  static const char *base64_chars =
      "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

  std::string ret;
  int i = 0;
  unsigned char array_3[3];
  unsigned char array_4[4];

  for (char c : input) {
    array_3[i++] = c;
    if (i == 3) {
      array_4[0] = (array_3[0] & 0xfc) >> 2;
      array_4[1] = ((array_3[0] & 0x03) << 4) + ((array_3[1] & 0xf0) >> 4);
      array_4[2] = ((array_3[1] & 0x0f) << 2) + ((array_3[2] & 0xc0) >> 6);
      array_4[3] = array_3[2] & 0x3f;

      for (i = 0; i < 4; i++) {
        ret += base64_chars[array_4[i]];
      }
      i = 0;
    }
  }

  if (i) {
    for (int j = i; j < 3; j++) {
      array_3[j] = '\0';
    }

    array_4[0] = (array_3[0] & 0xfc) >> 2;
    array_4[1] = ((array_3[0] & 0x03) << 4) + ((array_3[1] & 0xf0) >> 4);
    array_4[2] = ((array_3[1] & 0x0f) << 2) + ((array_3[2] & 0xc0) >> 6);

    for (int j = 0; j < i + 1; j++) {
      ret += base64_chars[array_4[j]];
    }
    while (i++ < 3) {
      ret += '=';
    }
  }

  return ret;
}

void copyToClipboard(const std::string &text) {
  if (text.empty()) {
    return;
  }

  // Use OSC 52 escape sequence to copy to clipboard
  // Works in most modern terminals (xterm, kitty, iTerm2, tmux, etc.)
  std::string encoded = base64_encode(text);

  // OSC 52 format: \033]52;c;<base64-text>\007
  // c = clipboard (also supports p=primary, s=secondary)
  std::cout << "\033]52;c;" << encoded << "\007" << std::flush;
}

} // namespace Clipboard
