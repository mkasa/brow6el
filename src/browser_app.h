#pragma once

#include "config.h"
#include "profile_config.h"
#include "include/cef_app.h"
#include "include/cef_command_line.h"

class BrowserApp : public CefApp, public CefBrowserProcessHandler {
public:
  BrowserApp() {
    // Load CEF flags from config file
    Config::getInstance().loadCefFlags();
  }

  virtual CefRefPtr<CefBrowserProcessHandler>
  GetBrowserProcessHandler() override {
    return this;
  }

  virtual void OnBeforeCommandLineProcessing(
      const CefString &process_type,
      CefRefPtr<CefCommandLine> command_line) override {

    // Load flags from config file (~/.brow6el/cef_flags.conf)
    const auto &flags = Config::getInstance().getCefFlags();
    const auto &flags_with_value = Config::getInstance().getCefFlagsWithValue();

    // Apply simple flags
    for (const auto &flag : flags) {
      command_line->AppendSwitch(flag);
    }

    // Apply flags with values
    for (const auto &pair : flags_with_value) {
      command_line->AppendSwitchWithValue(pair.first, pair.second);
    }

    // Apply proxy settings from browser.conf
    ProfileConfig &profile_config = ProfileConfig::getInstance();
    if (profile_config.isProxyEnabled() && !profile_config.getProxyServer().empty()) {
      command_line->AppendSwitchWithValue("proxy-server", profile_config.getProxyServer());
      
      if (!profile_config.getProxyBypassList().empty()) {
        command_line->AppendSwitchWithValue("proxy-bypass-list", profile_config.getProxyBypassList());
      }
    }
  }

private:
  IMPLEMENT_REFCOUNTING(BrowserApp);
};

