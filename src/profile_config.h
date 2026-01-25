#pragma once

#include <string>
#include <map>

enum class ProfileMode {
  Temporary,  // /tmp/brow6el_PID_RANDOM - deleted on exit
  Persistent, // ~/.brow6el/profile - saved between sessions
  Custom      // User-specified path
};

class ProfileConfig {
public:
  static ProfileConfig &getInstance() {
    static ProfileConfig instance;
    return instance;
  }

  void load();
  void save();

  // Getters
  ProfileMode getMode() const { return mode_; }
  std::string getProfilePath() const;
  bool shouldClearCacheOnExit() const { return clear_cache_on_exit_; }
  bool shouldClearCookiesOnExit() const { return clear_cookies_on_exit_; }
  size_t getCacheSizeMB() const { return cache_size_mb_; }
  std::string getDefaultUrl() const { return default_url_; }
  std::string getGridKeys() const { return grid_keys_; }
  bool isDohEnabled() const { return doh_enabled_; }
  std::string getDohServer() const { return doh_server_; }
  std::string getDohMode() const { return doh_mode_; }
  bool isTiledRenderingEnabled() const { return tiled_rendering_enabled_; }
  int getCellWidth() const { return cell_width_override_; }
  int getCellHeight() const { return cell_height_override_; }
  std::string getGraphicsProtocol() const { return graphics_protocol_; }
  bool showInternalConsoleLogs() const { return show_internal_console_logs_; }
  bool isProxyEnabled() const { return proxy_enabled_; }
  std::string getProxyServer() const { return proxy_server_; }
  std::string getProxyBypassList() const { return proxy_bypass_list_; }
  std::string getProxyUsername() const { return proxy_username_; }
  std::string getProxyPassword() const { return proxy_password_; }

  // Profile management
  std::string createProfileDirectory();
  void cleanupProfile();

  // Set mode (for command line override)
  void setMode(ProfileMode mode) { mode_ = mode; }
  void setCustomPath(const std::string &path) { custom_path_ = path; }
  void overrideMode(const std::string &mode_str);
  void overrideGraphicsProtocol(const std::string &protocol) { 
    if (protocol == "sixel" || protocol == "kitty") {
      graphics_protocol_ = protocol;
    }
  }

private:
  ProfileConfig();

  ProfileMode mode_ = ProfileMode::Temporary;
  std::string custom_path_;
  std::string current_profile_path_;
  bool clear_cache_on_exit_ = false;
  bool clear_cookies_on_exit_ = false;
  size_t cache_size_mb_ = 500;
  std::string default_url_ = "https://example.com";
  std::string grid_keys_ = "qweasdzxc";
  bool doh_enabled_ = false;
  std::string doh_server_ = "https://cloudflare-dns.com/dns-query";
  std::string doh_mode_ = "secure";
  bool tiled_rendering_enabled_ = false;
  int cell_width_override_ = 0;  // 0 means auto-detect
  int cell_height_override_ = 0; // 0 means auto-detect
  std::string graphics_protocol_ = "sixel"; // "sixel" or "kitty"
  bool show_internal_console_logs_ = false; // Hide Brow6el internal messages
  bool proxy_enabled_ = false;
  std::string proxy_server_ = "";
  std::string proxy_bypass_list_ = "localhost,127.0.0.1";
  std::string proxy_username_ = "";
  std::string proxy_password_ = "";
  
  // Zoom settings
  double zoom_level_ = 1.0;
  double zoom_step_ = 0.5;
  std::string default_zoom_behavior_ = "none"; // "auto", "fixed", or "none"
  std::map<std::string, double> site_zoom_levels_; // domain -> zoom level

  std::string expandPath(const std::string &path) const;
  std::string trim(const std::string &str);
  void createDefaultConfig();
  void writeConfig(std::ofstream &file, bool use_defaults,
                   bool include_examples);
  std::string getConfigDir();
  std::string getConfigPath();
  void loadSiteZoomLevels();
  
public:
  // Zoom getters
  double getZoomLevel() const { return zoom_level_; }
  double getZoomStep() const { return zoom_step_; }
  std::string getDefaultZoomBehavior() const { return default_zoom_behavior_; }
  double getSiteZoomLevel(const std::string &domain) const;
};
