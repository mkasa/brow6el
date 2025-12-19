#pragma once

#include <string>

struct TerminalInfo {
    int width;
    int height;
    int cell_width;
    int cell_height;
    bool supports_sixel;
};

class TerminalDetector {
public:
    static TerminalInfo detect();
    
private:
    static bool checkSixelSupport();
    static void querySixelGeometry(int& width, int& height);
    static void getTerminalSize(int& cols, int& rows);
};
