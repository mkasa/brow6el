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

private:
  static bool checkSixelSupport();
  static bool checkKittySupport();
  static void querySixelGeometry(int &width, int &height);
  static void getTerminalSize(int &cols, int &rows);
};
