#pragma once

#include "include/cef_client.h"
#include "include/cef_render_handler.h"
#include "include/cef_display_handler.h"
#include "include/cef_jsdialog_handler.h"
#include "include/cef_download_handler.h"
#include "include/cef_dialog_handler.h"
#include "sixel_renderer.h"
#include "status_bar.h"
#include "bookmarks.h"
#include "user_scripts.h"
#include <memory>
#include <atomic>
#include <mutex>

class BrowserClient : public CefClient,
                      public CefRenderHandler,
                      public CefLifeSpanHandler,
                      public CefLoadHandler,
                      public CefDisplayHandler,
                      public CefJSDialogHandler,
                      public CefDownloadHandler,
                      public CefDialogHandler {
public:
    BrowserClient(int width, int height);
    
    virtual CefRefPtr<CefRenderHandler> GetRenderHandler() override { return this; }
    virtual CefRefPtr<CefLifeSpanHandler> GetLifeSpanHandler() override { return this; }
    virtual CefRefPtr<CefLoadHandler> GetLoadHandler() override { return this; }
    virtual CefRefPtr<CefDisplayHandler> GetDisplayHandler() override { return this; }
    virtual CefRefPtr<CefJSDialogHandler> GetJSDialogHandler() override { return this; }
    virtual CefRefPtr<CefDownloadHandler> GetDownloadHandler() override { return this; }
    virtual CefRefPtr<CefDialogHandler> GetDialogHandler() override { return this; }
    
    virtual void GetViewRect(CefRefPtr<CefBrowser> browser, CefRect& rect) override;
    virtual void OnPaint(CefRefPtr<CefBrowser> browser, PaintElementType type,
                        const RectList& dirtyRects, const void* buffer,
                        int width, int height) override;
    
    virtual void OnAfterCreated(CefRefPtr<CefBrowser> browser) override;
    virtual void OnBeforeClose(CefRefPtr<CefBrowser> browser) override;
    virtual bool OnBeforePopup(CefRefPtr<CefBrowser> browser,
                               CefRefPtr<CefFrame> frame,
                               int popup_id,
                               const CefString& target_url,
                               const CefString& target_frame_name,
                               CefLifeSpanHandler::WindowOpenDisposition target_disposition,
                               bool user_gesture,
                               const CefPopupFeatures& popupFeatures,
                               CefWindowInfo& windowInfo,
                               CefRefPtr<CefClient>& client,
                               CefBrowserSettings& settings,
                               CefRefPtr<CefDictionaryValue>& extra_info,
                               bool* no_javascript_access) override;
    
    virtual void OnLoadEnd(CefRefPtr<CefBrowser> browser, CefRefPtr<CefFrame> frame,
                          int httpStatusCode) override;
    virtual void OnLoadError(CefRefPtr<CefBrowser> browser, CefRefPtr<CefFrame> frame,
                            ErrorCode errorCode, const CefString& errorText,
                            const CefString& failedUrl) override;
    
    virtual bool OnConsoleMessage(CefRefPtr<CefBrowser> browser,
                                 cef_log_severity_t level,
                                 const CefString& message,
                                 const CefString& source,
                                 int line) override;
    
    virtual void OnTitleChange(CefRefPtr<CefBrowser> browser, const CefString& title) override;
    
    // CefJSDialogHandler methods
    virtual bool OnJSDialog(CefRefPtr<CefBrowser> browser,
                           const CefString& origin_url,
                           CefJSDialogHandler::JSDialogType dialog_type,
                           const CefString& message_text,
                           const CefString& default_prompt_text,
                           CefRefPtr<CefJSDialogCallback> callback,
                           bool& suppress_message) override;
    
    // CefDialogHandler methods
    virtual bool OnFileDialog(CefRefPtr<CefBrowser> browser,
                             CefDialogHandler::FileDialogMode mode,
                             const CefString& title,
                             const CefString& default_file_path,
                             const std::vector<CefString>& accept_filters,
                             const std::vector<CefString>& accept_extensions,
                             const std::vector<CefString>& accept_descriptions,
                             CefRefPtr<CefFileDialogCallback> callback) override;
    
    CefRefPtr<CefBrowser> GetBrowser() { return browser_; }
    bool IsClosing() const { return is_closing_; }
    StatusBar* GetStatusBar() { return status_bar_.get(); }
    bool HandleSelectNavigation(int direction); // Returns true if handled
    bool HandleSelectConfirm(); // Returns true if handled
    bool IsSelectOptionsActive() const { return !current_options_.empty(); }
    void SetUrlInputActive(bool active) { url_input_active_ = active; }
    void SetConsoleActive(bool active) { console_active_ = active; }
    bool IsConsoleActive() const { return console_active_; }
    const std::vector<std::string>& GetConsoleLogs() const { return console_logs_; }
    void ClearConsoleLogs() { console_logs_.clear(); }
    void ExecuteJavaScript(const std::string& code);
    void SetPopupConfirmActive(bool active, const std::string& url = "");
    bool IsPopupConfirmActive() const { return popup_confirm_active_; }
    const std::string& GetPopupUrl() const { return popup_url_; }
    void HandlePopupResponse(bool accept);
    void HandleJSDialogResponse(bool success, const std::string& input = "");
    bool IsJSDialogActive() const { return js_dialog_active_; }
    CefJSDialogHandler::JSDialogType GetJSDialogType() const { return js_dialog_type_; }
    const std::string& GetJSDialogMessage() const { return js_dialog_message_; }
    const std::string& GetJSDialogPromptDefault() const { return js_dialog_prompt_default_; }
    
    // File dialog handling
    void SetFileInputActive(bool active) { file_input_active_ = active; }
    bool IsFileInputActive() const { return file_input_active_; }
    void HandleFileDialogResponse(const std::string& file_path);
    
    // CefDownloadHandler methods
    virtual bool OnBeforeDownload(CefRefPtr<CefBrowser> browser,
                                  CefRefPtr<CefDownloadItem> download_item,
                                  const CefString& suggested_name,
                                  CefRefPtr<CefBeforeDownloadCallback> callback) override;
    
    virtual void OnDownloadUpdated(CefRefPtr<CefBrowser> browser,
                                   CefRefPtr<CefDownloadItem> download_item,
                                   CefRefPtr<CefDownloadItemCallback> callback) override;
    
    bool IsDownloadConfirmActive() const { return download_confirm_active_; }
    const std::string& GetDownloadFilename() const { return download_filename_; }
    const std::string& GetDownloadUrl() const { return download_url_; }
    void HandleDownloadResponse(bool accept, const std::string& path = "");
    
    // Bookmarks
    void AddCurrentPageToBookmarks();
    void SetBookmarksActive(bool active);
    bool IsBookmarksActive() const { return bookmarks_active_; }
    bool HandleBookmarkNavigation(int direction);
    bool HandleBookmarkConfirm();
    bool HandleBookmarkDelete();
    BookmarksManager* GetBookmarksManager() { return &bookmarks_manager_; }
    
    // User Scripts
    void SetUserScriptsActive(bool active);
    bool IsUserScriptsActive() const { return user_scripts_active_; }
    bool HandleUserScriptNavigation(int direction);
    bool HandleUserScriptConfirm();
    void ToggleAutoInjectUserScripts();
    void InjectUserScriptsForCurrentPage();
    UserScriptsManager* GetUserScriptsManager() { return &user_scripts_manager_; }
    
    // Hint Mode (keyboard link navigation)
    void ActivateHintMode();
    void SetHintModeActive(bool active);
    bool IsHintModeActive() const { return hint_mode_active_; }
    void HandleHintSelection(const std::string& hint);
    
    // Mouse Emulation Mode
    void ActivateMouseEmuMode();
    void SetMouseEmuModeActive(bool active);
    bool IsMouseEmuModeActive() const { return mouse_emu_mode_active_; }
    void HandleMouseEmuKey(const std::string& key);
    void HandleMouseEmuClick();
    void HandleMouseEmuPosition(int x, int y);
    
private:
    int width_;
    int height_;
    CefRefPtr<CefBrowser> browser_;
    std::unique_ptr<SixelRenderer> renderer_;
    std::unique_ptr<StatusBar> status_bar_;
    std::atomic<bool> is_closing_;
    std::mutex render_mutex_;  // Synchronize rendering and status updates
    
    // Track current select element state
    std::vector<std::string> current_options_;
    int current_selected_index_;
    bool url_input_active_ = false;
    bool console_active_ = false;
    std::vector<std::string> console_logs_;
    std::mutex console_mutex_;
    
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
    
    // Download handling
    bool download_confirm_active_ = false;
    std::string download_filename_;
    std::string download_url_;
    CefRefPtr<CefBeforeDownloadCallback> download_callback_;
    std::mutex download_mutex_;
    
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
    
    void injectSelectDetector();
    void parseSelectMessage(const std::string& message);
    
    IMPLEMENT_REFCOUNTING(BrowserClient);
};
