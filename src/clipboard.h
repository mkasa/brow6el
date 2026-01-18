#pragma once

#include <string>

namespace Clipboard {
// Copy text to system clipboard using OSC 52 escape sequence
void copyToClipboard(const std::string &text);

// Base64 encode for OSC 52
std::string base64_encode(const std::string &input);
} // namespace Clipboard
