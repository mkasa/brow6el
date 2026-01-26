#include "status_bar.h"
#include "kitty_renderer.h"
#include "profile_config.h"
#include "sixel_renderer.h"
#include <algorithm>
#include <iostream>
#include <sys/ioctl.h>
#include <unistd.h>
#include <wchar.h>
#include <cstring>

// Helper to calculate visual display width of a UTF-8 string
static int getVisualWidth(const std::string &str) {
  // Convert UTF-8 to wide string
  size_t len = mbstowcs(nullptr, str.c_str(), 0);
  if (len == (size_t)-1) {
    // Invalid UTF-8, fall back to byte length
    return str.length();
  }
  
  wchar_t* wstr = new wchar_t[len + 1];
  mbstowcs(wstr, str.c_str(), len + 1);
  
  // Calculate visual width
  int width = wcswidth(wstr, len);
  delete[] wstr;
  
  // If wcswidth fails (returns -1), fall back to byte length
  return (width >= 0) ? width : str.length();
}

// Helper to prepare for showing dialogs (crop renderer if using Kitty)
static void prepareDialogArea(int dialog_rows) {
  ProfileConfig &config = ProfileConfig::getInstance();
  if (config.getGraphicsProtocol() == "kitty") {
    // Get renderer instance and crop it to exclude dialog area
    KittyRenderer* renderer = KittyRenderer::getGlobalInstance();
    if (renderer) {
      renderer->renderCropped(dialog_rows);
    }
  }
}

// Legacy prepareDialogArea without parameter - just delete images
static void prepareDialogArea() {
  ProfileConfig &config = ProfileConfig::getInstance();
  if (config.getGraphicsProtocol() == "kitty") {
    // Just delete all images
    printf("\033_Ga=d,d=a;\033\\");
    fflush(stdout);
  }
}

// Helper to get KittyRenderer instance for cropping
static KittyRenderer* getKittyRenderer() {
  // We need access to the renderer - will be set by browser_client
  static KittyRenderer* renderer = nullptr;
  return renderer;
}

// Helper to crop and re-render for dialogs
static void cropKittyImageForDialog(int start_row, int num_rows) {
  ProfileConfig &config = ProfileConfig::getInstance();
  if (config.getGraphicsProtocol() != "kitty") {
    return; // Not using Kitty
  }
  
  // Delete all images
  printf("\033_Ga=d,d=a;\033\\");
  fflush(stdout);
  
  // Note: Cropped re-render would require access to renderer instance
  // For now, just delete - browser will re-render on dialog close
}


StatusBar::StatusBar() : is_showing_(false), current_selected_(0) {}

StatusBar::~StatusBar() { clear(); }

void StatusBar::saveCursorPosition() { std::cout << "\033[s" << std::flush; }

void StatusBar::restoreCursorPosition() { std::cout << "\033[u" << std::flush; }

void StatusBar::clearStatusArea() {
  // Get terminal size
  struct winsize w;
  ioctl(STDOUT_FILENO, TIOCGWINSZ, &w);
  int rows = w.ws_row;

  // Clear bottom half of screen (where dialogs appear) more aggressively for Kitty
  ProfileConfig &config = ProfileConfig::getInstance();
  if (config.getGraphicsProtocol() == "kitty") {
    // Clear entire screen to remove all dialog text
    std::cout << "\033[2J";
  } else {
    // For sixel, just clear bottom half
    int clear_start = rows / 2;
    for (int i = clear_start; i <= rows; i++) {
      std::cout << "\033[" << i << ";1H\033[2K"; // Move to line and clear entire line
    }
  }
  std::cout << std::flush;
}

void StatusBar::clear(bool redraw_title) {
  // For Kitty: clear entire screen to remove dialog text
  ProfileConfig &config = ProfileConfig::getInstance();
  if (config.getGraphicsProtocol() == "kitty") {
    std::cout << "\033[2J\033[H" << std::flush;
    // Delete all images
    printf("\033_Ga=d,d=a;\033\\");
    fflush(stdout);
    
    // Small delay to ensure terminal processes clear before re-rendering
    usleep(20000); // 20ms
    
    // Force immediate re-render of full frame to cover dialog text
    KittyRenderer* renderer = KittyRenderer::getGlobalInstance();
    if (renderer && !renderer->getPrevBuffer().empty()) {
      // Re-render full frame (0 = no cropping)
      renderer->renderCropped(0);
    }
  } else {
    clearStatusArea();
  }
  
  is_showing_ = false;
  current_options_.clear();
  // Don't clear current_title_ or current_mode_prefix_ - they should persist

  // Request a full redraw on next paint to fill in the area where status bar
  // was
  redraw_requested_ = true;

  // Immediately redraw the title bar if we have one (unless explicitly
  // disabled)
  if (redraw_title && !current_title_.empty()) {
    showTitle(current_title_, current_mode_prefix_.empty()
                                  ? nullptr
                                  : current_mode_prefix_.c_str());
  }
}

void StatusBar::showTitle(const std::string &title, const char *mode_prefix) {
  if (shutdown_mode_)
    return; // Don't update during shutdown

  std::lock_guard<std::mutex> lock(SixelRenderer::getTerminalMutex());
  
  // Don't overwrite active dialogs - they take priority over title updates
  if (is_showing_) {
    // Still update the cached title so it's available when dialog closes
    current_title_ = title;
    if (mode_prefix) {
      current_mode_prefix_ = mode_prefix;
    }
    return;
  }

  current_title_ = title;
  if (mode_prefix) {
    current_mode_prefix_ = mode_prefix;
  }

  saveCursorPosition();

  struct winsize w;
  ioctl(STDOUT_FILENO, TIOCGWINSZ, &w);
  int rows = w.ws_row;
  int cols = w.ws_col;

  // Build the display string with mode prefix on the right
  std::string mode_str;
  if (mode_prefix) {
    mode_str = "[";
    mode_str += mode_prefix;
    mode_str += "]";
  }

  // Calculate available space for title using visual width
  int mode_width = getVisualWidth(mode_str);
  int max_title_width = cols - mode_width - 5; // 5 for padding
  if (max_title_width < 10)
    max_title_width = 10;

  // Truncate title if it's too long (based on visual width)
  std::string display_title = title;
  int title_width = getVisualWidth(display_title);
  if (title_width > max_title_width) {
    // Truncate by removing characters until we fit
    // Simple approach: remove characters from end until width fits
    while (title_width > max_title_width - 3 && !display_title.empty()) {
      // Remove last character (handle UTF-8 multi-byte)
      size_t len = display_title.length();
      if (len > 0) {
        // Find start of last UTF-8 character
        size_t pos = len - 1;
        while (pos > 0 && (display_title[pos] & 0xC0) == 0x80) {
          pos--;
        }
        display_title.erase(pos);
        title_width = getVisualWidth(display_title);
      }
    }
    display_title += "...";
    title_width = getVisualWidth(display_title);
  }

  // Calculate padding to right-align mode using visual widths
  int padding = cols - title_width - mode_width - 2; // 2 for spaces
  if (padding < 1)
    padding = 1;

  // Move to bottom line
  // Only clear line above and force single-width for real xterm (has double-height rendering issues)
  // Don't apply to kitty/wezterm which report as xterm-256color but don't need it
  const char* term = getenv("TERM");
  const char* term_program = getenv("TERM_PROGRAM");
  bool is_real_xterm = (term && strstr(term, "xterm") != NULL) &&
                       (!term_program || 
                        (strstr(term_program, "WezTerm") == NULL &&
                         strstr(term_program, "kitty") == NULL &&
                         strstr(term_program, "ghostty") == NULL));
  
  if (is_real_xterm) {
    std::cout << "\033[" << (rows - 1) << ";1H";
    std::cout << "\033#5"; // Force single-width on line above
    std::cout << "\033[2K"; // Clear line above
  }
  
  std::cout << "\033[" << rows << ";1H";
  if (is_real_xterm) {
    std::cout << "\033#5"; // Force single-width line (DECSWL) - xterm only
  }
  std::cout << "\033[2K"; // Clear status line
  std::cout << "\033[" << rows << ";1H"; // Reposition
  std::cout << "\033[44m\033[97m"; // Blue background, white text
  std::cout << " " << display_title;
  std::cout << std::string(padding, ' '); // Padding
  std::cout << mode_str << " ";
  std::cout << "\033[K";  // Clear to end of line (with current background)
  std::cout << "\033[0m"; // Reset colors
  std::cout << std::flush;

  restoreCursorPosition();
}

void StatusBar::showMessage(const std::string &message) {
  std::lock_guard<std::mutex> lock(SixelRenderer::getTerminalMutex());

  saveCursorPosition();

  struct winsize w;
  ioctl(STDOUT_FILENO, TIOCGWINSZ, &w);
  int rows = w.ws_row;

  // Move to bottom line
  std::cout << "\033[" << rows << ";1H";
  std::cout << "\033[44m\033[97m"; // Blue background, white text
  std::cout << " " << message;
  std::cout << "\033[K";  // Clear to end of line
  std::cout << "\033[0m"; // Reset colors
  std::cout << std::flush;

  restoreCursorPosition();
}

void StatusBar::showURLInput(const std::string &current_url) {
  std::lock_guard<std::mutex> lock(SixelRenderer::getTerminalMutex());

  saveCursorPosition();
  // No prepareDialogArea - single line doesn't need cropping

  struct winsize w;
  ioctl(STDOUT_FILENO, TIOCGWINSZ, &w);
  int rows = w.ws_row;

  // Move to bottom line
  std::cout << "\033[" << rows << ";1H";
  std::cout << "\033[44m\033[97m"; // Blue background, white text
  std::cout << " URL: " << current_url;
  std::cout << "\033[K";  // Clear to end of line
  std::cout << "\033[0m"; // Reset colors
  std::cout << std::flush;

  restoreCursorPosition();
}

void StatusBar::showSearchInput(const std::string &query, int current, int total) {
  std::lock_guard<std::mutex> lock(SixelRenderer::getTerminalMutex());

  saveCursorPosition();

  struct winsize w;
  ioctl(STDOUT_FILENO, TIOCGWINSZ, &w);
  int rows = w.ws_row;

  // Move to bottom line
  std::cout << "\033[" << rows << ";1H";
  std::cout << "\033[44m\033[97m"; // Blue background, white text
  std::cout << " Find: " << query;
  
  // Show match count if available
  if (total > 0) {
    std::cout << " [" << current << "/" << total << "]";
  }
  
  std::cout << "\033[K";  // Clear to end of line
  std::cout << "\033[0m"; // Reset colors
  std::cout << std::flush;

  restoreCursorPosition();
}

void StatusBar::showFileInput(const std::string &default_path) {
  std::lock_guard<std::mutex> lock(SixelRenderer::getTerminalMutex());

  saveCursorPosition();
  // No prepareDialogArea - single line doesn't need cropping

  struct winsize w;
  ioctl(STDOUT_FILENO, TIOCGWINSZ, &w);
  int rows = w.ws_row;

  // Move to bottom line
  std::cout << "\033[" << rows << ";1H";
  std::cout << "\033[44m\033[97m"; // Blue background, white text
  std::cout << " File: " << default_path;
  std::cout << "\033[K";  // Clear to end of line
  std::cout << "\033[0m"; // Reset colors
  std::cout << std::flush;

  restoreCursorPosition();
}

void StatusBar::showAuthDialog(const std::string &input_display,
                                const std::string &realm) {
  std::lock_guard<std::mutex> lock(SixelRenderer::getTerminalMutex());

  saveCursorPosition();
  
  struct winsize w;
  ioctl(STDOUT_FILENO, TIOCGWINSZ, &w);
  int rows = w.ws_row;

  // Use bottom 4 lines for auth dialog
  int dialog_rows = 4;
  prepareDialogArea(dialog_rows);
  
  int start_line = rows - 3;
  
  // Clear the dialog area
  for (int i = start_line; i <= rows; i++) {
    std::cout << "\033[" << i << ";1H\033[2K";
  }

  // Draw header
  std::cout << "\033[" << start_line << ";1H";
  std::cout << "\033[44m\033[97m\033[1m"; // Blue background, white bold text
  std::cout << " 🔒 Authentication Required";
  std::cout << "\033[K\033[0m\n";

  // Show realm
  std::cout << "\033[40m\033[97m"; // Black background, white text
  std::cout << " Realm: " << realm;
  std::cout << "\033[K\033[0m\n";

  // Input line with current field and value
  std::cout << "\033[40m\033[97m"; // Black background, white text
  std::cout << " " << input_display;
  std::cout << "\033[K\033[0m\n";
  
  // Instructions
  std::cout << "\033[40m\033[90m"; // Dark gray text
  std::cout << " (Enter to move to password, Esc to cancel)";
  std::cout << "\033[K\033[0m";
  std::cout << std::flush;

  restoreCursorPosition();
}

void StatusBar::showComboboxOptions(const std::vector<std::string> &options,
                                    int selected_index) {
  if (options.empty()) {
    return;
  }

  std::lock_guard<std::mutex> lock(SixelRenderer::getTerminalMutex());

  // Save state for redrawing
  is_showing_ = true;
  current_options_ = options;
  current_selected_ = selected_index;

  saveCursorPosition();

  struct winsize w;
  ioctl(STDOUT_FILENO, TIOCGWINSZ, &w);
  int rows = w.ws_row;
  int cols = w.ws_col;

  // Calculate how many options to show (max 8 lines)
  int max_display = std::min(8, (int)options.size());
  
  // Calculate dialog height: header (1) + options (max_display) + scroll indicators (1 if needed)
  int dialog_rows = max_display + 1; // header + options
  if (options.size() > max_display) {
    dialog_rows++; // add line for scroll indicators
  }
  
  // Crop renderer for Kitty protocol
  prepareDialogArea(dialog_rows);
  
  int start_line = rows - max_display - 1;

  // Clear the entire options area
  for (int i = start_line; i <= rows; i++) {
    std::cout << "\033[" << i
              << ";1H\033[2K"; // Move to line and clear entire line
  }
  std::cout << std::flush; // Ensure clearing is complete before redrawing

  // Draw header
  std::cout << "\033[" << start_line << ";1H";
  std::cout << "\033[44m\033[97m\033[1m"; // Blue background, white bold text
  std::cout
      << " Select option (↑↓ to navigate, Enter to confirm, Esc to close): ";
  std::cout << "\033[K\033[0m";

  // Determine which options to show (with scrolling)
  int start_idx = 0;
  if (selected_index >= max_display - 1) {
    start_idx = std::min(selected_index - max_display + 2,
                         (int)options.size() - max_display);
  }

  // Draw options with absolute positioning
  int option_line = start_line + 1;
  for (int i = 0; i < max_display && (start_idx + i) < options.size(); i++) {
    int opt_idx = start_idx + i;
    std::string option = options[opt_idx];

    // Truncate if too long
    int max_width = cols - 6;
    if (option.length() > max_width) {
      option = option.substr(0, max_width - 3) + "...";
    }

    // Position at specific line
    std::cout << "\033[" << option_line << ";1H";

    // Highlight selected option
    if (opt_idx == selected_index) {
      std::cout << "\033[42m\033[30m"; // Green background, black text
      std::cout << " ► " << option;
    } else {
      std::cout << "\033[40m\033[97m"; // Black background, white text
      std::cout << "   " << option;
    }
    std::cout << "\033[K\033[0m"; // Clear to end of line and reset
    option_line++;
  }

  // Show scroll indicators if needed (on a separate line after options)
  if (start_idx > 0 || (start_idx + max_display) < options.size()) {
    std::cout << "\033[" << option_line << ";1H"; // Position on next line
    std::cout << "\033[44m\033[97m";              // Blue background
    if (start_idx > 0) {
      std::cout << " ▲ More above";
    }
    if ((start_idx + max_display) < options.size()) {
      if (start_idx > 0)
        std::cout << "  ";
      std::cout << " ▼ More below";
    }
    std::cout << "\033[K\033[0m\n";
  }

  std::cout << std::flush;
  restoreCursorPosition();
}

void StatusBar::showConsole(const std::vector<std::string> &logs,
                            const std::string &input, int scroll_offset) {
  std::lock_guard<std::mutex> lock(SixelRenderer::getTerminalMutex());

  saveCursorPosition();

  struct winsize w;
  ioctl(STDOUT_FILENO, TIOCGWINSZ, &w);
  int rows = w.ws_row;
  int cols = w.ws_col;

  // Use bottom half of screen for console (or at least 10 lines)
  int console_height = std::max(10, rows / 2);
  int start_line = rows - console_height + 1;
  
  // Crop renderer to exclude console area
  prepareDialogArea(console_height);

  // Save cursor position AFTER we know where we'll be drawing
  saveCursorPosition();

  // Clear the entire console area from start_line to bottom of screen
  for (int i = start_line; i <= rows; i++) {
    std::cout << "\033[" << i
              << ";1H\033[2K"; // Move to line and clear entire line
  }

  // Draw header
  std::cout << "\033[" << start_line << ";1H";
  std::cout << "\033[44m\033[97m\033[1m"; // Blue background, white bold text
  std::cout
      << " JavaScript Console (↑↓ scroll, Esc to close, Enter to execute) ";
  std::cout << "\033[K\033[0m";

  // Calculate how many log lines we can show
  int log_display_lines = console_height - 3; // Header + input line + border

  // Determine which logs to show (with scrolling)
  int total_logs = logs.size();
  int start_idx = std::max(0, total_logs - log_display_lines - scroll_offset);
  int end_idx = std::min(total_logs, start_idx + log_display_lines);

  // Draw log messages (starting from line after header)
  int log_line = start_line + 1;
  for (int i = start_idx; i < end_idx; i++) {
    std::string log = logs[i];

    // Truncate if too long
    if (log.length() > cols - 2) {
      log = log.substr(0, cols - 5) + "...";
    }

    std::cout << "\033[" << log_line << ";1H"; // Position at specific line
    std::cout << "\033[40m\033[97m";           // Black background, white text
    std::cout << " " << log;
    std::cout << "\033[K\033[0m"; // Clear rest of line
    log_line++;
  }

  // Fill remaining lines (clear any leftover content)
  int input_line = start_line + console_height - 2;
  while (log_line < input_line) {
    std::cout << "\033[" << log_line << ";1H\033[K";
    log_line++;
  }

  // Show scroll indicators
  if (total_logs > log_display_lines) {
    std::cout << "\033[" << input_line << ";1H";
    std::cout << "\033[44m\033[97m";
    std::cout << " [" << (end_idx - start_idx) << "/" << total_logs
              << " messages]";
    if (start_idx > 0)
      std::cout << " ▲";
    if (end_idx < total_logs)
      std::cout << " ▼";
    std::cout << "\033[K\033[0m";
  }

  // Draw input line
  std::cout << "\033[" << (rows - 1) << ";1H";
  std::cout << "\033[42m\033[30m"; // Green background, black text
  std::cout << " > " << input;
  std::cout << "\033[K\033[0m";

  std::cout << std::flush;
  restoreCursorPosition();
}

void StatusBar::redraw() {
  if (is_showing_ && !current_options_.empty()) {
    showComboboxOptions(current_options_, current_selected_);
  } else if (!current_title_.empty()) {
    // Show title bar when nothing else is active
    showTitle(current_title_, current_mode_prefix_.empty()
                                  ? nullptr
                                  : current_mode_prefix_.c_str());
  }
}

void StatusBar::showPopupConfirm(const std::string &url) {
  std::lock_guard<std::mutex> lock(SixelRenderer::getTerminalMutex());

  saveCursorPosition();

  struct winsize w;
  ioctl(STDOUT_FILENO, TIOCGWINSZ, &w);
  int rows = w.ws_row;
  int cols = w.ws_col;

  // Use 5 lines at bottom
  int dialog_height = 5;
  int start_line = rows - 4;
  
  // Crop renderer
  prepareDialogArea(dialog_height);

  // Clear area
  for (int i = 0; i < 5; i++) {
    std::cout << "\033[" << (start_line + i) << ";1H\033[K";
  }

  // Draw header
  std::cout << "\033[" << start_line << ";1H";
  std::cout << "\033[43m\033[30m\033[1m"; // Yellow background, black bold text
  std::cout << " ⚠ Open in New Window? ";
  std::cout << "\033[K\033[0m\n";

  // Show URL (truncate if needed)
  std::string display_url = url;
  int max_url_len = cols - 4;
  if (display_url.length() > max_url_len) {
    display_url = display_url.substr(0, max_url_len - 3) + "...";
  }

  std::cout << "\033[40m\033[97m"; // Black background, white text
  std::cout << " " << display_url;
  std::cout << "\033[K\033[0m\n";

  // Empty line
  std::cout << "\033[K\n";

  // Show options
  std::cout << "\033[42m\033[30m\033[1m"; // Green background, black bold text
  std::cout << " [y] Yes - Open in current tab ";
  std::cout << "\033[0m ";
  std::cout << "\033[41m\033[97m\033[1m"; // Red background, white bold text
  std::cout << " [n] No - Cancel ";
  std::cout << "\033[K\033[0m";

  std::cout << std::flush;
  restoreCursorPosition();

  is_showing_ = true;
}

void StatusBar::showJSAlert(const std::string &message) {
  std::lock_guard<std::mutex> lock(SixelRenderer::getTerminalMutex());

  saveCursorPosition();

  struct winsize w;
  ioctl(STDOUT_FILENO, TIOCGWINSZ, &w);
  int rows = w.ws_row;
  int cols = w.ws_col;

  // Use 5 lines at bottom
  int dialog_height = 5;
  int start_line = rows - 4;
  
  // Crop renderer
  prepareDialogArea(dialog_height);

  // Clear area
  for (int i = 0; i < 5; i++) {
    std::cout << "\033[" << (start_line + i) << ";1H\033[K";
  }

  // Draw header
  std::cout << "\033[" << start_line << ";1H";
  std::cout << "\033[44m\033[97m\033[1m"; // Blue background, white bold text
  std::cout << " ℹ JavaScript Alert ";
  std::cout << "\033[K\033[0m\n";

  // Show message (truncate if needed)
  std::string display_msg = message;
  int max_msg_len = cols - 4;
  if (display_msg.length() > max_msg_len) {
    display_msg = display_msg.substr(0, max_msg_len - 3) + "...";
  }

  std::cout << "\033[40m\033[97m"; // Black background, white text
  std::cout << " " << display_msg;
  std::cout << "\033[K\033[0m\n";

  // Empty line
  std::cout << "\033[K\n";

  // Show option
  std::cout << "\033[42m\033[30m\033[1m"; // Green background, black bold text
  std::cout << " [Enter] OK ";
  std::cout << "\033[K\033[0m";

  std::cout << std::flush;
  restoreCursorPosition();

  is_showing_ = true;
}

void StatusBar::showJSConfirm(const std::string &message) {
  std::lock_guard<std::mutex> lock(SixelRenderer::getTerminalMutex());

  saveCursorPosition();

  struct winsize w;
  ioctl(STDOUT_FILENO, TIOCGWINSZ, &w);
  int rows = w.ws_row;
  int cols = w.ws_col;

  // Use 5 lines at bottom
  int dialog_height = 5;
  int start_line = rows - 4;
  
  // Crop renderer
  prepareDialogArea(dialog_height);

  // Clear area
  for (int i = 0; i < 5; i++) {
    std::cout << "\033[" << (start_line + i) << ";1H\033[K";
  }

  // Draw header
  std::cout << "\033[" << start_line << ";1H";
  std::cout << "\033[43m\033[30m\033[1m"; // Yellow background, black bold text
  std::cout << " ⚠ JavaScript Confirm ";
  std::cout << "\033[K\033[0m\n";

  // Show message (truncate if needed)
  std::string display_msg = message;
  int max_msg_len = cols - 4;
  if (display_msg.length() > max_msg_len) {
    display_msg = display_msg.substr(0, max_msg_len - 3) + "...";
  }

  std::cout << "\033[40m\033[97m"; // Black background, white text
  std::cout << " " << display_msg;
  std::cout << "\033[K\033[0m\n";

  // Empty line
  std::cout << "\033[K\n";

  // Show options
  std::cout << "\033[42m\033[30m\033[1m"; // Green background, black bold text
  std::cout << " [y] OK ";
  std::cout << "\033[0m ";
  std::cout << "\033[41m\033[97m\033[1m"; // Red background, white bold text
  std::cout << " [n] Cancel ";
  std::cout << "\033[K\033[0m";

  std::cout << std::flush;
  restoreCursorPosition();

  is_showing_ = true;
}

void StatusBar::showJSPrompt(const std::string &message,
                             const std::string &default_value) {
  std::lock_guard<std::mutex> lock(SixelRenderer::getTerminalMutex());

  saveCursorPosition();

  struct winsize w;
  ioctl(STDOUT_FILENO, TIOCGWINSZ, &w);
  int rows = w.ws_row;
  int cols = w.ws_col;

  // Use 5 lines at bottom
  int dialog_height = 5;
  int start_line = rows - 4;
  
  // Crop renderer
  prepareDialogArea(dialog_height);

  // Clear area
  for (int i = 0; i < 5; i++) {
    std::cout << "\033[" << (start_line + i) << ";1H\033[K";
  }

  // Draw header
  std::cout << "\033[" << start_line << ";1H";
  std::cout << "\033[45m\033[97m\033[1m"; // Magenta background, white bold text
  std::cout << " ✎ JavaScript Prompt ";
  std::cout << "\033[K\033[0m\n";

  // Show message (truncate if needed)
  std::string display_msg = message;
  int max_msg_len = cols - 4;
  if (display_msg.length() > max_msg_len) {
    display_msg = display_msg.substr(0, max_msg_len - 3) + "...";
  }

  std::cout << "\033[40m\033[97m"; // Black background, white text
  std::cout << " " << display_msg;
  std::cout << "\033[K\033[0m\n";

  // Show input line with default value
  std::cout << "\033[40m\033[93m"; // Black background, yellow text
  std::cout << " > " << default_value;
  std::cout << "\033[K\033[0m\n";

  // Show help
  std::cout << "\033[40m\033[90m"; // Black background, gray text
  std::cout << " [Enter] OK  [ESC] Cancel";
  std::cout << "\033[K\033[0m";

  std::cout << std::flush;
  restoreCursorPosition();

  is_showing_ = true;
}

void StatusBar::showDownloadConfirm(const std::string &filename,
                                    const std::string &url) {
  std::lock_guard<std::mutex> lock(SixelRenderer::getTerminalMutex());

  saveCursorPosition();

  struct winsize w;
  ioctl(STDOUT_FILENO, TIOCGWINSZ, &w);
  int rows = w.ws_row;
  int cols = w.ws_col;

  // Use 6 lines at bottom
  int dialog_height = 6;
  int start_line = rows - 5;
  
  // Crop renderer
  prepareDialogArea(dialog_height);

  // Clear area
  for (int i = 0; i < 6; i++) {
    std::cout << "\033[" << (start_line + i) << ";1H\033[K";
  }

  // Draw header
  std::cout << "\033[" << start_line << ";1H";
  std::cout << "\033[46m\033[30m\033[1m"; // Cyan background, black bold text
  std::cout << " ⬇ Download File? ";
  std::cout << "\033[K\033[0m\n";

  // Show filename (truncate if needed)
  std::string display_name = filename;
  int max_name_len = cols - 4;
  if (display_name.length() > max_name_len) {
    display_name = display_name.substr(0, max_name_len - 3) + "...";
  }

  std::cout << "\033[40m\033[97m"; // Black background, white text
  std::cout << " File: " << display_name;
  std::cout << "\033[K\033[0m\n";

  // Show URL (truncate if needed)
  std::string display_url = url;
  if (display_url.length() > max_name_len) {
    display_url = display_url.substr(0, max_name_len - 3) + "...";
  }

  std::cout << "\033[40m\033[90m"; // Black background, gray text
  std::cout << " From: " << display_url;
  std::cout << "\033[K\033[0m\n";

  // Empty line
  std::cout << "\033[K\n";

  // Show options
  std::cout << "\033[42m\033[30m\033[1m"; // Green background, black bold text
  std::cout << " [y] Download to ~/Downloads ";
  std::cout << "\033[0m ";
  std::cout << "\033[41m\033[97m\033[1m"; // Red background, white bold text
  std::cout << " [n] Cancel ";
  std::cout << "\033[K\033[0m";

  std::cout << std::flush;
  restoreCursorPosition();

  is_showing_ = true;
}

void StatusBar::showBookmarks(const std::vector<std::string> &bookmarks,
                              int selected_index) {
  if (bookmarks.empty()) {
    std::lock_guard<std::mutex> lock(SixelRenderer::getTerminalMutex());

    saveCursorPosition();
    prepareDialogArea();

    struct winsize w;
    ioctl(STDOUT_FILENO, TIOCGWINSZ, &w);
    int rows = w.ws_row;

    // Clear status area
    clearStatusArea();

    std::cout << "\033[" << (rows - 2) << ";1H";
    std::cout << "\033[44m\033[97m\033[1m"; // Blue background, white bold text
    std::cout << " 📚 Bookmarks ";
    std::cout << "\033[K\033[0m\n";

    std::cout << "\033[40m\033[97m"; // Black background, white text
    std::cout << " No bookmarks yet. Press 'd' to bookmark current page.";
    std::cout << "\033[K\033[0m";

    std::cout << std::flush;
    restoreCursorPosition();

    is_showing_ = true;
    return;
  }

  std::lock_guard<std::mutex> lock(SixelRenderer::getTerminalMutex());

  // Save state for redrawing
  is_showing_ = true;
  current_options_ = bookmarks;
  current_selected_ = selected_index;

  saveCursorPosition();

  struct winsize w;
  ioctl(STDOUT_FILENO, TIOCGWINSZ, &w);
  int rows = w.ws_row;
  int cols = w.ws_col;

  // Use bottom half of screen for bookmarks (or at least 10 lines), same as console
  int dialog_height = std::max(10, rows / 2);
  int start_line = rows - dialog_height + 1;
  
  // Crop renderer to exclude dialog area
  prepareDialogArea(dialog_height);
  
  // Calculate how many bookmarks to show
  int max_display = dialog_height - 1; // Minus header line

  // Clear the entire dialog area from start_line to bottom of screen
  for (int i = start_line; i <= rows; i++) {
    std::cout << "\033[" << i << ";1H\033[2K"; // Move to line and clear entire line
  }

  // Draw header
  std::cout << "\033[" << start_line << ";1H";
  std::cout << "\033[44m\033[97m\033[1m"; // Blue background, white bold text
  std::cout
      << " 📚 Bookmarks (↑↓ navigate, Enter open, d delete, b/Esc close) ";
  std::cout << "\033[K\033[0m" << std::flush;
  std::cout << "\n";

  // Determine which bookmarks to show (with scrolling)
  int start_idx = 0;
  if (selected_index >= max_display - 1) {
    start_idx = std::min(selected_index - max_display + 2,
                         (int)bookmarks.size() - max_display);
  }

  // Draw bookmarks
  for (int i = 0; i < max_display && (start_idx + i) < bookmarks.size(); i++) {
    int opt_idx = start_idx + i;
    std::string bookmark = bookmarks[opt_idx];

    // Truncate if too long
    int max_width = cols - 6;
    if (bookmark.length() > max_width) {
      bookmark = bookmark.substr(0, max_width - 3) + "...";
    }

    // Highlight selected bookmark
    if (opt_idx == selected_index) {
      std::cout << "\033[42m\033[30m"; // Green background, black text
      std::cout << " ► " << bookmark;
    } else {
      std::cout << "\033[40m\033[97m"; // Black background, white text
      std::cout << "   " << bookmark;
    }
    std::cout << "\033[K\033[0m" << std::flush;
    std::cout << "\n"; // Clear to end of line and reset
  }

  // Show scroll indicators if needed
  if (start_idx > 0 || (start_idx + max_display) < bookmarks.size()) {
    std::cout << "\033[44m\033[97m"; // Blue background
    if (start_idx > 0) {
      std::cout << " ▲ More above";
    }
    if ((start_idx + max_display) < bookmarks.size()) {
      if (start_idx > 0)
        std::cout << "  ";
      std::cout << " ▼ More below";
    }
    std::cout << "\033[K\033[0m\n";
  }

  std::cout << std::flush;
  restoreCursorPosition();
}

void StatusBar::showUserScripts(const std::vector<std::string> &scripts,
                                int selected_index) {
  if (scripts.empty()) {
    std::lock_guard<std::mutex> lock(SixelRenderer::getTerminalMutex());

    saveCursorPosition();
    prepareDialogArea();

    struct winsize w;
    ioctl(STDOUT_FILENO, TIOCGWINSZ, &w);
    int rows = w.ws_row;

    // Clear status area
    clearStatusArea();

    std::cout << "\033[" << (rows - 2) << ";1H";
    std::cout << "\033[44m\033[97m\033[1m"; // Blue background, white bold text
    std::cout << " 📜 User Scripts ";
    std::cout << "\033[K\033[0m\n";

    std::cout << "\033[40m\033[97m"; // Black background, white text
    std::cout << " No scripts found in ~/.brow6el_userscripts/";
    std::cout << "\033[K\033[0m";

    std::cout << std::flush;
    restoreCursorPosition();

    is_showing_ = true;
    return;
  }

  std::lock_guard<std::mutex> lock(SixelRenderer::getTerminalMutex());

  // Save state for redrawing
  is_showing_ = true;
  current_options_ = scripts;
  current_selected_ = selected_index;

  saveCursorPosition();

  struct winsize w;
  ioctl(STDOUT_FILENO, TIOCGWINSZ, &w);
  int rows = w.ws_row;
  int cols = w.ws_col;

  // Use bottom half of screen for scripts (or at least 10 lines), same as console
  int dialog_height = std::max(10, rows / 2);
  int start_line = rows - dialog_height + 1;
  
  // Crop renderer
  prepareDialogArea(dialog_height);
  
  // Calculate how many scripts to show
  int max_display = dialog_height - 1; // Minus header line

  // Clear the entire dialog area from start_line to bottom of screen
  for (int i = start_line; i <= rows; i++) {
    std::cout << "\033[" << i << ";1H\033[2K"; // Move to line and clear entire line
  }

  // Draw header
  std::cout << "\033[" << start_line << ";1H";
  std::cout << "\033[44m\033[97m\033[1m"; // Blue background, white bold text
  std::cout << " 📜 User Scripts (↑↓ navigate, Enter inject, Esc close) ";
  std::cout << "\033[K\033[0m\n";

  // Determine which scripts to show (with scrolling)
  int start_idx = 0;
  if (selected_index >= max_display - 1) {
    start_idx = std::min(selected_index - max_display + 2,
                         (int)scripts.size() - max_display);
  }

  // Draw scripts
  for (int i = 0; i < max_display && (start_idx + i) < scripts.size(); i++) {
    int opt_idx = start_idx + i;
    std::string script_name = scripts[opt_idx];

    // Truncate if too long
    int max_width = cols - 6;
    if (script_name.length() > max_width) {
      script_name = script_name.substr(0, max_width - 3) + "...";
    }

    // Highlight selected script
    if (opt_idx == selected_index) {
      std::cout << "\033[42m\033[30m"; // Green background, black text
      std::cout << " ► " << script_name;
    } else {
      std::cout << "\033[40m\033[97m"; // Black background, white text
      std::cout << "   " << script_name;
    }
    std::cout << "\033[K\033[0m\n"; // Clear to end of line and reset
  }

  // Show scroll indicators if needed
  if (start_idx > 0 || (start_idx + max_display) < scripts.size()) {
    std::cout << "\033[44m\033[97m"; // Blue background
    if (start_idx > 0) {
      std::cout << " ▲ More above";
    }
    if ((start_idx + max_display) < scripts.size()) {
      if (start_idx > 0)
        std::cout << "  ";
      std::cout << " ▼ More below";
    }
    std::cout << "\033[K\033[0m\n";
  }

  std::cout << std::flush;
  restoreCursorPosition();
}

void StatusBar::showHintInput(const std::string &input, int hint_count) {
  std::lock_guard<std::mutex> lock(SixelRenderer::getTerminalMutex());

  is_showing_ = true;
  // Don't set current_title_ - we don't want "Hint Mode" to persist after ESC

  saveCursorPosition();

  struct winsize w;
  ioctl(STDOUT_FILENO, TIOCGWINSZ, &w);
  int rows = w.ws_row;

  // Move to bottom line
  std::cout << "\033[" << rows << ";1H";
  std::cout << "\033[43m\033[30m"; // Yellow background, black text
  std::cout << " Hint: " << input;
  std::cout << "\033[K";  // Clear to end of line
  std::cout << "\033[0m"; // Reset colors
  std::cout << std::flush;

  restoreCursorPosition();
}

void StatusBar::showDownloadManager(const std::vector<std::string> &downloads,
                                    int selected_index) {
  if (downloads.empty()) {
    std::lock_guard<std::mutex> lock(SixelRenderer::getTerminalMutex());
    saveCursorPosition();
    
    struct winsize w;
    ioctl(STDOUT_FILENO, TIOCGWINSZ, &w);
    int rows = w.ws_row;

    // Use same height calculation as non-empty case
    int dialog_height = std::max(10, rows / 2);
    int start_line = rows - dialog_height + 1;
    
    // Crop renderer
    prepareDialogArea(dialog_height);

    // Clear the entire dialog area from start_line to bottom of screen
    for (int i = start_line; i <= rows; i++) {
      std::cout << "\033[" << i << ";1H\033[2K"; // Move to line and clear entire line
    }

    std::cout
        << "\033[" << start_line
        << ";1H\033[44m\033[97m\033[1m 📥 Download Manager \033[K\033[0m\n";
    std::cout << "\033[40m\033[97m No downloads yet.\033[K\033[0m"
              << std::flush;
    restoreCursorPosition();
    is_showing_ = true;
    return;
  }

  std::lock_guard<std::mutex> lock(SixelRenderer::getTerminalMutex());
  is_showing_ = true;
  current_options_ = downloads;
  current_selected_ = selected_index;
  saveCursorPosition();
  
  struct winsize w;
  ioctl(STDOUT_FILENO, TIOCGWINSZ, &w);
  int rows = w.ws_row;
  int cols = w.ws_col;

  // Use bottom half of screen for downloads (or at least 10 lines), same as console
  int dialog_height = std::max(10, rows / 2);
  int start_line = rows - dialog_height + 1;
  
  // Crop renderer
  prepareDialogArea(dialog_height);
  
  // Calculate how many downloads to show
  int max_display = dialog_height - 1; // Minus header line

  // Clear the entire dialog area from start_line to bottom of screen
  for (int i = start_line; i <= rows; i++) {
    std::cout << "\033[" << i << ";1H\033[2K"; // Move to line and clear entire line
  }

  std::cout << "\033[" << start_line
            << ";1H\033[44m\033[97m\033[1m 📥 Download Manager (↑↓ navigate, x "
               "cancel, c clear, m/Esc close) \033[K\033[0m\n";

  int start_idx = 0;
  if (selected_index >= max_display - 1) {
    start_idx = std::min(selected_index - max_display + 2,
                         (int)downloads.size() - max_display);
  }
  for (int i = 0; i < max_display && (start_idx + i) < downloads.size(); i++) {
    int opt_idx = start_idx + i;
    std::string download = downloads[opt_idx];
    int max_width = cols - 6;
    if (download.length() > max_width) {
      download = download.substr(0, max_width - 3) + "...";
    }
    if (opt_idx == selected_index) {
      std::cout << "\033[42m\033[30m ► " << download << "\033[K\033[0m\n";
    } else {
      std::cout << "\033[40m\033[97m   " << download << "\033[K\033[0m\n";
    }
  }
  std::cout << std::flush;
  restoreCursorPosition();
}
