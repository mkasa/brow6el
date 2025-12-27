#include "browser_client.h"
#include <iostream>
#include <fstream>
#include <sstream>
#include <unistd.h>

#define LOGB(msg) do { \
    std::ofstream log("/tmp/brow6el_debug.log", std::ios::app); \
    log << msg << std::endl; \
} while(0)

BrowserClient::BrowserClient(int width, int height)
    : width_(width), height_(height), is_closing_(false), current_selected_index_(-1) {
    renderer_ = std::make_unique<SixelRenderer>(width, height);
    status_bar_ = std::make_unique<StatusBar>();
}

void BrowserClient::GetViewRect(CefRefPtr<CefBrowser> browser, CefRect& rect) {
    rect.x = 0;
    rect.y = 0;
    rect.width = width_;
    rect.height = height_;
}

void BrowserClient::OnPaint(CefRefPtr<CefBrowser> browser, PaintElementType type,
                           const RectList& dirtyRects, const void* buffer,
                           int width, int height) {
    std::lock_guard<std::mutex> lock(render_mutex_);
    
    // Skip rendering when URL input, console, popup confirm, JS dialog, file input, download confirm, bookmarks, or user scripts is active
    // Note: hint_mode_active and mouse_emu_mode_active are NOT in this list because they use JS overlays that need the page visible
    if (url_input_active_ || console_active_ || popup_confirm_active_ || js_dialog_active_ || file_input_active_ || download_confirm_active_ || bookmarks_active_ || user_scripts_active_) {
        return;
    }
    
    if (type == PET_VIEW && renderer_ && buffer && width > 0 && height > 0) {
        try {
            renderer_->render(buffer, width, height, false);
            
            // Redraw status bar after sixel render (so it stays visible)
            // Skip only for hint mode (which has its own yellow status bar)
            // Allow for mouse emu mode since it doesn't use status bar after initial activation
            if (status_bar_ && !hint_mode_active_) {
                status_bar_->redraw();
            }
        } catch (const std::exception& e) {
            LOGB("Render error: " << e.what());
        } catch (...) {
            LOGB("Unknown render error");
        }
    }
}

void BrowserClient::OnAfterCreated(CefRefPtr<CefBrowser> browser) {
    browser_ = browser;
    LOGB("Browser created");
}

void BrowserClient::OnBeforeClose(CefRefPtr<CefBrowser> browser) {
    browser_ = nullptr;
    is_closing_ = true;
    if (status_bar_) {
        std::lock_guard<std::mutex> lock(render_mutex_);
        status_bar_->clear();
    }
    LOGB("Browser closing");
}

bool BrowserClient::OnBeforePopup(CefRefPtr<CefBrowser> browser,
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
                                   bool* no_javascript_access) {
    std::string url = target_url.ToString();
    LOGB("OnBeforePopup: Intercepting popup to " << url);
    
    // Store the URL and show confirmation dialog
    SetPopupConfirmActive(true, url);
    
    // Return true to cancel the popup creation
    return true;
}

void BrowserClient::OnLoadEnd(CefRefPtr<CefBrowser> browser, CefRefPtr<CefFrame> frame,
                             int httpStatusCode) {
    if (frame->IsMain()) {
        std::string url = frame->GetURL().ToString();
        LOGB("OnLoadEnd: url=" << url << " status=" << httpStatusCode);
        
        // Clear modes on navigation
        hint_mode_active_ = false;
        mouse_emu_mode_active_ = false;
        
        // Clear status bar on new page load only if something is showing
        if (status_bar_) {
            std::lock_guard<std::mutex> lock(render_mutex_);
            // Only clear if we're showing something (not just blank)
            if (!current_options_.empty() || url_input_active_ || console_active_ || 
                popup_confirm_active_ || js_dialog_active_ || file_input_active_ || download_confirm_active_ || bookmarks_active_) {
                status_bar_->clear();
            }
        }
        current_options_.clear();
        
        LOGB("OnLoadEnd: injecting select detector...");
        injectSelectDetector();
        LOGB("OnLoadEnd: injection complete");
        
        // Auto-inject user scripts if enabled
        InjectUserScriptsForCurrentPage();
    }
}

void BrowserClient::OnLoadError(CefRefPtr<CefBrowser> browser, CefRefPtr<CefFrame> frame,
                               ErrorCode errorCode, const CefString& errorText,
                               const CefString& failedUrl) {
    if (frame->IsMain() && errorCode != ERR_ABORTED) {
        LOGB("Load error: " << errorText.ToString());
    }
}

bool BrowserClient::OnConsoleMessage(CefRefPtr<CefBrowser> browser,
                                    cef_log_severity_t level,
                                    const CefString& message,
                                    const CefString& source,
                                    int line) {
    std::string msg = message.ToString();
    
    // Log ALL console messages for debugging
    FILE* log = fopen("/tmp/brow6el_debug.log", "a");
    if (log) {
        fprintf(log, "Console: %s\n", msg.c_str());
        fclose(log);
    }
    
    // Store console messages for console view
    {
        std::lock_guard<std::mutex> lock(console_mutex_);
        
        // Format message with severity
        std::string level_str;
        switch (level) {
            case LOGSEVERITY_DEBUG: level_str = "[DEBUG] "; break;
            case LOGSEVERITY_INFO: level_str = "[INFO] "; break;
            case LOGSEVERITY_WARNING: level_str = "[WARN] "; break;
            case LOGSEVERITY_ERROR: level_str = "[ERROR] "; break;
            default: level_str = "[LOG] "; break;
        }
        
        console_logs_.push_back(level_str + msg);
        
        // Keep only last 1000 messages
        if (console_logs_.size() > 1000) {
            console_logs_.erase(console_logs_.begin(), console_logs_.begin() + 100);
        }
        
        // Trigger status bar redraw if console is active
        if (console_active_ && status_bar_) {
            status_bar_->RequestRedraw();
        }
    }
    
    // Check for our custom messages
    if (msg.find("BROW6EL_SELECT_") == 0) {
        parseSelectMessage(msg);
        return true; // Suppress console output
    }
    
    // Check for hint mode messages
    if (msg.find("[Brow6el] HINT_MODE_ACTIVE:") == 0) {
        try {
            std::string count_str = msg.substr(28);
            hint_count_ = std::stoi(count_str);
            if (status_bar_) {
                status_bar_->showHintInput("", hint_count_);
            }
        } catch (const std::exception& e) {
            LOGB("Failed to parse hint count: " << e.what());
            hint_count_ = 0;
        }
        return true;
    }
    if (msg.find("[Brow6el] HINT_") == 0) {
        // Suppress hint mode debug messages
        return true;
    }
    
    // Check for mouse emulation messages
    if (msg.find("[Brow6el] MOUSE_EMU_POS:") == 0) {
        // Parse position: [Brow6el] MOUSE_EMU_POS:x,y
        try {
            std::string pos_str = msg.substr(24);
            size_t comma = pos_str.find(',');
            if (comma != std::string::npos) {
                int x = std::stoi(pos_str.substr(0, comma));
                int y = std::stoi(pos_str.substr(comma + 1));
                HandleMouseEmuPosition(x, y);
            }
        } catch (const std::exception& e) {
            LOGB("Failed to parse mouse emu position: " << e.what());
        }
        return true;
    }
    if (msg.find("[Brow6el] MOUSE_EMU_CLICK") == 0) {
        // JavaScript determined this is not a select/input, so send real click
        HandleMouseEmuClick();
        return true;
    }
    if (msg.find("[Brow6el] MOUSE_EMU_FOCUS") == 0) {
        // JavaScript focused a select/input element, don't send click
        LOGB("Mouse emu focused element: " << msg);
        return true;
    }
    if (msg.find("[Brow6el] MOUSE_EMU_") == 0) {
        // Suppress other mouse emu debug messages
        return true;
    }
    
    return false; // Show other console messages
}

void BrowserClient::OnTitleChange(CefRefPtr<CefBrowser> browser, const CefString& title) {
    std::string page_title = title.ToString();
    current_page_title_ = page_title; // Store for bookmarks
    std::string window_title = "brow6el - " + page_title;
    LOGB("OnTitleChange called: " << page_title);
    
    // Use printf and fflush for immediate output
    printf("\033]0;%s\007", window_title.c_str());
    fflush(stdout);
    
    // Show title in status bar
    if (status_bar_) {
        status_bar_->showTitle(page_title);
    }
}

void BrowserClient::injectSelectDetector() {
    if (!browser_) return;
    
    // Read JavaScript file from build directory
    std::ifstream js_file("select_detector.js");
    if (!js_file.is_open()) {
        LOGB("Warning: Could not load select_detector.js from current directory");
        return;
    }
    
    std::stringstream buffer;
    buffer << js_file.rdbuf();
    std::string js_code = buffer.str();
    
    // Execute JavaScript in main frame
    CefRefPtr<CefFrame> frame = browser_->GetMainFrame();
    if (frame) {
        frame->ExecuteJavaScript(js_code, frame->GetURL(), 0);
        LOGB("Select detector injected");
    }
}

void BrowserClient::parseSelectMessage(const std::string& message) {
    if (!status_bar_) return;
    
    if (message.find("BROW6EL_SELECT_FOCUSED:") == 0) {
        // Extract JSON data
        std::string json = message.substr(23); // Skip "BROW6EL_SELECT_FOCUSED:"
        
        // Simple JSON parsing (find options array and selectedIndex)
        std::vector<std::string> options;
        int selected_index = 0;
        
        // Extract options (looking for ["text1","text2",...])
        size_t options_start = json.find("[");
        size_t options_end = json.find("]");
        if (options_start != std::string::npos && options_end != std::string::npos) {
            std::string options_str = json.substr(options_start + 1, options_end - options_start - 1);
            
            // Parse each option
            size_t pos = 0;
            while (pos < options_str.length()) {
                size_t quote_start = options_str.find('"', pos);
                if (quote_start == std::string::npos) break;
                
                size_t quote_end = options_str.find('"', quote_start + 1);
                if (quote_end == std::string::npos) break;
                
                std::string option = options_str.substr(quote_start + 1, quote_end - quote_start - 1);
                options.push_back(option);
                pos = quote_end + 1;
            }
        }
        
        // Extract selectedIndex
        size_t index_pos = json.find("selectedIndex\":");
        if (index_pos != std::string::npos) {
            selected_index = std::atoi(json.c_str() + index_pos + 15);
        }
        
        // Show status bar
        if (!options.empty()) {
            current_options_ = options; // Save for later updates
            current_selected_index_ = selected_index; // Save current index
            
            std::lock_guard<std::mutex> lock(render_mutex_);
            status_bar_->showComboboxOptions(options, selected_index);
        }
        
    } else if (message == "BROW6EL_SELECT_BLURRED") {
        {
            std::lock_guard<std::mutex> lock(render_mutex_);
            status_bar_->clear();
        }
        current_options_.clear(); // Clear saved options
        current_selected_index_ = -1; // Reset index
        
    } else if (message.find("BROW6EL_SELECT_CHANGED:") == 0) {
        // Extract new selected index
        int new_index = std::atoi(message.c_str() + 23); // Skip "BROW6EL_SELECT_CHANGED:"
        
        FILE* log = fopen("/tmp/brow6el_debug.log", "a");
        if (log) {
            fprintf(log, "SELECT_CHANGED: new_index=%d, have_options=%d\n", 
                    new_index, (int)current_options_.size());
            fclose(log);
        }
        
        // Update status bar if it's currently showing
        if (status_bar_ && !current_options_.empty()) {
            std::lock_guard<std::mutex> lock(render_mutex_);
            status_bar_->showComboboxOptions(current_options_, new_index);
        }
    }
}

bool BrowserClient::HandleSelectNavigation(int direction) {
    // Only handle if status bar is showing and we have options
    if (current_options_.empty() || current_selected_index_ < 0) {
        return false;
    }
    
    // Calculate new selection
    int new_index = current_selected_index_ + direction;
    
    // Clamp to valid range
    if (new_index < 0) {
        new_index = 0;
    }
    if (new_index >= (int)current_options_.size()) {
        new_index = current_options_.size() - 1;
    }
    
    // Only update if index actually changed
    if (new_index == current_selected_index_) {
        return true; // We handled it, but no change needed
    }
    
    current_selected_index_ = new_index;
    
    // Update status bar display
    {
        std::lock_guard<std::mutex> lock(render_mutex_);
        status_bar_->showComboboxOptions(current_options_, current_selected_index_);
    }
    
    // Update the actual select element in CEF via JavaScript
    if (browser_ && browser_->GetMainFrame()) {
        std::string js = "if (window._brow6el_focused_select) { "
                        "window._brow6el_focused_select.selectedIndex = " + 
                        std::to_string(current_selected_index_) + "; "
                        "window._brow6el_select_index = " + 
                        std::to_string(current_selected_index_) + "; }";
        browser_->GetMainFrame()->ExecuteJavaScript(js, "", 0);
    }
    
    return true; // We handled it
}

bool BrowserClient::HandleSelectConfirm() {
    // Only handle if status bar is showing and we have options
    if (current_options_.empty() || current_selected_index_ < 0) {
        return false;
    }
    
    // Set the final selection in CEF and trigger change event
    if (browser_ && browser_->GetMainFrame()) {
        std::string js = "if (window._brow6el_focused_select) { "
                        "window._brow6el_focused_select.selectedIndex = " + 
                        std::to_string(current_selected_index_) + "; "
                        // Trigger change event so the page knows selection changed
                        "var event = new Event('change', { bubbles: true }); "
                        "window._brow6el_focused_select.dispatchEvent(event); "
                        // Blur the select to close it
                        "window._brow6el_focused_select.blur(); "
                        "}";
        browser_->GetMainFrame()->ExecuteJavaScript(js, "", 0);
    }
    
    // Clear status bar and state
    {
        std::lock_guard<std::mutex> lock(render_mutex_);
        status_bar_->clear();
    }
    current_options_.clear();
    current_selected_index_ = -1;
    
    return true; // We handled it
}

void BrowserClient::ExecuteJavaScript(const std::string& code) {
    if (browser_ && browser_->GetMainFrame()) {
        // Escape backslashes and quotes in user code
        std::string escaped_code = code;
        size_t pos = 0;
        while ((pos = escaped_code.find('\\', pos)) != std::string::npos) {
            escaped_code.replace(pos, 1, "\\\\");
            pos += 2;
        }
        pos = 0;
        while ((pos = escaped_code.find('"', pos)) != std::string::npos) {
            escaped_code.replace(pos, 1, "\\\"");
            pos += 2;
        }
        pos = 0;
        while ((pos = escaped_code.find('\n', pos)) != std::string::npos) {
            escaped_code.replace(pos, 1, "\\n");
            pos += 2;
        }
        
        // Execute code in global scope using indirect eval
        std::string wrapped_code = 
            "(function() { "
            "  try { "
            "    var result = (0, eval)(\"" + escaped_code + "\"); "
            "    console.log('← ' + (result !== undefined ? result : 'undefined')); "
            "    return result; "
            "  } catch(e) { "
            "    console.error('✗ ' + e.toString()); "
            "  } "
            "})();";
        
        browser_->GetMainFrame()->ExecuteJavaScript(wrapped_code, "", 0);
    }
}

void BrowserClient::SetPopupConfirmActive(bool active, const std::string& url) {
    popup_confirm_active_ = active;
    popup_url_ = url;
    
    if (active) {
        std::lock_guard<std::mutex> lock(render_mutex_);
        status_bar_->showPopupConfirm(url);
    } else {
        std::lock_guard<std::mutex> lock(render_mutex_);
        status_bar_->clear();
    }
}

void BrowserClient::HandlePopupResponse(bool accept) {
    if (!popup_confirm_active_) {
        return;
    }
    
    std::string url = popup_url_;
    SetPopupConfirmActive(false);
    
    if (accept && browser_ && browser_->GetMainFrame()) {
        LOGB("Opening popup URL in current window: " << url);
        browser_->GetMainFrame()->LoadURL(url);
    } else {
        LOGB("Popup rejected by user");
        // Force a repaint to clear the status bar
        if (browser_ && browser_->GetHost()) {
            browser_->GetHost()->Invalidate(PET_VIEW);
        }
    }
}

bool BrowserClient::OnJSDialog(CefRefPtr<CefBrowser> browser,
                               const CefString& origin_url,
                               CefJSDialogHandler::JSDialogType dialog_type,
                               const CefString& message_text,
                               const CefString& default_prompt_text,
                               CefRefPtr<CefJSDialogCallback> callback,
                               bool& suppress_message) {
    LOGB("JS Dialog: type=" << dialog_type << " message=" << message_text.ToString());
    
    std::lock_guard<std::mutex> lock(js_dialog_mutex_);
    
    // Store dialog info
    js_dialog_active_ = true;
    js_dialog_type_ = dialog_type;
    js_dialog_message_ = message_text.ToString();
    js_dialog_prompt_default_ = default_prompt_text.ToString();
    js_dialog_callback_ = callback;
    
    // Show appropriate dialog in status bar
    std::lock_guard<std::mutex> render_lock(render_mutex_);
    switch (dialog_type) {
        case JSDIALOGTYPE_ALERT:
            status_bar_->showJSAlert(js_dialog_message_);
            break;
        case JSDIALOGTYPE_CONFIRM:
            status_bar_->showJSConfirm(js_dialog_message_);
            break;
        case JSDIALOGTYPE_PROMPT:
            status_bar_->showJSPrompt(js_dialog_message_, js_dialog_prompt_default_);
            break;
    }
    
    // Return true to handle it ourselves
    return true;
}

void BrowserClient::HandleJSDialogResponse(bool success, const std::string& input) {
    std::lock_guard<std::mutex> lock(js_dialog_mutex_);
    
    if (!js_dialog_active_ || !js_dialog_callback_) {
        return;
    }
    
    LOGB("JS Dialog response: success=" << success << " input=" << input);
    
    // Call the callback
    js_dialog_callback_->Continue(success, input);
    
    // Clean up
    js_dialog_active_ = false;
    js_dialog_callback_ = nullptr;
    
    // Force repaint to clear the dialog
    if (browser_ && browser_->GetHost()) {
        browser_->GetHost()->Invalidate(PET_VIEW);
    }
}

bool BrowserClient::OnFileDialog(CefRefPtr<CefBrowser> browser,
                                 CefDialogHandler::FileDialogMode mode,
                                 const CefString& title,
                                 const CefString& default_file_path,
                                 const std::vector<CefString>& accept_filters,
                                 const std::vector<CefString>& accept_extensions,
                                 const std::vector<CefString>& accept_descriptions,
                                 CefRefPtr<CefFileDialogCallback> callback) {
    LOGB("File Dialog: mode=" << mode << " title=" << title.ToString() << " default_path=" << default_file_path.ToString());
    
    std::lock_guard<std::mutex> lock(file_dialog_mutex_);
    
    file_input_active_ = true;
    file_dialog_callback_ = callback;
    
    if (status_bar_) {
        status_bar_->showFileInput(default_file_path.ToString());
    }
    
    return true;
}

void BrowserClient::HandleFileDialogResponse(const std::string& file_path) {
    std::lock_guard<std::mutex> lock(file_dialog_mutex_);
    
    if (!file_input_active_ || !file_dialog_callback_) {
        return;
    }
    
    LOGB("File Dialog response: file_path=" << file_path);
    
    if (!file_path.empty()) {
        std::vector<CefString> file_paths;
        file_paths.push_back(file_path);
        file_dialog_callback_->Continue(file_paths);
    } else {
        file_dialog_callback_->Cancel();
    }
    
    file_input_active_ = false;
    file_dialog_callback_ = nullptr;
    
    if (browser_ && browser_->GetHost()) {
        browser_->GetHost()->Invalidate(PET_VIEW);
    }
}

bool BrowserClient::OnBeforeDownload(CefRefPtr<CefBrowser> browser,
                                     CefRefPtr<CefDownloadItem> download_item,
                                     const CefString& suggested_name,
                                     CefRefPtr<CefBeforeDownloadCallback> callback) {
    std::lock_guard<std::mutex> lock(download_mutex_);
    
    download_filename_ = suggested_name.ToString();
    download_url_ = download_item->GetURL().ToString();
    download_callback_ = callback;
    download_confirm_active_ = true;
    
    LOGB("Download request: " << download_filename_ << " from " << download_url_);
    
    // Show download confirmation dialog
    std::lock_guard<std::mutex> render_lock(render_mutex_);
    status_bar_->showDownloadConfirm(download_filename_, download_url_);
    
    return true;
}

void BrowserClient::OnDownloadUpdated(CefRefPtr<CefBrowser> browser,
                                      CefRefPtr<CefDownloadItem> download_item,
                                      CefRefPtr<CefDownloadItemCallback> callback) {
    if (download_item->IsComplete()) {
        LOGB("Download complete: " << download_item->GetFullPath().ToString());
        std::lock_guard<std::mutex> lock(render_mutex_);
        status_bar_->showMessage("Download complete: " + download_item->GetFullPath().ToString());
    } else if (download_item->IsCanceled()) {
        LOGB("Download canceled");
    }
}

void BrowserClient::HandleDownloadResponse(bool accept, const std::string& path) {
    std::lock_guard<std::mutex> lock(download_mutex_);
    
    if (!download_confirm_active_ || !download_callback_) {
        return;
    }
    
    if (accept) {
        std::string download_path = path.empty() ? 
            (std::string(getenv("HOME") ? getenv("HOME") : "/tmp") + "/Downloads/" + download_filename_) : 
            path;
        
        LOGB("Download accepted: " << download_path);
        download_callback_->Continue(download_path, false);
    } else {
        LOGB("Download rejected");
    }
    
    // Clean up
    download_confirm_active_ = false;
    download_callback_ = nullptr;
    
    // Force repaint to clear the dialog
    if (browser_ && browser_->GetHost()) {
        browser_->GetHost()->Invalidate(PET_VIEW);
    }
}

void BrowserClient::AddCurrentPageToBookmarks() {
    if (!browser_) return;
    
    std::string url = browser_->GetMainFrame()->GetURL().ToString();
    if (url.empty() || url == "about:blank") return;
    
    bookmarks_manager_.addBookmark(current_page_title_, url);
    
    std::lock_guard<std::mutex> lock(render_mutex_);
    status_bar_->showMessage("📚 Bookmark added: " + current_page_title_);
}

void BrowserClient::SetBookmarksActive(bool active) {
    bookmarks_active_ = active;
    if (active) {
        bookmarks_selected_index_ = 0;
        
        // Build display list
        const auto& bookmarks = bookmarks_manager_.getBookmarks();
        std::vector<std::string> display_list;
        for (const auto& bookmark : bookmarks) {
            display_list.push_back(bookmark.title + " - " + bookmark.url);
        }
        
        std::lock_guard<std::mutex> lock(render_mutex_);
        status_bar_->showBookmarks(display_list, bookmarks_selected_index_);
    } else {
        std::lock_guard<std::mutex> lock(render_mutex_);
        status_bar_->clear();
        
        // Force a repaint
        if (browser_ && browser_->GetHost()) {
            browser_->GetHost()->Invalidate(PET_VIEW);
        }
    }
}

bool BrowserClient::HandleBookmarkNavigation(int direction) {
    if (!bookmarks_active_) return false;
    
    const auto& bookmarks = bookmarks_manager_.getBookmarks();
    if (bookmarks.empty()) return true;
    
    bookmarks_selected_index_ += direction;
    if (bookmarks_selected_index_ < 0) {
        bookmarks_selected_index_ = 0;
    } else if (bookmarks_selected_index_ >= bookmarks.size()) {
        bookmarks_selected_index_ = bookmarks.size() - 1;
    }
    
    // Build display list
    std::vector<std::string> display_list;
    for (const auto& bookmark : bookmarks) {
        display_list.push_back(bookmark.title + " - " + bookmark.url);
    }
    
    std::lock_guard<std::mutex> lock(render_mutex_);
    status_bar_->showBookmarks(display_list, bookmarks_selected_index_);
    
    return true;
}

bool BrowserClient::HandleBookmarkConfirm() {
    if (!bookmarks_active_) return false;
    
    const auto& bookmarks = bookmarks_manager_.getBookmarks();
    LOGB("HandleBookmarkConfirm: bookmarks.size=" << bookmarks.size() << " selected=" << bookmarks_selected_index_);
    
    if (bookmarks.empty() || bookmarks_selected_index_ >= bookmarks.size()) {
        return false;
    }
    
    std::string url = bookmarks[bookmarks_selected_index_].url;
    LOGB("Opening bookmark: " << url);
    
    // Close bookmarks view
    SetBookmarksActive(false);
    
    // Navigate to URL
    if (browser_ && browser_->GetMainFrame()) {
        browser_->GetMainFrame()->LoadURL(url);
    }
    
    return true;
}

bool BrowserClient::HandleBookmarkDelete() {
    if (!bookmarks_active_) return false;
    
    const auto& bookmarks = bookmarks_manager_.getBookmarks();
    if (bookmarks.empty() || bookmarks_selected_index_ >= bookmarks.size()) {
        return false;
    }
    
    bookmarks_manager_.removeBookmark(bookmarks_selected_index_);
    
    // Adjust selection
    const auto& updated_bookmarks = bookmarks_manager_.getBookmarks();
    if (bookmarks_selected_index_ >= updated_bookmarks.size() && bookmarks_selected_index_ > 0) {
        bookmarks_selected_index_--;
    }
    
    // Update display
    std::vector<std::string> display_list;
    for (const auto& bookmark : updated_bookmarks) {
        display_list.push_back(bookmark.title + " - " + bookmark.url);
    }
    
    std::lock_guard<std::mutex> lock(render_mutex_);
    status_bar_->showBookmarks(display_list, bookmarks_selected_index_);
    
    return true;
}

void BrowserClient::SetUserScriptsActive(bool active) {
    user_scripts_active_ = active;
    if (active) {
        user_scripts_selected_index_ = 0;
        
        // Get all available scripts
        std::vector<std::string> script_names = user_scripts_manager_.getAllScriptNames();
        
        std::lock_guard<std::mutex> lock(render_mutex_);
        status_bar_->showUserScripts(script_names, user_scripts_selected_index_);
    } else {
        std::lock_guard<std::mutex> lock(render_mutex_);
        status_bar_->clear();
        
        // Force a repaint
        if (browser_ && browser_->GetHost()) {
            browser_->GetHost()->Invalidate(PET_VIEW);
        }
    }
}

bool BrowserClient::HandleUserScriptNavigation(int direction) {
    if (!user_scripts_active_) return false;
    
    std::vector<std::string> script_names = user_scripts_manager_.getAllScriptNames();
    if (script_names.empty()) return true;
    
    user_scripts_selected_index_ += direction;
    if (user_scripts_selected_index_ < 0) {
        user_scripts_selected_index_ = 0;
    } else if (user_scripts_selected_index_ >= script_names.size()) {
        user_scripts_selected_index_ = script_names.size() - 1;
    }
    
    std::lock_guard<std::mutex> lock(render_mutex_);
    status_bar_->showUserScripts(script_names, user_scripts_selected_index_);
    
    return true;
}

bool BrowserClient::HandleUserScriptConfirm() {
    if (!user_scripts_active_) return false;
    
    std::vector<std::string> script_names = user_scripts_manager_.getAllScriptNames();
    if (script_names.empty() || user_scripts_selected_index_ >= script_names.size()) {
        return false;
    }
    
    std::string script_name = script_names[user_scripts_selected_index_];
    std::string script_content = user_scripts_manager_.getScriptContent(script_name);
    
    LOGB("Injecting user script: " << script_name);
    
    // Close user scripts view
    SetUserScriptsActive(false);
    
    // Execute script
    if (!script_content.empty() && browser_ && browser_->GetMainFrame()) {
        browser_->GetMainFrame()->ExecuteJavaScript(script_content, browser_->GetMainFrame()->GetURL(), 0);
        
        // Show confirmation
        std::lock_guard<std::mutex> lock(render_mutex_);
        status_bar_->showMessage("📜 Script injected: " + script_name);
    }
    
    return true;
}

void BrowserClient::ToggleAutoInjectUserScripts() {
    bool current = user_scripts_manager_.isAutoInjectEnabled();
    user_scripts_manager_.setAutoInject(!current);
    
    LOGB("ToggleAutoInjectUserScripts: was=" << current << " now=" << !current);
    
    std::lock_guard<std::mutex> lock(render_mutex_);
    if (!current) {
        status_bar_->showMessage("📜 User scripts auto-inject: ENABLED");
    } else {
        status_bar_->showMessage("📜 User scripts auto-inject: DISABLED");
    }
}

void BrowserClient::InjectUserScriptsForCurrentPage() {
    if (!browser_ || !browser_->GetMainFrame()) return;
    
    std::string url = browser_->GetMainFrame()->GetURL().ToString();
    std::vector<std::string> matching_scripts = user_scripts_manager_.getMatchingScripts(url);
    
    for (const auto& script_name : matching_scripts) {
        std::string script_content = user_scripts_manager_.getScriptContent(script_name);
        if (!script_content.empty()) {
            LOGB("Auto-injecting user script: " << script_name << " for URL: " << url);
            browser_->GetMainFrame()->ExecuteJavaScript(script_content, browser_->GetMainFrame()->GetURL(), 0);
        }
    }
}

// Hint Mode implementation
void BrowserClient::ActivateHintMode() {
    if (!browser_ || !browser_->GetMainFrame()) {
        LOGB("ActivateHintMode: browser or frame is null");
        return;
    }
    
    // Read hint mode JavaScript
    std::ifstream file("hint_mode.js");
    if (!file.is_open()) {
        LOGB("Failed to load hint_mode.js - file not found or can't open");
        // Try with full debug info
        char cwd[1024];
        if (getcwd(cwd, sizeof(cwd)) != NULL) {
            LOGB("Current working directory: " << cwd);
        }
        return;
    }
    
    std::stringstream buffer;
    buffer << file.rdbuf();
    std::string hint_js = buffer.str();
    
    if (hint_js.empty()) {
        LOGB("hint_mode.js is empty!");
        return;
    }
    
    LOGB("Activating hint mode, JS size: " << hint_js.size() << " bytes");
    
    hint_mode_active_ = true;
    hint_count_ = 0;
    
    browser_->GetMainFrame()->ExecuteJavaScript(hint_js, "", 0);
    
    // Show hint input in status bar
    if (status_bar_) {
        status_bar_->showHintInput("", hint_count_);
    } else {
        LOGB("Status bar is null!");
    }
}

void BrowserClient::SetHintModeActive(bool active) {
    hint_mode_active_ = active;
    if (!active && browser_ && browser_->GetMainFrame()) {
        // Cleanup hints
        browser_->GetMainFrame()->ExecuteJavaScript(
            "if (window.__brow6el_hints) { window.__brow6el_hints.cleanup(); }", "", 0);
    }
}

void BrowserClient::HandleHintSelection(const std::string& hint) {
    if (!browser_ || !browser_->GetMainFrame() || hint.empty()) return;
    
    std::string js = "if (window.__brow6el_hints) { window.__brow6el_hints.select('" + hint + "'); }";
    browser_->GetMainFrame()->ExecuteJavaScript(js, "", 0);
    
    hint_mode_active_ = false;
}

// Mouse Emulation Mode implementation
void BrowserClient::ActivateMouseEmuMode() {
    if (!browser_ || !browser_->GetMainFrame()) {
        LOGB("ActivateMouseEmuMode: browser or frame is null");
        return;
    }
    
    // Read mouse emulation JavaScript
    std::ifstream file("mouse_emu.js");
    if (!file.is_open()) {
        LOGB("Failed to load mouse_emu.js");
        return;
    }
    
    std::stringstream buffer;
    buffer << file.rdbuf();
    std::string mouse_emu_js = buffer.str();
    
    if (mouse_emu_js.empty()) {
        LOGB("mouse_emu.js is empty!");
        return;
    }
    
    LOGB("Activating mouse emulation mode, JS size: " << mouse_emu_js.size() << " bytes");
    
    mouse_emu_mode_active_ = true;
    
    browser_->GetMainFrame()->ExecuteJavaScript(mouse_emu_js, "", 0);
    
    // Don't show status message - let select elements and other UI use the status bar normally
    // The yellow circle is enough visual feedback that mouse emu is active
}

void BrowserClient::SetMouseEmuModeActive(bool active) {
    mouse_emu_mode_active_ = active;
    if (!active && browser_ && browser_->GetMainFrame()) {
        // Cleanup mouse cursor
        browser_->GetMainFrame()->ExecuteJavaScript(
            "if (window.__brow6el_mouse_emu) { window.__brow6el_mouse_emu.cleanup(); }", "", 0);
    }
}

void BrowserClient::HandleMouseEmuKey(const std::string& key) {
    if (!browser_ || !browser_->GetMainFrame()) return;
    
    std::string js = "if (window.__brow6el_mouse_emu) { window.__brow6el_mouse_emu.handleKey('" + key + "'); }";
    browser_->GetMainFrame()->ExecuteJavaScript(js, "", 0);
}

void BrowserClient::HandleMouseEmuClick() {
    if (!browser_ || !browser_->GetHost()) return;
    
    // Use the stored position to send a real CEF mouse click
    CefMouseEvent mouse_event;
    mouse_event.x = mouse_emu_x_;
    mouse_event.y = mouse_emu_y_;
    mouse_event.modifiers = 0;
    
    LOGB("Mouse emu click at " << mouse_emu_x_ << "," << mouse_emu_y_);
    
    // Send mouse move first
    browser_->GetHost()->SendMouseMoveEvent(mouse_event, false);
    
    // For proper interaction with UI elements like comboboxes, 
    // just send a single click event rather than down+up sequence
    browser_->GetHost()->SendMouseClickEvent(mouse_event, MBT_LEFT, false, 1); // Mouse down
    
    // Small delay before mouse up to allow element to process the click
    usleep(50000); // 50ms delay
    
    browser_->GetHost()->SendMouseClickEvent(mouse_event, MBT_LEFT, true, 1);  // Mouse up
    
    // Still trigger JS visual feedback
    std::string js = "if (window.__brow6el_mouse_emu) { window.__brow6el_mouse_emu.flashClick(); }";
    browser_->GetMainFrame()->ExecuteJavaScript(js, "", 0);
}

void BrowserClient::HandleMouseEmuPosition(int x, int y) {
    mouse_emu_x_ = x;
    mouse_emu_y_ = y;
}
