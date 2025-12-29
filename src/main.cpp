#include "include/cef_app.h"
#include "include/cef_browser.h"
#include "include/cef_command_line.h"
#include "include/cef_cookie.h"
#include "browser_app.h"
#include "browser_client.h"
#include "terminal_detector.h"
#include "input_handler.h"
#include "profile_config.h"
#include <iostream>
#include <unistd.h>
#include <signal.h>
#include <filesystem>

namespace fs = std::filesystem;

static volatile bool g_running = true;
static std::string g_original_title;

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

void requestShutdown() {
    g_running = false;
}

int main(int argc, char* argv[]) {
    CefMainArgs main_args(argc, argv);
    
    // Parse command line arguments
    std::string url = "https://example.com";
    std::string profile_mode_override;
    
    for (int i = 1; i < argc; i++) {
        std::string arg = argv[i];
        if (arg == "--help" || arg == "-h") {
            std::cout << "Usage: brow6el [OPTIONS] [URL]\n\n";
            std::cout << "Options:\n";
            std::cout << "  --persistent        Use persistent profile mode\n";
            std::cout << "  --temporary         Use temporary profile mode (private)\n";
            std::cout << "  --custom            Use custom profile mode\n\n";
            std::cout << "Keyboard Shortcuts:\n";
            std::cout << "  Ctrl+X              Exit\n";
            std::cout << "  Ctrl+R              Reload page\n";
            std::cout << "  Ctrl+L              Navigate to URL\n";
            std::cout << "  Ctrl+Left/Right     Back/Forward (or Ctrl+P/N)\n";
            std::cout << "  Ctrl+Up/Down        Scroll (or Ctrl+T/G)\n";
            std::cout << "  Ctrl+F              Hint mode (keyboard navigation)\n";
            std::cout << "  Ctrl+E              Mouse emulation (arrow keys + Enter)\n";
            std::cout << "  Ctrl+K              Toggle console\n";
            std::cout << "  Ctrl+D              Add bookmark\n";
            std::cout << "  Ctrl+B              Open bookmarks\n";
            std::cout << "  Ctrl+U              Open user scripts\n";
            std::cout << "  Ctrl+Y              Toggle auto-inject scripts\n\n";
            std::cout << "Note: Profile mode can be configured in ~/.brow6el/browser.conf\n";
            std::cout << "      Bookmarks and user scripts are persistent\n";
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
    
    CefRefPtr<BrowserClient> client(new BrowserClient(termInfo.width, termInfo.height));
    
    CefWindowInfo window_info;
    window_info.SetAsWindowless(0);
    
    CefBrowserSettings browser_settings;
    browser_settings.windowless_frame_rate = 30;
    
    CefBrowserHost::CreateBrowser(window_info, client.get(), url, 
                                  browser_settings, nullptr, nullptr);
    
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
    input_handler.start();
    
    while (g_running && !client->IsClosing()) {
        CefDoMessageLoopWork();
        usleep(33333);
    }
    
    // Stop input handler before closing
    input_handler.stop();
    
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
