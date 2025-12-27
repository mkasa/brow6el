#pragma once

#include <string>
#include <vector>

class StatusBar {
public:
    StatusBar();
    ~StatusBar();
    
    void showComboboxOptions(const std::vector<std::string>& options, int selected_index);
    void showURLInput(const std::string& current_url);
    void showFileInput(const std::string& default_path);
    void showConsole(const std::vector<std::string>& logs, const std::string& input, int scroll_offset);
    void showPopupConfirm(const std::string& url);
    void showJSAlert(const std::string& message);
    void showJSConfirm(const std::string& message);
    void showJSPrompt(const std::string& message, const std::string& default_value);
    void showDownloadConfirm(const std::string& filename, const std::string& url);
    void showBookmarks(const std::vector<std::string>& bookmarks, int selected_index);
    void showUserScripts(const std::vector<std::string>& scripts, int selected_index);
    void showMessage(const std::string& message);
    void showTitle(const std::string& title);
    void showHintInput(const std::string& input, int hint_count);
    void clear();
    void redraw(); // Redraw last shown content
    void RequestRedraw() { redraw_requested_ = true; }
    bool IsRedrawRequested() { return redraw_requested_; }
    void ClearRedrawRequest() { redraw_requested_ = false; }
    
private:
    void saveCursorPosition();
    void restoreCursorPosition();
    void clearStatusArea();
    void drawBox(int width, int height);
    
    // State for redrawing
    bool is_showing_;
    bool redraw_requested_ = false;
    std::vector<std::string> current_options_;
    int current_selected_;
    std::string current_title_;
};

