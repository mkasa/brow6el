#include "status_bar.h"
#include "sixel_renderer.h"
#include <iostream>
#include <algorithm>
#include <sys/ioctl.h>
#include <unistd.h>

StatusBar::StatusBar() : is_showing_(false), current_selected_(0) {
}

StatusBar::~StatusBar() {
    clear();
}

void StatusBar::saveCursorPosition() {
    std::cout << "\033[s" << std::flush;
}

void StatusBar::restoreCursorPosition() {
    std::cout << "\033[u" << std::flush;
}

void StatusBar::clearStatusArea() {
    // Get terminal size
    struct winsize w;
    ioctl(STDOUT_FILENO, TIOCGWINSZ, &w);
    int rows = w.ws_row;
    
    // Move to bottom and clear several lines
    for (int i = 0; i < 10; i++) {
        std::cout << "\033[" << (rows - i) << ";1H\033[K";
    }
    std::cout << std::flush;
}

void StatusBar::clear(bool redraw_title) {
    clearStatusArea();
    is_showing_ = false;
    current_options_.clear();
    // Don't clear current_title_ or current_mode_prefix_ - they should persist
    
    // Immediately redraw the title bar if we have one (unless explicitly disabled)
    if (redraw_title && !current_title_.empty()) {
        showTitle(current_title_, current_mode_prefix_.empty() ? nullptr : current_mode_prefix_.c_str());
    }
}

void StatusBar::showTitle(const std::string& title, const char* mode_prefix) {
    if (shutdown_mode_) return; // Don't update during shutdown
    
    std::lock_guard<std::mutex> lock(SixelRenderer::getTerminalMutex());
    
    current_title_ = title;
    if (mode_prefix) {
        current_mode_prefix_ = mode_prefix;
    }
    
    saveCursorPosition();
    
    struct winsize w;
    ioctl(STDOUT_FILENO, TIOCGWINSZ, &w);
    int rows = w.ws_row;
    int cols = w.ws_col;
    
    // Build the display string with mode prefix if provided
    std::string display_title;
    if (mode_prefix) {
        display_title = "[";
        display_title += mode_prefix;
        display_title += "] ";
    }
    display_title += title;
    
    // Truncate title if too long (max cols - 5, then add "...")
    int max_length = cols - 5;
    if (max_length < 10) max_length = 10; // Minimum reasonable length
    
    if ((int)display_title.length() > max_length) {
        display_title = display_title.substr(0, max_length) + "...";
    }
    
    // Move to bottom line
    std::cout << "\033[" << rows << ";1H";
    std::cout << "\033[44m\033[97m"; // Blue background, white text
    std::cout << " " << display_title;
    std::cout << "\033[K"; // Clear to end of line
    std::cout << "\033[0m"; // Reset colors
    std::cout << std::flush;
    
    restoreCursorPosition();
}

void StatusBar::showMessage(const std::string& message) {
    std::lock_guard<std::mutex> lock(SixelRenderer::getTerminalMutex());
    
    saveCursorPosition();
    
    struct winsize w;
    ioctl(STDOUT_FILENO, TIOCGWINSZ, &w);
    int rows = w.ws_row;
    
    // Move to bottom line
    std::cout << "\033[" << rows << ";1H";
    std::cout << "\033[44m\033[97m"; // Blue background, white text
    std::cout << " " << message;
    std::cout << "\033[K"; // Clear to end of line
    std::cout << "\033[0m"; // Reset colors
    std::cout << std::flush;
    
    restoreCursorPosition();
}

void StatusBar::showURLInput(const std::string& current_url) {
    std::lock_guard<std::mutex> lock(SixelRenderer::getTerminalMutex());
    
    saveCursorPosition();
    
    struct winsize w;
    ioctl(STDOUT_FILENO, TIOCGWINSZ, &w);
    int rows = w.ws_row;
    
    // Move to bottom line
    std::cout << "\033[" << rows << ";1H";
    std::cout << "\033[44m\033[97m"; // Blue background, white text
    std::cout << " URL: " << current_url;
    std::cout << "\033[K"; // Clear to end of line
    std::cout << "\033[0m"; // Reset colors
    std::cout << std::flush;
    
    restoreCursorPosition();
}

void StatusBar::showFileInput(const std::string& default_path) {
    std::lock_guard<std::mutex> lock(SixelRenderer::getTerminalMutex());
    
    saveCursorPosition();
    
    struct winsize w;
    ioctl(STDOUT_FILENO, TIOCGWINSZ, &w);
    int rows = w.ws_row;
    
    // Move to bottom line
    std::cout << "\033[" << rows << ";1H";
    std::cout << "\033[44m\033[97m"; // Blue background, white text
    std::cout << " File: " << default_path;
    std::cout << "\033[K"; // Clear to end of line
    std::cout << "\033[0m"; // Reset colors
    std::cout << std::flush;
    
    restoreCursorPosition();
}

void StatusBar::showComboboxOptions(const std::vector<std::string>& options, int selected_index) {
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
    int start_line = rows - max_display - 1;
    
    // Clear the entire options area
    for (int i = start_line; i <= rows; i++) {
        std::cout << "\033[" << i << ";1H\033[2K"; // Move to line and clear entire line
    }
    std::cout << std::flush; // Ensure clearing is complete before redrawing
    
    // Draw header
    std::cout << "\033[" << start_line << ";1H";
    std::cout << "\033[44m\033[97m\033[1m"; // Blue background, white bold text
    std::cout << " Select option (↑↓ to navigate, Enter to confirm, Esc to close): ";
    std::cout << "\033[K\033[0m";
    
    // Determine which options to show (with scrolling)
    int start_idx = 0;
    if (selected_index >= max_display - 1) {
        start_idx = std::min(selected_index - max_display + 2, (int)options.size() - max_display);
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
        std::cout << "\033[44m\033[97m"; // Blue background
        if (start_idx > 0) {
            std::cout << " ▲ More above";
        }
        if ((start_idx + max_display) < options.size()) {
            if (start_idx > 0) std::cout << "  ";
            std::cout << " ▼ More below";
        }
        std::cout << "\033[K\033[0m\n";
    }
    
    std::cout << std::flush;
    restoreCursorPosition();
}

void StatusBar::showConsole(const std::vector<std::string>& logs, const std::string& input, int scroll_offset) {
    std::lock_guard<std::mutex> lock(SixelRenderer::getTerminalMutex());
    
    struct winsize w;
    ioctl(STDOUT_FILENO, TIOCGWINSZ, &w);
    int rows = w.ws_row;
    int cols = w.ws_col;
    
    // Use bottom half of screen for console (or at least 10 lines)
    int console_height = std::max(10, rows / 2);
    int start_line = rows - console_height + 1;
    
    // Save cursor position AFTER we know where we'll be drawing
    saveCursorPosition();
    
    // Clear the entire console area from start_line to bottom of screen
    for (int i = start_line; i <= rows; i++) {
        std::cout << "\033[" << i << ";1H\033[2K"; // Move to line and clear entire line
    }
    
    // Draw header
    std::cout << "\033[" << start_line << ";1H";
    std::cout << "\033[44m\033[97m\033[1m"; // Blue background, white bold text
    std::cout << " JavaScript Console (↑↓ scroll, Esc to close, Enter to execute) ";
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
        std::cout << "\033[40m\033[97m"; // Black background, white text
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
        std::cout << " [" << (end_idx - start_idx) << "/" << total_logs << " messages]";
        if (start_idx > 0) std::cout << " ▲";
        if (end_idx < total_logs) std::cout << " ▼";
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
        showTitle(current_title_, current_mode_prefix_.empty() ? nullptr : current_mode_prefix_.c_str());
    }
}

void StatusBar::showPopupConfirm(const std::string& url) {
    std::lock_guard<std::mutex> lock(SixelRenderer::getTerminalMutex());
    
    saveCursorPosition();
    
    struct winsize w;
    ioctl(STDOUT_FILENO, TIOCGWINSZ, &w);
    int rows = w.ws_row;
    int cols = w.ws_col;
    
    // Use 5 lines at bottom
    int start_line = rows - 4;
    
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

void StatusBar::showJSAlert(const std::string& message) {
    std::lock_guard<std::mutex> lock(SixelRenderer::getTerminalMutex());
    
    saveCursorPosition();
    
    struct winsize w;
    ioctl(STDOUT_FILENO, TIOCGWINSZ, &w);
    int rows = w.ws_row;
    int cols = w.ws_col;
    
    // Use 5 lines at bottom
    int start_line = rows - 4;
    
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

void StatusBar::showJSConfirm(const std::string& message) {
    std::lock_guard<std::mutex> lock(SixelRenderer::getTerminalMutex());
    
    saveCursorPosition();
    
    struct winsize w;
    ioctl(STDOUT_FILENO, TIOCGWINSZ, &w);
    int rows = w.ws_row;
    int cols = w.ws_col;
    
    // Use 5 lines at bottom
    int start_line = rows - 4;
    
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

void StatusBar::showJSPrompt(const std::string& message, const std::string& default_value) {
    std::lock_guard<std::mutex> lock(SixelRenderer::getTerminalMutex());
    
    saveCursorPosition();
    
    struct winsize w;
    ioctl(STDOUT_FILENO, TIOCGWINSZ, &w);
    int rows = w.ws_row;
    int cols = w.ws_col;
    
    // Use 5 lines at bottom
    int start_line = rows - 4;
    
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

void StatusBar::showDownloadConfirm(const std::string& filename, const std::string& url) {
    std::lock_guard<std::mutex> lock(SixelRenderer::getTerminalMutex());
    
    saveCursorPosition();
    
    struct winsize w;
    ioctl(STDOUT_FILENO, TIOCGWINSZ, &w);
    int rows = w.ws_row;
    int cols = w.ws_col;
    
    // Use 6 lines at bottom
    int start_line = rows - 5;
    
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

void StatusBar::showBookmarks(const std::vector<std::string>& bookmarks, int selected_index) {
    if (bookmarks.empty()) {
        std::lock_guard<std::mutex> lock(SixelRenderer::getTerminalMutex());
        
        saveCursorPosition();
        
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
        std::cout << " No bookmarks yet. Press Ctrl+D to bookmark current page.";
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
    
    // Calculate how many bookmarks to show (max 10 lines)
    int max_display = std::min(10, (int)bookmarks.size());
    int start_line = rows - max_display - 2;
    
    // Clear status area
    clearStatusArea();
    
    // Draw header
    std::cout << "\033[" << start_line << ";1H";
    std::cout << "\033[44m\033[97m\033[1m"; // Blue background, white bold text
    std::cout << " 📚 Bookmarks (↑↓ navigate, Enter open, d delete, Esc close) ";
    std::cout << "\033[K\033[0m\n";
    
    // Determine which bookmarks to show (with scrolling)
    int start_idx = 0;
    if (selected_index >= max_display - 1) {
        start_idx = std::min(selected_index - max_display + 2, (int)bookmarks.size() - max_display);
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
        std::cout << "\033[K\033[0m\n"; // Clear to end of line and reset
    }
    
    // Show scroll indicators if needed
    if (start_idx > 0 || (start_idx + max_display) < bookmarks.size()) {
        std::cout << "\033[44m\033[97m"; // Blue background
        if (start_idx > 0) {
            std::cout << " ▲ More above";
        }
        if ((start_idx + max_display) < bookmarks.size()) {
            if (start_idx > 0) std::cout << "  ";
            std::cout << " ▼ More below";
        }
        std::cout << "\033[K\033[0m\n";
    }
    
    std::cout << std::flush;
    restoreCursorPosition();
}

void StatusBar::showUserScripts(const std::vector<std::string>& scripts, int selected_index) {
    if (scripts.empty()) {
        std::lock_guard<std::mutex> lock(SixelRenderer::getTerminalMutex());
        
        saveCursorPosition();
        
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
    
    // Calculate how many scripts to show (max 10 lines)
    int max_display = std::min(10, (int)scripts.size());
    int start_line = rows - max_display - 2;
    
    // Clear status area
    clearStatusArea();
    
    // Draw header
    std::cout << "\033[" << start_line << ";1H";
    std::cout << "\033[44m\033[97m\033[1m"; // Blue background, white bold text
    std::cout << " 📜 User Scripts (↑↓ navigate, Enter inject, Esc close) ";
    std::cout << "\033[K\033[0m\n";
    
    // Determine which scripts to show (with scrolling)
    int start_idx = 0;
    if (selected_index >= max_display - 1) {
        start_idx = std::min(selected_index - max_display + 2, (int)scripts.size() - max_display);
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
            if (start_idx > 0) std::cout << "  ";
            std::cout << " ▼ More below";
        }
        std::cout << "\033[K\033[0m\n";
    }
    
    std::cout << std::flush;
    restoreCursorPosition();
}

void StatusBar::showHintInput(const std::string& input, int hint_count) {
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
    std::cout << "\033[K"; // Clear to end of line
    std::cout << "\033[0m"; // Reset colors
    std::cout << std::flush;
    
    restoreCursorPosition();
}

void StatusBar::showDownloadManager(const std::vector<std::string>& downloads, int selected_index) {
    if (downloads.empty()) {
        std::lock_guard<std::mutex> lock(SixelRenderer::getTerminalMutex());
        saveCursorPosition();
        struct winsize w;
        ioctl(STDOUT_FILENO, TIOCGWINSZ, &w);
        int rows = w.ws_row;
        clearStatusArea();
        std::cout << "\033[" << (rows - 2) << ";1H\033[44m\033[97m\033[1m 📥 Download Manager \033[K\033[0m\n";
        std::cout << "\033[40m\033[97m No downloads yet.\033[K\033[0m" << std::flush;
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
    int max_display = std::min(10, (int)downloads.size());
    int start_line = rows - max_display - 2;
    clearStatusArea();
    std::cout << "\033[" << start_line << ";1H\033[44m\033[97m\033[1m 📥 Download Manager (↑↓ navigate, x cancel, c clear, m/Esc close) \033[K\033[0m\n";
    int start_idx = 0;
    if (selected_index >= max_display - 1) {
        start_idx = std::min(selected_index - max_display + 2, (int)downloads.size() - max_display);
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
