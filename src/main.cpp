#include "include/cef_app.h"
#include "include/cef_browser.h"
#include "include/cef_command_line.h"
#include "include/cef_cookie.h"
#include "include/cef_request_context.h"
#include "include/cef_preference.h"
#include "include/cef_values.h"
#include "browser_app.h"
#include "browser_client.h"
#include "terminal_detector.h"
#include "input_handler.h"
#include "profile_config.h"
#include "version.h"
#include <iostream>
#include <unistd.h>
#include <signal.h>
#include <filesystem>

namespace fs = std::filesystem;

static volatile bool g_running = true;
static volatile bool g_needs_resize = false;
static volatile bool g_suspended = false;
static std::string g_original_title;
static InputHandler* g_input_handler = nullptr;

void saveTerminalTitle() {
    // Save current title (attempt to read via OSC query, fallback to default)
    // Most terminals don't support reading title, so we'll use a default
    g_original_title = "Terminal";
}

void restoreTerminalTitle() {
    // Restore original terminal title
    std::cout << "\033]0;" << g_original_title << "\007" << std::flush;
}

void cleanupAndExit() {
    // Cleanup profile based on configuration
    ProfileConfig::getInstance().cleanupProfile();
    
    restoreTerminalTitle();
    std::cout << "\033[2J\033[H";
    std::cout << "Browser closed." << std::endl;
}

void signalHandler(int signum) {
    cleanupAndExit();
    // Re-raise signal to get default behavior
    signal(signum, SIG_DFL);
    raise(signum);
}

void crashHandler(int signum) {
    cleanupAndExit();
    // Re-raise signal
    signal(signum, SIG_DFL);
    raise(signum);
}

void resizeHandler(int signum) {
    g_needs_resize = true;
}

void suspendHandler(int signum) {
    // Ctrl+Z: restore terminal to normal mode before suspending
    if (g_input_handler) {
        g_input_handler->stop();
    }
    
    // Restore terminal
    std::cout << "\033[?25h";  // Show cursor
    fflush(stdout);
    
    g_suspended = true;
    
    // Send SIGSTOP to ourselves to actually suspend
    signal(SIGTSTP, SIG_DFL);
    raise(SIGTSTP);
}

void continueHandler(int signum) {
    // fg: restore raw mode and continue
    if (g_suspended) {
        g_suspended = false;
        
        // Re-register the suspend handler
        signal(SIGTSTP, suspendHandler);
        
        // Restart input handler
        if (g_input_handler) {
            g_input_handler->start();
        }
    }
}

void requestShutdown() {
    g_running = false;
}

int main(int argc, char* argv[]) {
    CefMainArgs main_args(argc, argv);
    
    // Load profile config first to get default URL
    ProfileConfig& config = ProfileConfig::getInstance();
    
    // Parse command line arguments
    std::string url = config.getDefaultUrl();
    std::string profile_mode_override;
    
    for (int i = 1; i < argc; i++) {
        std::string arg = argv[i];
        if (arg == "--help" || arg == "-h") {
            std::cout << "Brow6el " << BROW6EL_VERSION << " - Terminal Web Browser\n\n";
            std::cout << "Usage: brow6el [OPTIONS] [URL]\n\n";
            std::cout << "Options:\n";
            std::cout << "  --persistent        Use persistent profile mode\n";
            std::cout << "  --temporary         Use temporary profile mode (private)\n";
            std::cout << "  --custom            Use custom profile mode\n";
            std::cout << "  --version           Show version information\n\n";
            std::cout << "Vim-Style Modal Control:\n";
            std::cout << "  STANDARD mode (default) - Single-key commands:\n";
            std::cout << "    hjkl              Navigate (left/down/up/right)\n";
            std::cout << "    t/g               Scroll up/down\n";
            std::cout << "    p/n               Back/forward in history\n";
            std::cout << "    r                 Reload page\n";
            std::cout << "    u                 Navigate to URL\n";
            std::cout << "    c                 Toggle console\n";
            std::cout << "    d                 Add bookmark\n";
            std::cout << "    b                 Open bookmarks\n";
            std::cout << "    f                 Hint mode (keyboard navigation)\n";
            std::cout << "    s                 User scripts menu\n";
            std::cout << "    y                 Toggle auto-inject scripts\n";
            std::cout << "    x                 Exit\n";
            std::cout << "    i                 Enter INSERT mode\n";
            std::cout << "    e                 Enter MOUSE mode\n\n";
            std::cout << "  INSERT mode - All keys pass to webpage:\n";
            std::cout << "    ESC               Return to STANDARD mode\n\n";
            std::cout << "  MOUSE mode - Keyboard mouse emulation:\n";
            std::cout << "    hjkl              Move mouse (left/down/up/right)\n";
            std::cout << "    q/f               Precision/fast speed\n";
            std::cout << "    r                 Toggle drag and drop\n";
            std::cout << "    SPACE/ENTER       Click\n";
            std::cout << "    e or ESC          Return to STANDARD mode\n\n";
            std::cout << "Mode indicator shown in status bar: [S], [I], or [M]\n\n";
            std::cout << "Note: Profile mode can be configured in ~/.brow6el/browser.conf\n";
            std::cout << "      Bookmarks and user scripts are persistent\n";
            std::cout << "      See VIM_CONTROL.md for detailed documentation\n";
            return 0;
        } else if (arg == "--version" || arg == "-v") {
            std::cout << "Brow6el " << BROW6EL_VERSION << "\n";
            std::cout << "User-Agent: " << BROW6EL_USER_AGENT << "\n";
            return 0;
        } else if (arg == "--persistent") {
            profile_mode_override = "persistent";
        } else if (arg == "--temporary") {
            profile_mode_override = "temporary";
        } else if (arg == "--custom") {
            profile_mode_override = "custom";
        } else if (arg[0] != '-') {
            url = arg;
        }
    }
    
    // Get executable directory for resources (needed by both main and sub-processes)
    char exe_path[1024];
    std::string exe_dir;
    ssize_t len = readlink("/proc/self/exe", exe_path, sizeof(exe_path) - 1);
    if (len != -1) {
        exe_path[len] = '\0';
        exe_dir = exe_path;
        size_t pos = exe_dir.find_last_of("/");
        if (pos != std::string::npos) {
            exe_dir = exe_dir.substr(0, pos);
        }
    }
    
    // Set up settings for both main process and subprocesses
    CefSettings settings;
    settings.windowless_rendering_enabled = true;
    settings.no_sandbox = true;
    settings.multi_threaded_message_loop = false;
    settings.command_line_args_disabled = false;
    
    // Disable sandbox-related features
    CefString(&settings.browser_subprocess_path).FromASCII(exe_path);
    
    // Suppress CEF logging - only show fatal errors
    settings.log_severity = LOGSEVERITY_FATAL;
    CefString(&settings.log_file).FromASCII("/tmp/brow6el_debug.log");
    
    // Set custom user agent
    CefString(&settings.user_agent).FromASCII(BROW6EL_USER_AGENT);
    
    if (!exe_dir.empty()) {
        CefString(&settings.resources_dir_path).FromASCII(exe_dir.c_str());
        CefString(&settings.locales_dir_path).FromASCII((exe_dir + "/locales").c_str());
    }
    
    CefRefPtr<BrowserApp> app(new BrowserApp);
    
    // Handle subprocess execution - RETURN EARLY if subprocess
    int exit_code = CefExecuteProcess(main_args, app.get(), nullptr);
    if (exit_code >= 0) {
        return exit_code;
    }
    
    // Only main process continues from here
    
    // Load profile configuration and create profile directory (main process only)
    ProfileConfig& profile_config = ProfileConfig::getInstance();
    
    // Apply command-line override if provided
    if (!profile_mode_override.empty()) {
        profile_config.overrideMode(profile_mode_override);
    }
    
    std::string profile_path = profile_config.createProfileDirectory();
    
    if (profile_path.empty()) {
        std::cerr << "Failed to create profile directory" << std::endl;
        return 1;
    }
    
    // Set profile paths for CEF
    std::string cache_path = profile_path + "/cache";
    CefString(&settings.cache_path).FromASCII(cache_path.c_str());
    CefString(&settings.root_cache_path).FromASCII(profile_path.c_str());
    
    // Enable cookie persistence for persistent/custom mode
    if (profile_config.getMode() != ProfileMode::Temporary) {
        settings.persist_session_cookies = 1;
        std::cout << "Cookie persistence enabled" << std::endl;
    } else {
        settings.persist_session_cookies = 0;
    }
    
    signal(SIGINT, signalHandler);
    signal(SIGTERM, signalHandler);
    signal(SIGSEGV, crashHandler);  // Segmentation fault
    signal(SIGABRT, crashHandler);  // Abort
    signal(SIGTRAP, crashHandler);  // Trace trap
    signal(SIGWINCH, resizeHandler); // Window resize
    signal(SIGTSTP, suspendHandler); // Ctrl+Z (suspend)
    signal(SIGCONT, continueHandler); // fg (continue)
    
    // Save original terminal title
    saveTerminalTitle();
    
    std::cout << "Brow6el - Terminal Web Browser with Sixel Support" << std::endl;
    
    // Check if profile is locked by another instance (persistent/custom mode)
    if (profile_config.getMode() != ProfileMode::Temporary) {
        std::string lock_file = profile_path + "/SingletonLock";
        
        // Use symlink_status instead of exists (exists follows symlinks and may return false)
        std::error_code ec_check;
        auto lock_status = fs::symlink_status(lock_file, ec_check);
        bool lock_exists = fs::exists(lock_status);
        
        if (lock_exists) {
            // Read the symlink target (format: hostname-PID)
            std::error_code ec;
            fs::path target = fs::read_symlink(lock_file, ec);
            
            if (!ec) {
                std::string target_str = target.string();
                size_t dash_pos = target_str.find_last_of('-');
                
                if (dash_pos != std::string::npos) {
                    std::string pid_str = target_str.substr(dash_pos + 1);
                    
                    try {
                        pid_t lock_pid = std::stoi(pid_str);
                        
                        // Check if process is still running
                        if (kill(lock_pid, 0) == 0) {
                            std::cerr << "Error: Profile is already in use by another browser instance (PID: " 
                                     << lock_pid << ")" << std::endl;
                            std::cerr << "       Close the other instance or use temporary mode for multiple sessions" << std::endl;
                            return 1;
                        } else {
                            // Stale lock file - process is dead, remove it
                            std::cout << "Removing stale profile lock..." << std::endl;
                            fs::remove(profile_path + "/SingletonLock", ec);
                            fs::remove(profile_path + "/SingletonCookie", ec);
                            fs::remove(profile_path + "/SingletonSocket", ec);
                        }
                    } catch (...) {
                        // Couldn't parse PID, ignore
                    }
                }
            }
        }
    }
    
    TerminalInfo termInfo = TerminalDetector::detect();
    
    if (!termInfo.supports_sixel) {
        std::cerr << "Error: Your terminal does not support Sixel graphics" << std::endl;
        std::cerr << "Please use a terminal emulator with Sixel support (e.g., mlterm, xterm with sixel)" << std::endl;
        return 1;
    }
    
    if (!CefInitialize(main_args, settings, app.get(), nullptr)) {
        std::cerr << "Failed to initialize CEF" << std::endl;
        return 1;
    }
    
    // Configure DNS-over-HTTPS using global preference manager (must be after CefInitialize)
    if (profile_config.isDohEnabled()) {
        CefRefPtr<CefPreferenceManager> pref_manager = CefPreferenceManager::GetGlobalPreferenceManager();
        
        std::string doh_server = profile_config.getDohServer();
        std::string doh_mode = profile_config.getDohMode();
        
        std::cout << "Enabling DNS-over-HTTPS: " << doh_server << " (mode: " << doh_mode << ")" << std::endl;
        
        CefString error;
        
        // Set DoH mode
        CefRefPtr<CefValue> mode_value = CefValue::Create();
        mode_value->SetString(doh_mode);
        if (!pref_manager->SetPreference("dns_over_https.mode", mode_value, error)) {
            std::cerr << "Failed to set DoH mode: " << error.ToString() << std::endl;
        }
        
        // Set DoH server template
        CefRefPtr<CefValue> templates_value = CefValue::Create();
        templates_value->SetString(doh_server);
        if (!pref_manager->SetPreference("dns_over_https.templates", templates_value, error)) {
            std::cerr << "Failed to set DoH templates: " << error.ToString() << std::endl;
        }
        
        std::cout << "DoH configuration complete." << std::endl;
    }
    
    // Use cell dimensions from config if set, otherwise use auto-detected
    int cell_width = profile_config.getCellWidth() > 0 ? profile_config.getCellWidth() : termInfo.cell_width;
    int cell_height = profile_config.getCellHeight() > 0 ? profile_config.getCellHeight() : termInfo.cell_height;
    
    CefRefPtr<BrowserClient> client(new BrowserClient(termInfo.width, termInfo.height, cell_width, cell_height));
    
    // Configure tiled rendering from config
    client->SetTiledRenderingEnabled(profile_config.isTiledRenderingEnabled());
    
    CefWindowInfo window_info;
    window_info.SetAsWindowless(0);
    
    CefBrowserSettings browser_settings;
    browser_settings.windowless_frame_rate = 30;
    
    // Create request context with light color scheme
    CefRequestContextSettings context_settings;
    CefRefPtr<CefRequestContext> request_context = CefRequestContext::CreateContext(context_settings, nullptr);
    
    // Set light color scheme to avoid forced dark mode
    request_context->SetChromeColorScheme(CEF_COLOR_VARIANT_LIGHT, 0);
    
    CefBrowserHost::CreateBrowser(window_info, client.get(), url, 
                                  browser_settings, nullptr, request_context);
    
    // Give browser time to initialize
    for (int i = 0; i < 10 && !client->GetBrowser(); i++) {
        CefDoMessageLoopWork();
        usleep(100000); // 100ms
    }
    
    if (!client->GetBrowser()) {
        std::cerr << "Error: Browser failed to initialize" << std::endl;
        CefShutdown();
        return 1;
    }
    
    // Start input handler (mouse and keyboard)
    InputHandler input_handler(client->GetBrowser(), 
                               termInfo.width / termInfo.cell_width,
                               termInfo.height / termInfo.cell_height,
                               termInfo.cell_width, termInfo.cell_height,
                               termInfo.width, termInfo.height);
    input_handler.setBrowserClient(client.get()); // Link for select navigation
    input_handler.setTiledRenderingEnabled(profile_config.isTiledRenderingEnabled()); // Set from config
    client->SetInputMode(input_handler.getModeName()); // Set initial mode in status bar
    
    // Set global pointer for signal handlers
    g_input_handler = &input_handler;
    
    // Set focus so the browser shows cursor/caret in input fields
    client->GetBrowser()->GetHost()->SetFocus(true);
    
    input_handler.start();
    
    while (g_running && !client->IsClosing()) {
        // Handle terminal resize
        if (g_needs_resize) {
            g_needs_resize = false;
            
            // Re-detect terminal size
            TerminalInfo newInfo = TerminalDetector::detect();
            
            if (newInfo.supports_sixel && newInfo.width > 0 && newInfo.height > 0) {
                // Update browser client dimensions
                client->Resize(newInfo.width, newInfo.height);
                
                // Update input handler dimensions
                input_handler.updateDimensions(
                    newInfo.width / newInfo.cell_width,
                    newInfo.height / newInfo.cell_height,
                    newInfo.cell_width,
                    newInfo.cell_height,
                    newInfo.width,
                    newInfo.height
                );
                
                // Notify CEF and force repaint
                client->GetBrowser()->GetHost()->WasResized();
                client->GetBrowser()->GetHost()->Invalidate(PET_VIEW);
            }
        }
        
        CefDoMessageLoopWork();
        usleep(33333);
    }
    
    // Stop input handler before closing
    input_handler.stop();
    
    // Clear global pointer
    g_input_handler = nullptr;
    
    // Prevent any further status bar updates
    if (client && client->GetStatusBar()) {
        client->GetStatusBar()->setShutdownMode();
    }
    
    // Reset terminal title to empty (let terminal use default)
    std::cout << "\033]0;\007" << std::flush;
    
    if (client->GetBrowser()) {
        client->GetBrowser()->GetHost()->CloseBrowser(true);
    }
    
    // Wait for browser to close
    int close_wait = 0;
    while (!client->IsClosing() && close_wait < 100) {
        CefDoMessageLoopWork();
        usleep(10000);
        close_wait++;
    }
    
    // Do a few more message loop iterations to ensure cleanup
    for (int i = 0; i < 10; i++) {
        CefDoMessageLoopWork();
        usleep(10000);
    }
    
    // Release browser reference before shutdown
    client = nullptr;
    app = nullptr;
    
    // NOW clear the screen after all CEF processing is done
    std::cout << "\033[0m";        // Reset all attributes (colors, etc)
    std::cout << "\033[2J";        // Clear entire screen
    std::cout << "\033[H";         // Move cursor to home
    std::cout << std::flush;
    
    // Flush cookies before shutdown (important for persistent mode)
    if (profile_config.getMode() != ProfileMode::Temporary) {
        CefCookieManager::GetGlobalManager(nullptr)->FlushStore(nullptr);
        std::cout << "Flushing cookies..." << std::endl;
        // Give cookies time to flush
        for (int i = 0; i < 20; i++) {
            CefDoMessageLoopWork();
            usleep(10000);
        }
    }
    
    // Shutdown CEF properly
    CefShutdown();
    
    // Profile cleanup is handled by cleanupAndExit()
    cleanupAndExit();
    
    return 0;
}
