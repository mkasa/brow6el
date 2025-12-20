#include "browser_client.h"
#include <iostream>
#include <fstream>
#include <sstream>

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
    
    // Skip rendering when URL input, console, popup confirm, JS dialog, or download confirm is active
    if (url_input_active_ || console_active_ || popup_confirm_active_ || js_dialog_active_ || download_confirm_active_) {
        return;
    }
    
    if (type == PET_VIEW && renderer_ && buffer && width > 0 && height > 0) {
        try {
            renderer_->render(buffer, width, height, false);
            
            // Redraw status bar after sixel render (so it stays visible)
            if (status_bar_) {
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
        
        // Clear status bar on new page load
        if (status_bar_) {
            std::lock_guard<std::mutex> lock(render_mutex_);
            status_bar_->clear();
        }
        current_options_.clear();
        
        LOGB("OnLoadEnd: injecting select detector...");
        injectSelectDetector();
        LOGB("OnLoadEnd: injection complete");
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
    
    return false; // Show other console messages
}

void BrowserClient::OnTitleChange(CefRefPtr<CefBrowser> browser, const CefString& title) {
    std::string page_title = title.ToString();
    std::string window_title = "Brow6el - " + page_title;
    std::cout << "\033]0;" << window_title << "\007" << std::flush;
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
    
    // Update selection
    current_selected_index_ += direction;
    if (current_selected_index_ < 0) {
        current_selected_index_ = 0;
    }
    if (current_selected_index_ >= (int)current_options_.size()) {
        current_selected_index_ = current_options_.size() - 1;
    }
    
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
