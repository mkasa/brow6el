#pragma once

#include "bookmarks.h"
#include "image_renderer.h"
#include "kitty_renderer.h"
#include "include/cef_client.h"
#include "include/cef_dialog_handler.h"
#include "include/cef_display_handler.h"
#include "include/cef_download_handler.h"
#include "include/cef_find_handler.h"
#include "include/cef_jsdialog_handler.h"
#include "include/cef_render_handler.h"
#include "include/cef_request_handler.h"
#include "status_bar.h"
#include "user_scripts.h"
#include <atomic>
#include <memory>
#include <mutex>

class InputHandler; // Forward declaration

class BrowserClient : public CefClient,
                      public CefRenderHandler,
                      public CefLifeSpanHandler,
                      public CefLoadHandler,
                      public CefDisplayHandler,
                      public CefJSDialogHandler,
                      public CefDownloadHandler,
                      public CefDialogHandler,
                      public CefRequestHandler,
                      public CefFindHandler {
public:
  struct DownloadEntry {
    int32_t id;
    std::string filename;
    std::string url;
    std::string full_path;
    int64_t total_bytes;
    int64_t received_bytes;
    int percent_complete;
    int64_t speed;
    bool is_complete;
    bool is_canceled;
    bool is_in_progress;
  };

  BrowserClient(int width, int height, int cell_width, int cell_height,
                bool supports_sixel, bool supports_kitty);

  CefRefPtr<CefRenderHandler> GetRenderHandler() override;
  CefRefPtr<CefLifeSpanHandler> GetLifeSpanHandler() override;
  CefRefPtr<CefLoadHandler> GetLoadHandler() override;
  CefRefPtr<CefDisplayHandler> GetDisplayHandler() override;
  CefRefPtr<CefJSDialogHandler> GetJSDialogHandler() override;
  CefRefPtr<CefDownloadHandler> GetDownloadHandler() override;
  CefRefPtr<CefDialogHandler> GetDialogHandler() override;
  CefRefPtr<CefRequestHandler> GetRequestHandler() override;
  CefRefPtr<CefFindHandler> GetFindHandler() override { return this; }

  virtual void GetViewRect(CefRefPtr<CefBrowser> browser,
                           CefRect &rect) override;
  virtual void OnPaint(CefRefPtr<CefBrowser> browser, PaintElementType type,
                       const RectList &dirtyRects, const void *buffer,
                       int width, int height) override;

  virtual void OnAfterCreated(CefRefPtr<CefBrowser> browser) override;
  virtual void OnBeforeClose(CefRefPtr<CefBrowser> browser) override;
  virtual bool OnBeforePopup(
      CefRefPtr<CefBrowser> browser, CefRefPtr<CefFrame> frame, int popup_id,
      const CefString &target_url, const CefString &target_frame_name,
      CefLifeSpanHandler::WindowOpenDisposition target_disposition,
      bool user_gesture, const CefPopupFeatures &popupFeatures,
      CefWindowInfo &windowInfo, CefRefPtr<CefClient> &client,
      CefBrowserSettings &settings, CefRefPtr<CefDictionaryValue> &extra_info,
      bool *no_javascript_access) override;

  virtual void OnLoadEnd(CefRefPtr<CefBrowser> browser,
                         CefRefPtr<CefFrame> frame,
                         int httpStatusCode) override;
  virtual void OnLoadError(CefRefPtr<CefBrowser> browser,
                           CefRefPtr<CefFrame> frame, ErrorCode errorCode,
                           const CefString &errorText,
                           const CefString &failedUrl) override;

  virtual bool OnConsoleMessage(CefRefPtr<CefBrowser> browser,
                                cef_log_severity_t level,
                                const CefString &message,
                                const CefString &source, int line) override;

  virtual void OnTitleChange(CefRefPtr<CefBrowser> browser,
                             const CefString &title) override;

  // CefJSDialogHandler methods
  virtual bool OnJSDialog(CefRefPtr<CefBrowser> browser,
                          const CefString &origin_url,
                          CefJSDialogHandler::JSDialogType dialog_type,
                          const CefString &message_text,
                          const CefString &default_prompt_text,
                          CefRefPtr<CefJSDialogCallback> callback,
                          bool &suppress_message) override;

  // CefDialogHandler methods
  virtual bool OnFileDialog(CefRefPtr<CefBrowser> browser,
                            CefDialogHandler::FileDialogMode mode,
                            const CefString &title,
                            const CefString &default_file_path,
                            const std::vector<CefString> &accept_filters,
                            const std::vector<CefString> &accept_extensions,
                            const std::vector<CefString> &accept_descriptions,
                            CefRefPtr<CefFileDialogCallback> callback) override;

  // CefRequestHandler method
  virtual bool GetAuthCredentials(CefRefPtr<CefBrowser> browser,
                                  const CefString &origin_url,
                                  bool isProxy,
                                  const CefString &host,
                                  int port,
                                  const CefString &realm,
                                  const CefString &scheme,
                                  CefRefPtr<CefAuthCallback> callback) override;

  // CefFindHandler methods
  void OnFindResult(CefRefPtr<CefBrowser> browser,
                    int identifier,
                    int count,
                    const CefRect& selectionRect,
                    int activeMatchOrdinal,
                    bool finalUpdate) override;

  CefRefPtr<CefBrowser> GetBrowser() { return browser_; }
  bool IsClosing() const { return is_closing_; }
  StatusBar *GetStatusBar() { return status_bar_.get(); }
  bool IsKittyRenderer() const;
  void Resize(int width, int height, int cell_width, int cell_height);
  void SetTiledRenderingEnabled(bool enabled) {
    if (renderer_)
      renderer_->setTiledRenderingEnabled(enabled);
  }
  void ForceFullRedraw() {
    FILE* log = fopen("/tmp/kitty_render.log", "a");
    if (log) {
      fprintf(log, "[BrowserClient::ForceFullRedraw] Called!\n");
      fclose(log);
    }
    if (renderer_) {
      // For Kitty: Don't force clear - use double buffering instead
      // Forcing clear deletes both buffers causing flicker
      if (dynamic_cast<KittyRenderer*>(renderer_.get())) {
        // Just invalidate, don't force clear
        if (browser_ && browser_->GetHost()) {
          browser_->GetHost()->Invalidate(PET_VIEW);
        }
      } else {
        renderer_->forceFullRender();
      }
    }
  }
  bool HandleSelectNavigation(int direction); // Returns true if handled
  bool HandleSelectConfirm();                 // Returns true if handled
  bool IsSelectOptionsActive() const { return !current_options_.empty(); }
  void SetUrlInputActive(bool active) { url_input_active_ = active; }
  void SetConsoleActive(bool active) { console_active_ = active; }
  void SetSearchActive(bool active) { search_active_ = active; }
  void setInputHandler(InputHandler* handler) { input_handler_ = handler; }
  bool IsConsoleActive() const { return console_active_; }
  const std::vector<std::string> &GetConsoleLogs() const {
    return console_logs_;
  }
  void ClearConsoleLogs() { console_logs_.clear(); }
  void SetShowInternalConsoleLogs(bool show) { show_internal_console_logs_ = show; }
  void ExecuteJavaScript(const std::string &code);
  void SetPopupConfirmActive(bool active, const std::string &url = "");
  bool IsPopupConfirmActive() const { return popup_confirm_active_; }
  const std::string &GetPopupUrl() const { return popup_url_; }
  void HandlePopupResponse(bool accept);
  void HandleJSDialogResponse(bool success, const std::string &input = "");
  bool IsJSDialogActive() const { return js_dialog_active_; }
  CefJSDialogHandler::JSDialogType GetJSDialogType() const {
    return js_dialog_type_;
  }
  const std::string &GetJSDialogMessage() const { return js_dialog_message_; }
  const std::string &GetJSDialogPromptDefault() const {
    return js_dialog_prompt_default_;
  }

  // HTTP Basic Auth handling
  void SetAuthDialogActive(bool active) { auth_dialog_active_ = active; }
  bool IsAuthDialogActive() const { return auth_dialog_active_; }
  const std::string &GetAuthRealm() const { return auth_realm_; }
  void HandleAuthResponse(bool accept, const std::string &username = "",
                         const std::string &password = "");

  // File dialog handling
  void SetFileInputActive(bool active) { file_input_active_ = active; }
  bool IsFileInputActive() const { return file_input_active_; }
  void HandleFileDialogResponse(const std::string &file_path);

  // CefDownloadHandler methods
  virtual bool
  OnBeforeDownload(CefRefPtr<CefBrowser> browser,
                   CefRefPtr<CefDownloadItem> download_item,
                   const CefString &suggested_name,
                   CefRefPtr<CefBeforeDownloadCallback> callback) override;

  virtual void
  OnDownloadUpdated(CefRefPtr<CefBrowser> browser,
                    CefRefPtr<CefDownloadItem> download_item,
                    CefRefPtr<CefDownloadItemCallback> callback) override;

  bool IsDownloadConfirmActive() const { return download_confirm_active_; }
  const std::string &GetDownloadFilename() const { return download_filename_; }
  const std::string &GetDownloadUrl() const { return download_url_; }
  void HandleDownloadResponse(bool accept, const std::string &path = "");

  // Download Manager
  void ToggleDownloadManager();
  bool IsDownloadManagerActive() const { return download_manager_active_; }
  void HandleDownloadManagerNavigation(int direction);
  void HandleDownloadManagerAction(char action);

  // Bookmarks
  void AddCurrentPageToBookmarks();
  void SetBookmarksActive(bool active);
  bool IsBookmarksActive() const { return bookmarks_active_; }
  bool HandleBookmarkNavigation(int direction);
  bool HandleBookmarkConfirm();
  bool HandleBookmarkDelete();
  BookmarksManager *GetBookmarksManager() { return &bookmarks_manager_; }

  // User Scripts
  void SetUserScriptsActive(bool active);
  bool IsUserScriptsActive() const { return user_scripts_active_; }
  bool HandleUserScriptNavigation(int direction);
  bool HandleUserScriptConfirm();
  void ToggleAutoInjectUserScripts();
  void InjectUserScriptsForCurrentPage();
  UserScriptsManager *GetUserScriptsManager() { return &user_scripts_manager_; }

  // Hint Mode (keyboard link navigation)
  void ActivateHintMode();
  void SetHintModeActive(bool active);
  bool IsHintModeActive() const { return hint_mode_active_; }
  void HandleHintSelection(const std::string &hint);

  // Mouse Emulation Mode
  void ActivateMouseEmuMode();
  void SetMouseEmuModeActive(bool active);
  bool IsMouseEmuModeActive() const { return mouse_emu_mode_active_; }
  bool IsGridModeActive() const { return grid_mode_active_; }
  bool GetAndClearGridModeHandledKey() {
    bool result = grid_mode_handled_key_;
    grid_mode_handled_key_ = false;
    return result;
  }
  void HandleMouseEmuKey(const std::string &key);
  void HandleMouseEmuClick();
  void HandleMouseEmuDragStart();
  void HandleMouseEmuDragEnd();
  void HandleMouseEmuPosition(int x, int y);
  void ToggleInspectMode();

  // Visual Mode (text selection)
  void ActivateVisualMode();
  void SetVisualModeActive(bool active);
  bool IsVisualModeActive() const { return visual_mode_active_; }
  void HandleVisualModeKey(const std::string &key);

  // Clipboard operations
  void CopyCurrentURL();

  // Mode switch request (from mouse click detection)
  bool GetModeSwitchRequest() const { return mode_switch_requested_; }
  bool GetSwitchToInsertMode() const { return switch_to_insert_mode_; }
  void ClearModeSwitchRequest() {
    mode_switch_requested_ = false;
    switch_to_insert_mode_ = false;
  }

  // Input mode display
  void SetInputMode(const char *mode) {
    input_mode_ = mode;
    // Update status bar with current title and new mode
    if (status_bar_ && !current_page_title_.empty()) {
      status_bar_->showTitle(current_page_title_, input_mode_);
    }
  }

private:
  int width_;
  int height_;
  int cell_width_;
  int cell_height_;
  bool supports_sixel_;
  bool supports_kitty_;
  CefRefPtr<CefBrowser> browser_;
  std::unique_ptr<ImageRenderer> renderer_;
  std::unique_ptr<StatusBar> status_bar_;
  InputHandler* input_handler_ = nullptr;
  std::atomic<bool> is_closing_;
  std::mutex render_mutex_; // Synchronize rendering and status updates

  // Track current select element state
  std::vector<std::string> current_options_;
  int current_selected_index_;
  bool url_input_active_ = false;
  bool console_active_ = false;
  bool search_active_ = false;
  int search_match_count_ = 0;
  int search_active_match_ = 0;
  std::vector<std::string> console_logs_;
  std::mutex console_mutex_;
  bool show_internal_console_logs_ = false;

  // Popup window confirmation
  bool popup_confirm_active_ = false;
  std::string popup_url_;

  // JS dialog handling
  bool js_dialog_active_ = false;
  CefJSDialogHandler::JSDialogType js_dialog_type_;
  std::string js_dialog_message_;
  std::string js_dialog_prompt_default_;
  CefRefPtr<CefJSDialogCallback> js_dialog_callback_;
  std::mutex js_dialog_mutex_;

  // File dialog handling
  bool file_input_active_ = false;
  CefRefPtr<CefFileDialogCallback> file_dialog_callback_;
  std::mutex file_dialog_mutex_;

  // HTTP Basic Auth handling
  bool auth_dialog_active_ = false;
  std::string auth_realm_;
  CefRefPtr<CefAuthCallback> auth_callback_;
  std::mutex auth_mutex_;

  // Download handling
  bool download_confirm_active_ = false;
  std::string download_filename_;
  std::string download_url_;
  CefRefPtr<CefBeforeDownloadCallback> download_callback_;
  std::mutex download_mutex_;

  // Download manager
  std::vector<DownloadEntry> downloads_list_;
  bool download_manager_active_ = false;
  int download_manager_selected_index_ = 0;

  // Bookmarks handling
  bool bookmarks_active_ = false;
  int bookmarks_selected_index_ = 0;
  BookmarksManager bookmarks_manager_;
  std::string current_page_title_;

  // User Scripts handling
  bool user_scripts_active_ = false;
  int user_scripts_selected_index_ = 0;
  UserScriptsManager user_scripts_manager_;

  // Hint mode handling
  bool hint_mode_active_ = false;
  int hint_count_ = 0;

  // Mouse emulation mode handling
  bool mouse_emu_mode_active_ = false;
  int mouse_emu_x_ = 0;
  int mouse_emu_y_ = 0;
  bool mouse_emu_dragging_ = false;
  bool mode_switch_requested_ = false;
  bool switch_to_insert_mode_ = false;
  bool grid_mode_active_ = false;
  bool grid_mode_handled_key_ = false;

  // Visual mode handling
  bool visual_mode_active_ = false;
  bool first_load_complete_ = false;
  bool force_next_paint_ = false;
  int paint_count_ = 0;

  // Input mode display
  const char *input_mode_ = "S";

  void injectSelectDetector();
  void parseSelectMessage(const std::string &message);

  IMPLEMENT_REFCOUNTING(BrowserClient);
};
