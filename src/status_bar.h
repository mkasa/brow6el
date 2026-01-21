#pragma once

#include <string>
#include <vector>

class StatusBar {
public:
  StatusBar();
  ~StatusBar();

  void showComboboxOptions(const std::vector<std::string> &options,
                           int selected_index);
  void showTitle(const std::string &title, const std::string &mode);
  void showURLInput(const std::string &current_url);
  void showSearchInput(const std::string &query, int current = 0, int total = 0);
  void showFileInput(const std::string &default_path);
  void showAuthDialog(const std::string &host, const std::string &realm);
  void showConsole(const std::vector<std::string> &logs,
                   const std::string &input, int scroll_offset);
  void showPopupConfirm(const std::string &url);
  void showJSAlert(const std::string &message);
  void showJSConfirm(const std::string &message);
  void showJSPrompt(const std::string &message,
                    const std::string &default_value);
  void showDownloadConfirm(const std::string &filename, const std::string &url);
  void showDownloadManager(const std::vector<std::string> &downloads,
                           int selected_index);
  void showBookmarks(const std::vector<std::string> &bookmarks,
                     int selected_index);
  void showUserScripts(const std::vector<std::string> &scripts,
                       int selected_index);
  void showMessage(const std::string &message);
  void showTitle(const std::string &title, const char *mode_prefix = nullptr);
  void showHintInput(const std::string &input, int hint_count);
  void clear(bool redraw_title = true);
  void redraw(); // Redraw last shown content
  void setShutdownMode() {
    shutdown_mode_ = true;
  } // Prevent any further updates
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
  bool shutdown_mode_ = false; // Prevent updates during shutdown
  std::vector<std::string> current_options_;
  int current_selected_;
  std::string current_title_;
  std::string current_mode_prefix_;
};
