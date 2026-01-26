#include "browser_client.h"
#include "clipboard.h"
#include "input_handler.h"
#include "kitty_renderer.h"
#include "profile_config.h"
#include "sixel_renderer.h"
#include "include/wrapper/cef_closure_task.h"
#include <fstream>
#include <iostream>
#include <sstream>
#include <thread>
#include <chrono>
#include <unistd.h>
#include <cmath>

#define LOGB(msg)                                                              \
  do {                                                                         \
    std::ofstream log("/tmp/brow6el_debug.log", std::ios::app);                \
    log << msg << std::endl;                                                   \
  } while (0)

// Helper function to get JS file path relative to executable
static std::string GetJsFilePath(const std::string& filename) {
  char exe_path[1024];
  ssize_t len = readlink("/proc/self/exe", exe_path, sizeof(exe_path) - 1);
  if (len != -1) {
    exe_path[len] = '\0';
    std::string exe_dir = std::string(exe_path);
    size_t last_slash = exe_dir.find_last_of('/');
    if (last_slash != std::string::npos) {
      exe_dir = exe_dir.substr(0, last_slash);
      return exe_dir + "/" + filename;
    }
  }
  // Fallback to current directory
  return filename;
}

bool BrowserClient::IsKittyRenderer() const {
  return dynamic_cast<KittyRenderer*>(renderer_.get()) != nullptr;
}

CefRefPtr<CefRenderHandler> BrowserClient::GetRenderHandler() {
  return this;
}
CefRefPtr<CefLifeSpanHandler> BrowserClient::GetLifeSpanHandler() {
  return this;
}
CefRefPtr<CefLoadHandler> BrowserClient::GetLoadHandler() {
  return this;
}
CefRefPtr<CefDisplayHandler> BrowserClient::GetDisplayHandler() {
  return this;
}
CefRefPtr<CefJSDialogHandler> BrowserClient::GetJSDialogHandler() {
  return this;
}
CefRefPtr<CefDownloadHandler> BrowserClient::GetDownloadHandler() {
  return this;
}
CefRefPtr<CefDialogHandler> BrowserClient::GetDialogHandler() {
  return this;
}
CefRefPtr<CefRequestHandler> BrowserClient::GetRequestHandler() {
  return static_cast<CefRequestHandler*>(this);
}

BrowserClient::BrowserClient(int width, int height, int cell_width,
                             int cell_height, bool supports_sixel,
                             bool supports_kitty)
    : width_(width), height_(height), cell_width_(cell_width),
      cell_height_(cell_height), supports_sixel_(supports_sixel),
      supports_kitty_(supports_kitty), is_closing_(false),
      current_selected_index_(-1) {

  // Log cell dimensions using direct file write
  std::ofstream log("/tmp/brow6el_debug.log", std::ios::app);
  if (log) {
    log << "BrowserClient created: " << width << "x" << height
        << " pixels, cell: " << cell_width << "x" << cell_height << std::endl;
    log.close();
  }

  // Select renderer based on config AND terminal support
  auto &config = ProfileConfig::getInstance();
  std::string graphics_protocol = config.getGraphicsProtocol();
  
  if (graphics_protocol == "kitty" && supports_kitty) {
    renderer_ = std::make_unique<KittyRenderer>(width, height, cell_width, cell_height);
  } else if (graphics_protocol == "kitty" && !supports_kitty && supports_sixel) {
    // Fallback to sixel if kitty not supported
    std::cerr << "Warning: Kitty protocol not supported, falling back to Sixel" << std::endl;
    renderer_ = std::make_unique<SixelRenderer>(width, height, cell_width, cell_height);
  } else if (graphics_protocol == "sixel" && supports_sixel) {
    renderer_ = std::make_unique<SixelRenderer>(width, height, cell_width, cell_height);
  } else if (graphics_protocol == "sixel" && !supports_sixel && supports_kitty) {
    // Fallback to kitty if sixel not supported
    std::cerr << "Warning: Sixel not supported, falling back to Kitty protocol" << std::endl;
    renderer_ = std::make_unique<KittyRenderer>(width, height, cell_width, cell_height);
  } else {
    // Last resort: try sixel (will fail later if not supported)
    renderer_ = std::make_unique<SixelRenderer>(width, height, cell_width, cell_height);
  }
  
  status_bar_ = std::make_unique<StatusBar>();
}

void BrowserClient::GetViewRect(CefRefPtr<CefBrowser> browser, CefRect &rect) {
  rect.x = 0;
  rect.y = 0;
  rect.width = width_;
  rect.height = height_;
}

void BrowserClient::Resize(int width, int height, int cell_width, int cell_height) {
  std::lock_guard<std::mutex> lock(render_mutex_);
  
  width_ = width;
  height_ = height;
  cell_width_ = cell_width;
  cell_height_ = cell_height;

  // Recalculate auto zoom if enabled
  ProfileConfig &config = ProfileConfig::getInstance();
  std::string behavior = config.getDefaultZoomBehavior();
  
  if (behavior == "auto" && browser_) {
    // Auto-calculate zoom based on:
    // 1. Terminal cell size (DPI adaptation)
    // 2. Rendering resolution (window size adaptation)
    
    const double reference_cell_height = 20.0;
    const double reference_cell_width = 10.0;
    const double reference_width = 1600.0; // Reference resolution
    const double reference_height = 900.0;
    
    // Factor 1: Cell size ratio (DPI)
    double height_ratio = cell_height_ / reference_cell_height;
    double width_ratio = cell_width_ / reference_cell_width;
    double cell_zoom = (height_ratio * 0.7) + (width_ratio * 0.3);
    
    // Factor 2: Resolution ratio (window size)
    double res_height_ratio = height / reference_height;
    double res_width_ratio = width / reference_width;
    double res_zoom = (res_height_ratio * 0.5) + (res_width_ratio * 0.5);
    
    // Combine both factors: 60% cell size, 40% resolution
    double zoom_level = (cell_zoom * 0.6) + (res_zoom * 0.4);
    
    // Apply bounds (0.25x to 3.0x)
    if (zoom_level < 0.25) zoom_level = 0.25;
    if (zoom_level > 3.0) zoom_level = 3.0;
    
    // Check for site-specific override
    std::string url = browser_->GetMainFrame()->GetURL().ToString();
    std::string domain;
    size_t proto = url.find("://");
    if (proto != std::string::npos) {
      size_t start = proto + 3;
      size_t end = url.find("/", start);
      domain = (end != std::string::npos) ? url.substr(start, end - start) : url.substr(start);
      size_t colon = domain.find(":");
      if (colon != std::string::npos) {
        domain = domain.substr(0, colon);
      }
    }
    
    // Site-specific zoom overrides auto-calculated zoom
    if (!domain.empty()) {
      double site_zoom = config.getSiteZoomLevel(domain);
      if (site_zoom != config.getZoomLevel()) {
        zoom_level = site_zoom;
      }
    }
    
    // Apply zoom
    double cef_zoom = std::log2(zoom_level);
    browser_->GetHost()->SetZoomLevel(cef_zoom);
    if (input_handler_) {
      input_handler_->setZoomLevel(cef_zoom);
    }
  }

  // Recreate the renderer with new dimensions AND respect terminal support
  std::string graphics_protocol = config.getGraphicsProtocol();
  
  if (graphics_protocol == "kitty" && supports_kitty_) {
    renderer_ = std::make_unique<KittyRenderer>(width, height, cell_width_, cell_height_);
  } else if (graphics_protocol == "kitty" && !supports_kitty_ && supports_sixel_) {
    renderer_ = std::make_unique<SixelRenderer>(width, height, cell_width_, cell_height_);
  } else if (graphics_protocol == "sixel" && supports_sixel_) {
    renderer_ = std::make_unique<SixelRenderer>(width, height, cell_width_, cell_height_);
  } else if (graphics_protocol == "sixel" && !supports_sixel_ && supports_kitty_) {
    renderer_ = std::make_unique<KittyRenderer>(width, height, cell_width_, cell_height_);
  } else {
    renderer_ = std::make_unique<SixelRenderer>(width, height, cell_width_, cell_height_);
  }
  
  LOGB("Browser resized to " << width << "x" << height << ", cells=" << cell_width << "x" << cell_height);
}

void BrowserClient::OnPaint(CefRefPtr<CefBrowser> browser,
                            PaintElementType type, const RectList &dirtyRects,
                            const void *buffer, int width, int height) {
  std::lock_guard<std::mutex> lock(render_mutex_);

  // Skip rendering when URL input, console, popup confirm, JS dialog, file
  // input, download confirm, auth dialog, bookmarks, user scripts, or download manager is
  // active Note: hint_mode_active and mouse_emu_mode_active are NOT in this
  // list because they use JS overlays that need the page visible
  if (url_input_active_ || console_active_ || popup_confirm_active_ ||
      js_dialog_active_ || file_input_active_ || download_confirm_active_ ||
      auth_dialog_active_ || bookmarks_active_ || user_scripts_active_ || 
      download_manager_active_) {
    return;
  }

  if (type == PET_VIEW && renderer_ && buffer && width > 0 && height > 0) {
    try {
      // Force full render on first 3 paints to avoid black tiles on startup
      bool force_render = false;
      if (paint_count_ < 3) {
        paint_count_++;
        force_render = true;
      }

      // Force full render if requested after OnLoadEnd
      if (force_next_paint_) {
        force_next_paint_ = false;
        force_render = true;
      }

      // Check if status bar requested a full redraw (e.g., after closing)
      if (status_bar_ && status_bar_->IsRedrawRequested()) {
        status_bar_->ClearRedrawRequest();
        force_render = true;
        
        FILE* log = fopen("/tmp/kitty_render.log", "a");
        if (log) {
          fprintf(log, "[CPP] ForceFullRender triggered by status_bar redraw request\n");
          fclose(log);
        }
      }

      if (force_render) {
        FILE* log = fopen("/tmp/kitty_render.log", "a");
        if (log) {
          fprintf(log, "[CPP] Calling forceFullRender (paint_count=%d)\n", paint_count_);
          fclose(log);
        }
        renderer_->forceFullRender();
      }

      // Convert CEF RectList to std::vector<CefRect>
      std::vector<CefRect> rects;
      for (size_t i = 0; i < dirtyRects.size(); i++) {
        rects.push_back(dirtyRects[i]);
      }

      renderer_->render(buffer, width, height, false, rects);

      // Redraw status bar after sixel render (so it stays visible)
      // Skip for hint mode (which has its own yellow status bar)
      // Skip for search mode (which has its own search bar)
      // Allow for mouse emu mode since it doesn't use status bar after initial
      // activation
      if (status_bar_ && !hint_mode_active_ && !search_active_) {
        status_bar_->redraw();
      }
    } catch (const std::exception &e) {
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

  // Cancel any pending callbacks to prevent crashes
  {
    std::lock_guard<std::mutex> lock(js_dialog_mutex_);
    if (js_dialog_callback_) {
      js_dialog_callback_->Continue(false, "");
      js_dialog_callback_ = nullptr;
    }
    js_dialog_active_ = false;
  }

  {
    std::lock_guard<std::mutex> lock(file_dialog_mutex_);
    if (file_dialog_callback_) {
      file_dialog_callback_->Cancel();
      file_dialog_callback_ = nullptr;
    }
    file_input_active_ = false;
  }

  {
    std::lock_guard<std::mutex> lock(download_mutex_);
    // download_callback_ doesn't need to be called if rejected
    download_callback_ = nullptr;
    download_confirm_active_ = false;
  }

  if (status_bar_) {
    std::lock_guard<std::mutex> lock(render_mutex_);
    status_bar_->clear(false); // Don't redraw title on exit
  }
  LOGB("Browser closing");
}

bool BrowserClient::OnBeforePopup(
    CefRefPtr<CefBrowser> browser, CefRefPtr<CefFrame> frame, int popup_id,
    const CefString &target_url, const CefString &target_frame_name,
    CefLifeSpanHandler::WindowOpenDisposition target_disposition,
    bool user_gesture, const CefPopupFeatures &popupFeatures,
    CefWindowInfo &windowInfo, CefRefPtr<CefClient> &client,
    CefBrowserSettings &settings, CefRefPtr<CefDictionaryValue> &extra_info,
    bool *no_javascript_access) {
  std::string url = target_url.ToString();
  LOGB("OnBeforePopup: Intercepting popup to " << url);

  // Store the URL and show confirmation dialog
  SetPopupConfirmActive(true, url);

  // Return true to cancel the popup creation
  return true;
}

void BrowserClient::OnLoadEnd(CefRefPtr<CefBrowser> browser,
                              CefRefPtr<CefFrame> frame, int httpStatusCode) {
  if (frame->IsMain()) {
    std::string url = frame->GetURL().ToString();
    LOGB("OnLoadEnd: url=" << url << " status=" << httpStatusCode);

    // Reset paint count to force first 3 paints (important for Kitty renderer)
    paint_count_ = 0;
    
    // Force full render on next paint (including first load to avoid race
    // conditions)
    force_next_paint_ = true;
    first_load_complete_ = true;

    // Apply zoom based on config
    ProfileConfig &config = ProfileConfig::getInstance();
    std::string behavior = config.getDefaultZoomBehavior();
    double zoom_level = 0.0; // Default to 1.0 (no zoom)
    
    if (behavior == "auto") {
      // Auto-calculate zoom based on:
      // 1. Terminal cell size (DPI adaptation)
      // 2. Rendering resolution (window size adaptation)
      const double reference_cell_height = 20.0;
      const double reference_cell_width = 10.0;
      const double reference_width = 1600.0;
      const double reference_height = 900.0;
      
      // Factor 1: Cell size ratio (DPI)
      double height_ratio = cell_height_ / reference_cell_height;
      double width_ratio = cell_width_ / reference_cell_width;
      double cell_zoom = (height_ratio * 0.7) + (width_ratio * 0.3);
      
      // Factor 2: Resolution ratio (window size)
      double res_height_ratio = height_ / reference_height;
      double res_width_ratio = width_ / reference_width;
      double res_zoom = (res_height_ratio * 0.5) + (res_width_ratio * 0.5);
      
      // Combine both factors: 60% cell size, 40% resolution
      zoom_level = (cell_zoom * 0.6) + (res_zoom * 0.4);
      
      // Apply bounds (0.25x to 3.0x)
      if (zoom_level < 0.25) zoom_level = 0.25;
      if (zoom_level > 3.0) zoom_level = 3.0;
      
      // Check for site-specific override
      std::string domain;
      size_t proto = url.find("://");
      if (proto != std::string::npos) {
        size_t start = proto + 3;
        size_t end = url.find("/", start);
        domain = (end != std::string::npos) ? url.substr(start, end - start) : url.substr(start);
        size_t colon = domain.find(":");
        if (colon != std::string::npos) {
          domain = domain.substr(0, colon);
        }
      }
      
      // Site-specific zoom overrides auto-calculated zoom
      if (!domain.empty()) {
        double site_zoom = config.getSiteZoomLevel(domain);
        if (site_zoom != config.getZoomLevel()) {
          // Site has custom zoom, use it instead
          zoom_level = site_zoom;
        }
      }
      
    } else if (behavior == "fixed") {
      // Check for site-specific zoom first
      std::string domain;
      size_t proto = url.find("://");
      if (proto != std::string::npos) {
        size_t start = proto + 3;
        size_t end = url.find("/", start);
        domain = (end != std::string::npos) ? url.substr(start, end - start) : url.substr(start);
        // Remove port if present
        size_t colon = domain.find(":");
        if (colon != std::string::npos) {
          domain = domain.substr(0, colon);
        }
      }
      
      if (!domain.empty()) {
        zoom_level = config.getSiteZoomLevel(domain);
      } else {
        zoom_level = config.getZoomLevel();
      }
    }
    // else behavior == "none", zoom_level stays 0.0 (no zoom applied)
    
    // Apply zoom if needed
    if (zoom_level > 0.0 && behavior != "none") {
      // Convert from multiplier to CEF zoom level
      // CEF uses log scale: level = log2(zoom_multiplier)
      double cef_zoom = std::log2(zoom_level);
      browser_->GetHost()->SetZoomLevel(cef_zoom);
      // Update InputHandler's tracked zoom level
      if (input_handler_) {
        input_handler_->setZoomLevel(cef_zoom);
      }
    }

    // Invalidate to trigger redraw
    if (browser_) {
      browser_->GetHost()->Invalidate(PET_VIEW);
    }

    // Clear modes on navigation and request switch to STANDARD mode
    hint_mode_active_ = false;
    mouse_emu_mode_active_ = false;
    mode_switch_requested_ = true;
    switch_to_insert_mode_ = false; // Switch to STANDARD mode

    // Clear status bar on new page load only if something is showing
    if (status_bar_) {
      std::lock_guard<std::mutex> lock(render_mutex_);
      // Only clear if we're showing something (not just blank)
      if (!current_options_.empty() || url_input_active_ || console_active_ ||
          popup_confirm_active_ || js_dialog_active_ || file_input_active_ ||
          download_confirm_active_ || bookmarks_active_) {
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

void BrowserClient::OnLoadError(CefRefPtr<CefBrowser> browser,
                                CefRefPtr<CefFrame> frame, ErrorCode errorCode,
                                const CefString &errorText,
                                const CefString &failedUrl) {
  if (frame->IsMain() && errorCode != ERR_ABORTED) {
    LOGB("Load error: " << errorText.ToString());
  }
}

bool BrowserClient::OnConsoleMessage(CefRefPtr<CefBrowser> browser,
                                     cef_log_severity_t level,
                                     const CefString &message,
                                     const CefString &source, int line) {
  std::string msg = message.ToString();

  // Log ALL console messages for debugging
  FILE *log = fopen("/tmp/brow6el_debug.log", "a");
  if (log) {
    fprintf(log, "Console: %s\n", msg.c_str());
    fclose(log);
  }

  // Store console messages for console view
  {
    std::lock_guard<std::mutex> lock(console_mutex_);

    // Skip internal messages if configured to hide them
    bool is_internal = msg.find("[Brow6el]") == 0;
    bool should_show = !is_internal || show_internal_console_logs_;

    if (should_show) {
      // Format message with severity
      std::string level_str;
      switch (level) {
      case LOGSEVERITY_DEBUG:
        level_str = "[DEBUG] ";
        break;
      case LOGSEVERITY_INFO:
        level_str = "[INFO] ";
        break;
      case LOGSEVERITY_WARNING:
        level_str = "[WARN] ";
        break;
      case LOGSEVERITY_ERROR:
        level_str = "[ERROR] ";
        break;
      default:
        level_str = "[LOG] ";
        break;
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
  }

  // Check for our custom messages
  if (msg.find("BROW6EL_SELECT_") == 0) {
    parseSelectMessage(msg);
    return true; // Suppress console output
  }

  // Handle DOM change notifications from MutationObserver
  // Used by hint mode and mouse emulation to trigger re-render after overlay changes
  if (msg.find("[Brow6el] DOM_CHANGED") == 0) {
    if (browser_ && browser_->GetHost()) {
      // Add small delay to let CEF finish rendering DOM changes into paint buffer
      // This ensures overlays are included in the frame
      CefRefPtr<CefBrowser> browser = browser_;
      std::thread([browser]() {
        std::this_thread::sleep_for(std::chrono::milliseconds(16)); // ~1 frame at 60fps
        if (browser && browser->GetHost()) {
          browser->GetHost()->Invalidate(PET_VIEW);
        }
      }).detach();
    }
    return true; // Suppress console output
  }

  // For Kitty: handle userscript completion notification
  if (msg.find("[Brow6el] USERSCRIPT_COMPLETE") == 0) {
    if (IsKittyRenderer() && browser_ && browser_->GetHost()) {
      CefRefPtr<CefBrowser> browser = browser_;
      std::thread([browser]() {
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
        if (browser && browser->GetHost()) {
          browser->GetHost()->Invalidate(PET_VIEW);
        }
      }).detach();
    }
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
    } catch (const std::exception &e) {
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
    } catch (const std::exception &e) {
      LOGB("Failed to parse mouse emu position: " << e.what());
    }
    return true;
  }
  if (msg.find("[Brow6el] MOUSE_EMU_CLICK:") == 0) {
    // Parse element info: [Brow6el]
    // MOUSE_EMU_CLICK:tagName:type:isSelect:isInput:isCheckboxOrRadio
    try {
      std::string info = msg.substr(26);
      std::vector<std::string> parts;
      size_t pos = 0;
      while ((pos = info.find(':')) != std::string::npos) {
        parts.push_back(info.substr(0, pos));
        info.erase(0, pos + 1);
      }
      parts.push_back(info); // Add last part

      if (parts.size() >= 4) {
        std::string tagName = parts[0];
        std::string type = parts[1];
        bool isSelect = (parts[2] == "true");
        bool isInput = (parts[3] == "true");
        bool isCheckboxOrRadio = (parts.size() >= 5 && parts[4] == "true");

        LOGB("Mouse emu clicked: " << tagName << " type=" << type << " select="
                                   << isSelect << " input=" << isInput
                                   << " checkbox/radio=" << isCheckboxOrRadio);

        // Determine mode to switch to after click
        // SELECT elements (combobox) → STANDARD mode (handled by status bar)
        // INPUT/TEXTAREA (but NOT checkbox/radio) → INSERT mode (for typing)
        // Checkbox/radio → stay in MOUSE mode
        // Other elements → stay in MOUSE mode

        if (isSelect) {
          // Combobox - switch to STANDARD mode
          mouse_emu_mode_active_ = false;
          SetMouseEmuModeActive(false);
          ForceFullRedraw();
          mode_switch_requested_ = true;
          switch_to_insert_mode_ = false; // Switch to STANDARD
          if (browser_) {
            browser_->GetHost()->Invalidate(PET_VIEW);
          }
        } else if (isInput && !isCheckboxOrRadio) {
          // Text input field - switch to INSERT mode
          // Checkbox and radio buttons stay in MOUSE mode
          mouse_emu_mode_active_ = false;
          SetMouseEmuModeActive(false);
          ForceFullRedraw();
          mode_switch_requested_ = true;
          switch_to_insert_mode_ = true; // Switch to INSERT
          if (browser_) {
            browser_->GetHost()->Invalidate(PET_VIEW);
          }
        }
        // If isCheckboxOrRadio, we don't switch modes - stay in MOUSE
      }
    } catch (const std::exception &e) {
      LOGB("Failed to parse mouse emu click info: " << e.what());
    }

    // Always send the click event
    HandleMouseEmuClick();
    return true;
  }
  if (msg.find("[Brow6el] MOUSE_EMU_CLICK") == 0) {
    // JavaScript determined this is not a select/input, so send real click
    HandleMouseEmuClick();
    return true;
  }
  if (msg.find("[Brow6el] MOUSE_EMU_DRAG_START") == 0) {
    HandleMouseEmuDragStart();
    return true;
  }
  if (msg.find("[Brow6el] MOUSE_EMU_DRAG_END") == 0) {
    HandleMouseEmuDragEnd();
    return true;
  }
  if (msg.find("[Brow6el] MOUSE_EMU_FOCUS:") == 0) {
    // Parse element info: [Brow6el]
    // MOUSE_EMU_FOCUS:tagName:type:isSelect:isInput:isCheckboxOrRadio
    try {
      std::string info = msg.substr(26);
      std::vector<std::string> parts;
      size_t pos = 0;
      while ((pos = info.find(':')) != std::string::npos) {
        parts.push_back(info.substr(0, pos));
        info.erase(0, pos + 1);
      }
      parts.push_back(info); // Add last part

      if (parts.size() >= 4) {
        std::string tagName = parts[0];
        std::string type = parts[1];
        bool isSelect = (parts[2] == "true");
        bool isInput = (parts[3] == "true");

        LOGB("Mouse emu focused: " << tagName << " type=" << type << " select="
                                   << isSelect << " input=" << isInput);

        // Determine mode to switch to after focus
        // SELECT elements (combobox) → STANDARD mode (handled by status bar)
        // TEXT INPUT/TEXTAREA → INSERT mode (for typing)

        if (isSelect) {
          // Combobox - switch to STANDARD mode
          mouse_emu_mode_active_ = false;
          SetMouseEmuModeActive(false);
          ForceFullRedraw();
          mode_switch_requested_ = true;
          switch_to_insert_mode_ = false; // Switch to STANDARD
          if (browser_) {
            browser_->GetHost()->Invalidate(PET_VIEW);
          }
        } else if (isInput) {
          // Text input field - switch to INSERT mode
          mouse_emu_mode_active_ = false;
          SetMouseEmuModeActive(false);
          ForceFullRedraw();
          mode_switch_requested_ = true;
          switch_to_insert_mode_ = true; // Switch to INSERT
          if (browser_) {
            browser_->GetHost()->Invalidate(PET_VIEW);
          }
        }
      }
    } catch (const std::exception &e) {
      LOGB("Failed to parse mouse emu focus info: " << e.what());
    }
    return true;
  }
  if (msg.find("[Brow6el] MOUSE_EMU_FOCUS") == 0) {
    // Old handler for backward compatibility
    LOGB("Mouse emu focused element: " << msg);
    return true;
  }
  if (msg.find("[Brow6el] VISUAL_MODE_COPY:") == 0) {
    // Copy selected text from visual mode
    std::string text = msg.substr(27); // Skip "[Brow6el] VISUAL_MODE_COPY:"
    if (!text.empty()) {
      Clipboard::copyToClipboard(text);
      LOGB("Copied selection: " << text.length() << " characters");
    }
    // Visual mode will be exited by the input handler (y key)
    return true;
  }
  if (msg.find("[Brow6el] VISUAL_MODE_CLOSED") == 0) {
    // Visual mode closed - just log it
    // Mode switch is handled by input handler (ESC key)
    LOGB("Visual mode closed");
    return true;
  }
  if (msg.find("[Brow6el] PHYSICAL_MOUSE_TEXT_INPUT") == 0) {
    // Physical mouse clicked on a text input - switch to INSERT mode
    LOGB("Physical mouse clicked text input - switching to INSERT mode");
    mode_switch_requested_ = true;
    switch_to_insert_mode_ = true;
    if (browser_) {
      browser_->GetHost()->Invalidate(PET_VIEW);
    }
    return true;
  }
  if (msg.find("[Brow6el] MOUSE_EMU_GRID_ACTIVE:") == 0) {
    // Grid mode activated
    grid_mode_active_ = true;
    // MutationObserver will trigger Invalidate via DOM_CHANGED
    return true;
  }
  if (msg.find("[Brow6el] MOUSE_EMU_GRID_CLOSED") == 0) {
    // Grid mode closed
    grid_mode_active_ = false;
    ForceFullRedraw();
    if (browser_) {
      browser_->GetHost()->Invalidate(PET_VIEW);
    }
    return true;
  }
  if (msg.find("[Brow6el] MOUSE_EMU_GRID_HANDLED") == 0) {
    // Grid mode handled a key (ESC or Backspace)
    grid_mode_handled_key_ = true;
    return true;
  }
  if (msg.find("[Brow6el] MOUSE_EMU_") == 0) {
    // Suppress other mouse emu debug messages
    return true;
  }

  // Check for inspect mode messages
  if (msg.find("[Brow6el] INSPECT_MODE:") == 0) {
    // Parse inspect mode status: [Brow6el] INSPECT_MODE:ON or OFF
    std::string status = msg.substr(23);
    LOGB("Inspect mode: " << status);
    return true;
  }
  if (msg.find("[Brow6el] INSPECT_MODE_") == 0) {
    // Suppress inspect mode debug messages
    return true;
  }

  return false; // Show other console messages
}

void BrowserClient::OnTitleChange(CefRefPtr<CefBrowser> browser,
                                  const CefString &title) {
  std::string page_title = title.ToString();
  current_page_title_ = page_title; // Store for bookmarks
  std::string window_title = "brow6el - " + page_title;
  LOGB("OnTitleChange called: " << page_title);

  // Use printf and fflush for immediate output
  printf("\033]0;%s\007", window_title.c_str());
  fflush(stdout);

  // Show title in status bar
  if (status_bar_) {
    status_bar_->showTitle(page_title, input_mode_);
  }
}

void BrowserClient::injectSelectDetector() {
  if (!browser_)
    return;

  // Read JavaScript file from executable directory
  std::string js_path = GetJsFilePath("select_detector.js");
  std::ifstream js_file(js_path);
  if (!js_file.is_open()) {
    LOGB("Warning: Could not load select_detector.js from " << js_path);
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

void BrowserClient::parseSelectMessage(const std::string &message) {
  if (!status_bar_)
    return;

  if (message.find("BROW6EL_SELECT_FOCUSED:") == 0) {
    // Extract JSON data
    std::string json = message.substr(23); // Skip "BROW6EL_SELECT_FOCUSED:"

    // Simple JSON parsing (find options array and selectedIndex)
    std::vector<std::string> options;
    int selected_index = 0;

    // Extract options (looking for ["text1","text2",...])
    size_t options_start = json.find("[");
    size_t options_end = json.find("]");
    if (options_start != std::string::npos &&
        options_end != std::string::npos) {
      std::string options_str =
          json.substr(options_start + 1, options_end - options_start - 1);

      // Parse each option
      size_t pos = 0;
      while (pos < options_str.length()) {
        size_t quote_start = options_str.find('"', pos);
        if (quote_start == std::string::npos)
          break;

        size_t quote_end = options_str.find('"', quote_start + 1);
        if (quote_end == std::string::npos)
          break;

        std::string option =
            options_str.substr(quote_start + 1, quote_end - quote_start - 1);
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
      current_options_ = options;               // Save for later updates
      current_selected_index_ = selected_index; // Save current index

      std::lock_guard<std::mutex> lock(render_mutex_);
      status_bar_->showComboboxOptions(options, selected_index);
    }

  } else if (message == "BROW6EL_SELECT_BLURRED") {
    {
      std::lock_guard<std::mutex> lock(render_mutex_);
      status_bar_->clear();
    }
    current_options_.clear();     // Clear saved options
    current_selected_index_ = -1; // Reset index

  } else if (message.find("BROW6EL_SELECT_CHANGED:") == 0) {
    // Extract new selected index
    int new_index =
        std::atoi(message.c_str() + 23); // Skip "BROW6EL_SELECT_CHANGED:"

    FILE *log = fopen("/tmp/brow6el_debug.log", "a");
    if (log) {
      fprintf(log, "SELECT_CHANGED: new_index=%d, have_options=%d\n", new_index,
              (int)current_options_.size());
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
                     std::to_string(current_selected_index_) +
                     "; "
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
                     std::to_string(current_selected_index_) +
                     "; "
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

void BrowserClient::ExecuteJavaScript(const std::string &code) {
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
    std::string wrapped_code = "(function() { "
                               "  try { "
                               "    var result = (0, eval)(\"" +
                               escaped_code +
                               "\"); "
                               "    console.log('← ' + (result !== undefined ? "
                               "result : 'undefined')); "
                               "    return result; "
                               "  } catch(e) { "
                               "    console.error('✗ ' + e.toString()); "
                               "  } "
                               "})();";

    browser_->GetMainFrame()->ExecuteJavaScript(wrapped_code, "", 0);
  }
}

void BrowserClient::SetPopupConfirmActive(bool active, const std::string &url) {
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
                               const CefString &origin_url,
                               CefJSDialogHandler::JSDialogType dialog_type,
                               const CefString &message_text,
                               const CefString &default_prompt_text,
                               CefRefPtr<CefJSDialogCallback> callback,
                               bool &suppress_message) {
  LOGB("JS Dialog: type=" << dialog_type
                          << " message=" << message_text.ToString());

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

void BrowserClient::HandleJSDialogResponse(bool success,
                                           const std::string &input) {
  // Don't handle if browser is closing
  if (is_closing_) {
    return;
  }

  CefRefPtr<CefJSDialogCallback> callback;

  {
    std::lock_guard<std::mutex> lock(js_dialog_mutex_);

    if (!js_dialog_active_ || !js_dialog_callback_) {
      return;
    }

    LOGB("JS Dialog response: success=" << success << " input=" << input);

    // Take ownership of callback before unlocking
    callback = js_dialog_callback_;
    js_dialog_callback_ = nullptr;
    js_dialog_active_ = false;
  }

  // Clear the dialog from status bar
  {
    std::lock_guard<std::mutex> lock(render_mutex_);
    if (status_bar_) {
      status_bar_->clear();
    }
  }

  // Call the callback outside the mutex to avoid potential deadlocks
  if (callback) {
    callback->Continue(success, input);
  }

  // Force repaint to clear the dialog
  if (browser_ && browser_->GetHost()) {
    browser_->GetHost()->Invalidate(PET_VIEW);
  }
}

bool BrowserClient::OnFileDialog(
    CefRefPtr<CefBrowser> browser, CefDialogHandler::FileDialogMode mode,
    const CefString &title, const CefString &default_file_path,
    const std::vector<CefString> &accept_filters,
    const std::vector<CefString> &accept_extensions,
    const std::vector<CefString> &accept_descriptions,
    CefRefPtr<CefFileDialogCallback> callback) {
  LOGB("File Dialog: mode=" << mode << " title=" << title.ToString()
                            << " default_path="
                            << default_file_path.ToString());

  std::lock_guard<std::mutex> lock(file_dialog_mutex_);

  file_input_active_ = true;
  file_dialog_callback_ = callback;

  if (status_bar_) {
    status_bar_->showFileInput(default_file_path.ToString());
  }

  return true;
}

void BrowserClient::HandleFileDialogResponse(const std::string &file_path) {
  // Don't handle if browser is closing
  if (is_closing_) {
    return;
  }

  CefRefPtr<CefFileDialogCallback> callback;

  {
    std::lock_guard<std::mutex> lock(file_dialog_mutex_);

    if (!file_input_active_ || !file_dialog_callback_) {
      return;
    }

    LOGB("File Dialog response: file_path=" << file_path);

    // Take ownership of callback before unlocking
    callback = file_dialog_callback_;
    file_dialog_callback_ = nullptr;
    file_input_active_ = false;
  }

  // Call the callback outside the mutex to avoid potential deadlocks
  if (callback) {
    if (!file_path.empty()) {
      std::vector<CefString> file_paths;
      file_paths.push_back(file_path);
      callback->Continue(file_paths);
    } else {
      callback->Cancel();
    }
  }

  if (browser_ && browser_->GetHost()) {
    browser_->GetHost()->Invalidate(PET_VIEW);
  }
}

bool BrowserClient::GetAuthCredentials(
    CefRefPtr<CefBrowser> browser, const CefString &origin_url, bool isProxy,
    const CefString &host, int port, const CefString &realm,
    const CefString &scheme, CefRefPtr<CefAuthCallback> callback) {
  
  // If this is a proxy authentication request, check for configured credentials
  if (isProxy) {
    ProfileConfig &config = ProfileConfig::getInstance();
    std::string proxy_username = config.getProxyUsername();
    std::string proxy_password = config.getProxyPassword();
    
    // If proxy credentials are configured, use them automatically
    if (!proxy_username.empty()) {
      callback->Continue(proxy_username, proxy_password);
      return true;
    }
    // Otherwise fall through to show interactive dialog
  }
  
  std::lock_guard<std::mutex> lock(auth_mutex_);

  auth_dialog_active_ = true;
  auth_callback_ = callback;
  auth_realm_ = realm.ToString();
  
  // If realm is empty, show host:port instead
  std::string display_realm = auth_realm_;
  if (display_realm.empty()) {
    display_realm = host.ToString() + ":" + std::to_string(port);
  }

  if (status_bar_) {
    status_bar_->showAuthDialog("Username:", display_realm);
  }

  return true;
}

void BrowserClient::HandleAuthResponse(bool accept,
                                       const std::string &username,
                                       const std::string &password) {
  if (is_closing_) {
    return;
  }

  CefRefPtr<CefAuthCallback> callback;

  {
    std::lock_guard<std::mutex> lock(auth_mutex_);

    if (!auth_dialog_active_ || !auth_callback_) {
      return;
    }

    LOGB("Auth response: accept=" << accept << " username=" << username);

    callback = auth_callback_;
    auth_callback_ = nullptr;
    auth_dialog_active_ = false;
    auth_realm_.clear();
  }

  if (callback) {
    if (accept && !username.empty()) {
      callback->Continue(username, password);
    } else {
      callback->Cancel();
    }
  }

  if (browser_ && browser_->GetHost()) {
    browser_->GetHost()->Invalidate(PET_VIEW);
  }
}

bool BrowserClient::OnBeforeDownload(
    CefRefPtr<CefBrowser> browser, CefRefPtr<CefDownloadItem> download_item,
    const CefString &suggested_name,
    CefRefPtr<CefBeforeDownloadCallback> callback) {
  std::lock_guard<std::mutex> lock(download_mutex_);

  download_filename_ = suggested_name.ToString();
  download_url_ = download_item->GetURL().ToString();
  download_callback_ = callback;
  download_confirm_active_ = true;

  // Add to downloads list
  DownloadEntry entry;
  entry.id = download_item->GetId();
  entry.filename = suggested_name.ToString();
  entry.url = download_item->GetURL().ToString();
  entry.full_path = "";
  entry.total_bytes = 0;
  entry.received_bytes = 0;
  entry.percent_complete = 0;
  entry.speed = 0;
  entry.is_complete = false;
  entry.is_canceled = false;
  entry.is_in_progress = false;
  downloads_list_.push_back(entry);

  LOGB("Download request: " << download_filename_ << " from " << download_url_);

  // Show download confirmation dialog
  std::lock_guard<std::mutex> render_lock(render_mutex_);
  status_bar_->showDownloadConfirm(download_filename_, download_url_);

  return true;
}

void BrowserClient::OnDownloadUpdated(
    CefRefPtr<CefBrowser> browser, CefRefPtr<CefDownloadItem> download_item,
    CefRefPtr<CefDownloadItemCallback> callback) {
  std::lock_guard<std::mutex> lock(download_mutex_);

  // Find and update the download entry
  int32_t id = download_item->GetId();
  for (auto &entry : downloads_list_) {
    if (entry.id == id) {
      // Update filename - use suggested name if available, otherwise extract
      // from path
      std::string suggested = download_item->GetSuggestedFileName().ToString();
      if (!suggested.empty()) {
        entry.filename = suggested;
      } else {
        std::string path = download_item->GetFullPath().ToString();
        if (!path.empty()) {
          size_t pos = path.find_last_of("/\\");
          entry.filename =
              (pos != std::string::npos) ? path.substr(pos + 1) : path;
        }
      }
      entry.full_path = download_item->GetFullPath().ToString();
      entry.total_bytes = download_item->GetTotalBytes();
      entry.received_bytes = download_item->GetReceivedBytes();
      entry.percent_complete = download_item->GetPercentComplete();
      entry.speed = download_item->GetCurrentSpeed();
      entry.is_complete = download_item->IsComplete();
      entry.is_canceled = download_item->IsCanceled();
      entry.is_in_progress = download_item->IsInProgress();
      break;
    }
  }

  if (download_item->IsComplete()) {
    LOGB("Download complete: " << download_item->GetFullPath().ToString());
    std::lock_guard<std::mutex> render_lock(render_mutex_);
    status_bar_->showMessage("Download complete: " +
                             download_item->GetFullPath().ToString());
  } else if (download_item->IsCanceled()) {
    LOGB("Download canceled");
  }

  // Refresh download manager if active
  if (download_manager_active_ && browser_ && browser_->GetHost()) {
    browser_->GetHost()->Invalidate(PET_VIEW);
  }
}

void BrowserClient::HandleDownloadResponse(bool accept,
                                           const std::string &path) {
  // Don't handle if browser is closing
  if (is_closing_) {
    return;
  }

  CefRefPtr<CefBeforeDownloadCallback> callback;
  std::string filename;

  {
    std::lock_guard<std::mutex> lock(download_mutex_);

    if (!download_confirm_active_ || !download_callback_) {
      return;
    }

    // Take ownership of callback and data before unlocking
    callback = download_callback_;
    filename = download_filename_;
    download_callback_ = nullptr;
    download_confirm_active_ = false;
  }

  // Call the callback outside the mutex to avoid potential deadlocks
  if (callback) {
    if (accept) {
      std::string download_path =
          path.empty()
              ? (std::string(getenv("HOME") ? getenv("HOME") : "/tmp") +
                 "/Downloads/" + filename)
              : path;

      LOGB("Download accepted: " << download_path);
      callback->Continue(download_path, false);
    } else {
      LOGB("Download rejected");
      // Not calling Continue() means the download is rejected
    }
  }

  // Force repaint to clear the dialog
  if (browser_ && browser_->GetHost()) {
    browser_->GetHost()->Invalidate(PET_VIEW);
  }
}

void BrowserClient::AddCurrentPageToBookmarks() {
  if (!browser_)
    return;

  CefRefPtr<CefFrame> frame = browser_->GetMainFrame();
  if (!frame)
    return;

  std::string url = frame->GetURL().ToString();
  if (url.empty() || url == "about:blank")
    return;

  bookmarks_manager_.addBookmark(current_page_title_, url);

  std::lock_guard<std::mutex> lock(render_mutex_);
  status_bar_->showMessage("📚 Bookmark added: " + current_page_title_);
}

void BrowserClient::SetBookmarksActive(bool active) {
  bookmarks_active_ = active;
  if (active) {
    bookmarks_selected_index_ = 0;

    // Build display list
    const auto &bookmarks = bookmarks_manager_.getBookmarks();
    std::vector<std::string> display_list;
    for (const auto &bookmark : bookmarks) {
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
  if (!bookmarks_active_)
    return false;

  const auto &bookmarks = bookmarks_manager_.getBookmarks();
  if (bookmarks.empty())
    return true;

  bookmarks_selected_index_ += direction;
  if (bookmarks_selected_index_ < 0) {
    bookmarks_selected_index_ = 0;
  } else if (bookmarks_selected_index_ >= bookmarks.size()) {
    bookmarks_selected_index_ = bookmarks.size() - 1;
  }

  // Build display list
  std::vector<std::string> display_list;
  for (const auto &bookmark : bookmarks) {
    display_list.push_back(bookmark.title + " - " + bookmark.url);
  }

  std::lock_guard<std::mutex> lock(render_mutex_);
  status_bar_->showBookmarks(display_list, bookmarks_selected_index_);

  return true;
}

bool BrowserClient::HandleBookmarkConfirm() {
  if (!bookmarks_active_)
    return false;

  const auto &bookmarks = bookmarks_manager_.getBookmarks();
  LOGB("HandleBookmarkConfirm: bookmarks.size="
       << bookmarks.size() << " selected=" << bookmarks_selected_index_);

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
  if (!bookmarks_active_)
    return false;

  const auto &bookmarks = bookmarks_manager_.getBookmarks();
  if (bookmarks.empty() || bookmarks_selected_index_ >= bookmarks.size()) {
    return false;
  }

  bookmarks_manager_.removeBookmark(bookmarks_selected_index_);

  // Adjust selection
  const auto &updated_bookmarks = bookmarks_manager_.getBookmarks();
  if (bookmarks_selected_index_ >= updated_bookmarks.size() &&
      bookmarks_selected_index_ > 0) {
    bookmarks_selected_index_--;
  }

  // Update display
  std::vector<std::string> display_list;
  for (const auto &bookmark : updated_bookmarks) {
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
    std::vector<std::string> script_names =
        user_scripts_manager_.getAllScriptNames();

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
  if (!user_scripts_active_)
    return false;

  std::vector<std::string> script_names =
      user_scripts_manager_.getAllScriptNames();
  if (script_names.empty())
    return true;

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
  if (!user_scripts_active_)
    return false;

  std::vector<std::string> script_names =
      user_scripts_manager_.getAllScriptNames();
  if (script_names.empty() ||
      user_scripts_selected_index_ >= script_names.size()) {
    return false;
  }

  std::string script_name = script_names[user_scripts_selected_index_];
  std::string script_content =
      user_scripts_manager_.getScriptContent(script_name);

  LOGB("Injecting user script: " << script_name);

  // Close user scripts view
  SetUserScriptsActive(false);

  // Execute script
  if (!script_content.empty() && browser_) {
    CefRefPtr<CefFrame> frame = browser_->GetMainFrame();
    if (frame) {
      // For Kitty: append signal when script completes DOM transformation
      std::string wrapped_script = script_content;
      if (IsKittyRenderer()) {
        wrapped_script += "\nconsole.log('[Brow6el] USERSCRIPT_COMPLETE');";
      }
      
      frame->ExecuteJavaScript(wrapped_script, frame->GetURL(), 0);

      // Show confirmation
      std::lock_guard<std::mutex> lock(render_mutex_);
      status_bar_->showMessage("📜 Script injected: " + script_name + " (reload 'r' to exit)");
    }
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
  if (!browser_)
    return;

  CefRefPtr<CefFrame> frame = browser_->GetMainFrame();
  if (!frame)
    return;

  std::string url = frame->GetURL().ToString();
  std::vector<std::string> matching_scripts =
      user_scripts_manager_.getMatchingScripts(url);

  for (const auto &script_name : matching_scripts) {
    std::string script_content =
        user_scripts_manager_.getScriptContent(script_name);
    if (!script_content.empty()) {
      LOGB("Auto-injecting user script: " << script_name
                                          << " for URL: " << url);
      frame->ExecuteJavaScript(script_content, frame->GetURL(), 0);
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
  std::string js_path = GetJsFilePath("hint_mode.js");
  std::ifstream file(js_path);
  if (!file.is_open()) {
    LOGB("Failed to load hint_mode.js from " << js_path);
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

  CefRefPtr<CefFrame> frame = browser_->GetMainFrame();
  if (frame) {
    frame->ExecuteJavaScript(hint_js, "", 0);
    
    // Trigger render after delay to ensure overlay is visible
    // JavaScript sends DOM_CHANGED, but add this as safety/fallback
    CefRefPtr<CefBrowser> browser = browser_;
    std::thread([browser]() {
      std::this_thread::sleep_for(std::chrono::milliseconds(100));
      if (browser && browser->GetHost()) {
        browser->GetHost()->Invalidate(PET_VIEW);
      }
    }).detach();
  }

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
        "if (window.__brow6el_hints) { window.__brow6el_hints.cleanup(); }", "",
        0);
  }
}

void BrowserClient::HandleHintSelection(const std::string &hint) {
  if (!browser_ || hint.empty())
    return;

  CefRefPtr<CefFrame> frame = browser_->GetMainFrame();
  if (!frame)
    return;

  std::string js =
      "if (window.__brow6el_hints) { window.__brow6el_hints.select('" + hint +
      "'); }";
  frame->ExecuteJavaScript(js, "", 0);

  hint_mode_active_ = false;
}

// Mouse Emulation Mode implementation
void BrowserClient::ActivateMouseEmuMode() {
  if (!browser_ || !browser_->GetMainFrame()) {
    LOGB("ActivateMouseEmuMode: browser or frame is null");
    return;
  }

  // Read mouse emulation JavaScript
  std::string js_path = GetJsFilePath("mouse_emu.js");
  std::ifstream file(js_path);
  if (!file.is_open()) {
    LOGB("Failed to load mouse_emu.js from " << js_path);
    return;
  }

  std::stringstream buffer;
  buffer << file.rdbuf();
  std::string mouse_emu_js = buffer.str();

  if (mouse_emu_js.empty()) {
    LOGB("mouse_emu.js is empty!");
    return;
  }

  LOGB("Activating mouse emulation mode, JS size: " << mouse_emu_js.size()
                                                    << " bytes");

  mouse_emu_mode_active_ = true;

  CefRefPtr<CefFrame> frame = browser_->GetMainFrame();
  if (frame) {
    // Inject grid_keys config before executing mouse_emu.js
    std::string grid_keys = ProfileConfig::getInstance().getGridKeys();
    std::string config_js = "window.__brow6el_grid_keys = '" + grid_keys + "';";
    frame->ExecuteJavaScript(config_js, "", 0);
    frame->ExecuteJavaScript(mouse_emu_js, "", 0);
  }

  // Don't show status message - let select elements and other UI use the status
  // bar normally The yellow circle is enough visual feedback that mouse emu is
  // active
}

void BrowserClient::SetMouseEmuModeActive(bool active) {
  mouse_emu_mode_active_ = active;
  if (!active && browser_ && browser_->GetMainFrame()) {
    // Cleanup mouse cursor
    browser_->GetMainFrame()->ExecuteJavaScript(
        "if (window.__brow6el_mouse_emu) { "
        "window.__brow6el_mouse_emu.cleanup(); }",
        "", 0);
  }
}

void BrowserClient::HandleMouseEmuKey(const std::string &key) {
  if (!browser_)
    return;

  CefRefPtr<CefFrame> frame = browser_->GetMainFrame();
  if (!frame)
    return;

  std::string js = "if (window.__brow6el_mouse_emu) { "
                   "window.__brow6el_mouse_emu.handleKey('" +
                   key + "'); }";
  frame->ExecuteJavaScript(js, "", 0);
}

void BrowserClient::HandleMouseEmuClick() {
  if (!browser_ || !browser_->GetHost())
    return;

  CefRefPtr<CefFrame> frame = browser_->GetMainFrame();
  if (!frame)
    return;

  // Use the stored position to send a real CEF mouse click
  CefMouseEvent mouse_event;
  mouse_event.x = mouse_emu_x_;
  mouse_event.y = mouse_emu_y_;
  mouse_event.modifiers = 0;

  LOGB("Mouse emu click at (" << mouse_emu_x_ << "," << mouse_emu_y_ << ")");

  // Send mouse move first
  browser_->GetHost()->SendMouseMoveEvent(mouse_event, false);

  // Small delay to let the browser process the move
  usleep(10000); // 10ms

  // Send mouse down
  browser_->GetHost()->SendMouseClickEvent(mouse_event, MBT_LEFT, false, 1);

  // Small delay between down and up (like physical clicks)
  usleep(10000); // 10ms

  // Send mouse up
  browser_->GetHost()->SendMouseClickEvent(mouse_event, MBT_LEFT, true, 1);

  // Set focus after click (like physical mouse does)
  browser_->GetHost()->SetFocus(true);

  LOGB("Mouse emu click completed");
}

void BrowserClient::HandleMouseEmuDragStart() {
  if (!browser_ || !browser_->GetHost())
    return;

  mouse_emu_dragging_ = true;

  CefMouseEvent mouse_event;
  mouse_event.x = mouse_emu_x_;
  mouse_event.y = mouse_emu_y_;
  mouse_event.modifiers = 0;

  LOGB("Mouse emu drag start at " << mouse_emu_x_ << "," << mouse_emu_y_);

  // Send mouse move first to ensure position is correct
  browser_->GetHost()->SendMouseMoveEvent(mouse_event, false);

  // Small delay to let the browser process the move
  usleep(10000); // 10ms

  // Send mouse down (start drag) - important: this click event starts the drag
  browser_->GetHost()->SendMouseClickEvent(mouse_event, MBT_LEFT, false, 1);

  LOGB("Drag started, dragging_ = " << mouse_emu_dragging_);
}

void BrowserClient::HandleMouseEmuDragEnd() {
  if (!browser_ || !browser_->GetHost())
    return;

  if (!mouse_emu_dragging_)
    return;

  mouse_emu_dragging_ = false;

  CefMouseEvent mouse_event;
  mouse_event.x = mouse_emu_x_;
  mouse_event.y = mouse_emu_y_;
  mouse_event.modifiers = 0;

  LOGB("Mouse emu drag end at " << mouse_emu_x_ << "," << mouse_emu_y_);

  // Send mouse up (end drag/drop)
  browser_->GetHost()->SendMouseClickEvent(mouse_event, MBT_LEFT, true, 1);
}

void BrowserClient::HandleMouseEmuPosition(int x, int y) {
  mouse_emu_x_ = x;
  mouse_emu_y_ = y;

  // If dragging, send mouse move with button down
  if (mouse_emu_dragging_ && browser_ && browser_->GetHost()) {
    CefMouseEvent mouse_event;
    mouse_event.x = mouse_emu_x_;
    mouse_event.y = mouse_emu_y_;
    mouse_event.modifiers =
        EVENTFLAG_LEFT_MOUSE_BUTTON; // Indicate left button is pressed

    LOGB("Drag move to " << x << "," << y);

    // Send mouse move while dragging
    browser_->GetHost()->SendMouseMoveEvent(mouse_event, false);
  }
  
  // MutationObserver in mouse_emu.js will trigger Invalidate via DOM_CHANGED message
}

void BrowserClient::ToggleInspectMode() {
  if (!browser_ || !browser_->GetMainFrame()) {
    LOGB("ToggleInspectMode: browser or frame is null");
    return;
  }

  // Read inspect mode JavaScript
  std::string js_path = GetJsFilePath("inspect_mode.js");
  std::ifstream file(js_path);
  if (!file.is_open()) {
    LOGB("Failed to load inspect_mode.js from " << js_path);
    return;
  }

  std::stringstream buffer;
  buffer << file.rdbuf();
  std::string inspect_js = buffer.str();

  if (inspect_js.empty()) {
    LOGB("inspect_mode.js is empty!");
    return;
  }

  LOGB("Toggling inspect mode, JS size: " << inspect_js.size() << " bytes");

  CefRefPtr<CefFrame> frame = browser_->GetMainFrame();
  if (frame) {
    frame->ExecuteJavaScript(inspect_js, "", 0);
  }
}

void BrowserClient::ToggleDownloadManager() {
  download_manager_active_ = !download_manager_active_;

  if (download_manager_active_) {
    download_manager_selected_index_ = 0;

    std::vector<std::string> display_list;
    {
      std::lock_guard<std::mutex> lock(download_mutex_);
      for (const auto &entry : downloads_list_) {
        std::string status =
            entry.is_complete
                ? "✓"
                : (entry.is_canceled ? "✗"
                                     : (entry.is_in_progress ? "⬇" : "…"));
        std::string display = status + " " + entry.filename;
        if (entry.is_in_progress && entry.total_bytes > 0) {
          display += " (" + std::to_string(entry.percent_complete) + "%)";
        }
        display_list.push_back(display);
      }
    }

    std::lock_guard<std::mutex> lock(render_mutex_);
    status_bar_->showDownloadManager(display_list,
                                     download_manager_selected_index_);
  } else {
    std::lock_guard<std::mutex> lock(render_mutex_);
    status_bar_->clear();
  }

  if (browser_ && browser_->GetHost()) {
    browser_->GetHost()->Invalidate(PET_VIEW);
  }
}

void BrowserClient::HandleDownloadManagerNavigation(int direction) {
  if (!download_manager_active_)
    return;

  int total = downloads_list_.size();
  if (total == 0)
    return;

  download_manager_selected_index_ += direction;
  if (download_manager_selected_index_ < 0)
    download_manager_selected_index_ = 0;
  if (download_manager_selected_index_ >= total)
    download_manager_selected_index_ = total - 1;

  std::vector<std::string> display_list;
  {
    std::lock_guard<std::mutex> lock(download_mutex_);
    for (const auto &entry : downloads_list_) {
      std::string status =
          entry.is_complete
              ? "✓"
              : (entry.is_canceled ? "✗" : (entry.is_in_progress ? "⬇" : "…"));
      std::string display = status + " " + entry.filename;
      if (entry.is_in_progress && entry.total_bytes > 0) {
        display += " (" + std::to_string(entry.percent_complete) + "%)";
      }
      display_list.push_back(display);
    }
  }

  std::lock_guard<std::mutex> lock(render_mutex_);
  status_bar_->showDownloadManager(display_list,
                                   download_manager_selected_index_);
}

void BrowserClient::HandleDownloadManagerAction(char action) {
  if (!download_manager_active_)
    return;

  if (action == 'm' || action == 'M') {
    ToggleDownloadManager();
    return;
  }

  if (action == 'c' || action == 'C') {
    std::lock_guard<std::mutex> lock(download_mutex_);
    auto it = downloads_list_.begin();
    while (it != downloads_list_.end()) {
      if (it->is_complete || it->is_canceled) {
        it = downloads_list_.erase(it);
      } else {
        ++it;
      }
    }
    download_manager_selected_index_ = 0;

    std::vector<std::string> display_list;
    for (const auto &entry : downloads_list_) {
      std::string status =
          entry.is_complete
              ? "✓"
              : (entry.is_canceled ? "✗" : (entry.is_in_progress ? "⬇" : "…"));
      std::string display = status + " " + entry.filename;
      if (entry.is_in_progress && entry.total_bytes > 0) {
        display += " (" + std::to_string(entry.percent_complete) + "%)";
      }
      display_list.push_back(display);
    }

    std::lock_guard<std::mutex> render_lock(render_mutex_);
    status_bar_->showDownloadManager(display_list,
                                     download_manager_selected_index_);
  }
}

void BrowserClient::CopyCurrentURL() {
  if (!browser_ || !browser_->GetMainFrame()) {
    LOGB("CopyCurrentURL: browser or frame is null");
    return;
  }

  std::string url = browser_->GetMainFrame()->GetURL().ToString();
  if (!url.empty()) {
    Clipboard::copyToClipboard(url);
    LOGB("Copied URL to clipboard: " << url);
  }
}

void BrowserClient::ActivateVisualMode() {
  if (!browser_ || !browser_->GetMainFrame()) {
    LOGB("ActivateVisualMode: browser or frame is null");
    return;
  }

  visual_mode_active_ = true;

  // Load visual mode JavaScript
  std::string js_path = GetJsFilePath("visual_mode.js");
  std::ifstream file(js_path);
  if (!file.is_open()) {
    LOGB("Failed to load visual_mode.js from " << js_path);
    return;
  }

  std::string js((std::istreambuf_iterator<char>(file)),
                 std::istreambuf_iterator<char>());
  file.close();

  browser_->GetMainFrame()->ExecuteJavaScript(js, "", 0);
}

void BrowserClient::SetVisualModeActive(bool active) {
  visual_mode_active_ = active;

  if (!active && browser_ && browser_->GetMainFrame()) {
    // Cleanup visual mode
    std::string js = "if (window.__brow6el_visual_mode) { "
                     "window.__brow6el_visual_mode.cleanup(); }";
    browser_->GetMainFrame()->ExecuteJavaScript(js, "", 0);
  }
}

void BrowserClient::HandleVisualModeKey(const std::string &key) {
  if (!browser_ || !browser_->GetMainFrame()) {
    return;
  }

  std::string js = "if (window.__brow6el_visual_mode) { "
                   "window.__brow6el_visual_mode.handleKey('" +
                   key + "'); }";
  browser_->GetMainFrame()->ExecuteJavaScript(js, "", 0);
}

void BrowserClient::OnFindResult(CefRefPtr<CefBrowser> browser,
                                  int identifier,
                                  int count,
                                  const CefRect& selectionRect,
                                  int activeMatchOrdinal,
                                  bool finalUpdate) {
  search_match_count_ = count;
  search_active_match_ = activeMatchOrdinal;
  
  // Update status bar with match count
  if (status_bar_ && search_active_ && input_handler_) {
    std::string query = input_handler_->GetSearchQuery();
    status_bar_->showSearchInput(query, activeMatchOrdinal, count);
  }
  
  // Trigger render now that Find() is complete and buffer is updated
  // This ensures we render the frame WITH highlights, not before
  if (finalUpdate && browser_) {
    // For kitty renderer, add delay to let CEF finish preparing scrolled content
    // Scrolling during search navigation needs time to populate buffer
    if (IsKittyRenderer()) {
      std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
    browser_->GetHost()->Invalidate(PET_VIEW);
  }
}
