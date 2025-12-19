#pragma once

#include "include/cef_app.h"
#include "include/cef_command_line.h"

class BrowserApp : public CefApp, public CefBrowserProcessHandler {
public:
    BrowserApp() = default;
    
    virtual CefRefPtr<CefBrowserProcessHandler> GetBrowserProcessHandler() override {
        return this;
    }
    
    virtual void OnBeforeCommandLineProcessing(
        const CefString& process_type,
        CefRefPtr<CefCommandLine> command_line) override {
        // Aggressively disable GPU to use software rendering only
        command_line->AppendSwitch("disable-gpu");
        command_line->AppendSwitch("disable-gpu-compositing");
        command_line->AppendSwitch("disable-gpu-sandbox");
        command_line->AppendSwitch("disable-gpu-rasterization");
        command_line->AppendSwitch("disable-gpu-driver-bug-workarounds");
        command_line->AppendSwitchWithValue("use-gl", "swiftshader");
        command_line->AppendSwitch("disable-software-rasterizer");
        command_line->AppendSwitch("enable-begin-frame-scheduling");
        
        // Additional stability flags
        command_line->AppendSwitch("single-process");
        
        // Disable PDF viewer plugin to force downloads
        command_line->AppendSwitch("disable-pdf-extension");
    }
    
private:
    IMPLEMENT_REFCOUNTING(BrowserApp);
};
