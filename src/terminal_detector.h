#pragma once

#include <string>

struct TerminalInfo {
  int width;
  int height;
  int cell_width;
  int cell_height;
  bool supports_sixel;
  bool supports_kitty;
};

class TerminalDetector {
public:
  static TerminalInfo detect();
  // Ask the terminal whether it can read Kitty images from POSIX shared
  // memory (t=s). Only valid before the input handler starts reading stdin.
  static bool checkKittyShmSupport();

private:
  static bool checkSixelSupport();
  static bool checkKittySupport();
  static void querySixelGeometry(int &width, int &height);
  static void getTerminalSize(int &cols, int &rows);
};
