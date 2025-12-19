#include "include/cef_app.h"
#include "include/cef_browser.h"
#include "include/cef_command_line.h"
#include "browser_app.h"
#include "browser_client.h"
#include "terminal_detector.h"
#include "input_handler.h"
#include <iostream>
#include <unistd.h>
#include <signal.h>

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
    CefString(&settings.cache_path).FromASCII("/tmp/brow6el_cache");
    
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
    signal(SIGINT, signalHandler);
    signal(SIGTERM, signalHandler);
    signal(SIGSEGV, crashHandler);  // Segmentation fault
    signal(SIGABRT, crashHandler);  // Abort
    signal(SIGTRAP, crashHandler);  // Trace trap
    
    // Save original terminal title
    saveTerminalTitle();
    
    std::cout << "Brow6el - Terminal Web Browser with Sixel Support" << std::endl;
    
    TerminalInfo termInfo = TerminalDetector::detect();
    
    if (!termInfo.supports_sixel) {
        std::cerr << "Error: Your terminal does not support Sixel graphics" << std::endl;
        std::cerr << "Please use a terminal emulator with Sixel support (e.g., mlterm, xterm with sixel)" << std::endl;
        return 1;
    }
    
    std::string url = "https://example.com";
    if (argc > 1) {
        url = argv[1];
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
    
    cleanupAndExit();
    
    // Skip CefShutdown() as it causes trace trap in single-process mode
    // The OS will clean up resources on process exit
    _exit(0);  // Use _exit to bypass atexit handlers that might cause issues
    
    return 0;
}
