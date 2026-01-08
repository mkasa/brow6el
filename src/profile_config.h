#pragma once

#include <string>

enum class ProfileMode {
    Temporary,    // /tmp/brow6el_PID_RANDOM - deleted on exit
    Persistent,   // ~/.brow6el/profile - saved between sessions
    Custom        // User-specified path
};

class ProfileConfig {
public:
    static ProfileConfig& getInstance() {
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
    
    // Profile management
    std::string createProfileDirectory();
    void cleanupProfile();
    
    // Set mode (for command line override)
    void setMode(ProfileMode mode) { mode_ = mode; }
    void setCustomPath(const std::string& path) { custom_path_ = path; }
    void overrideMode(const std::string& mode_str);
    
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
    
    std::string expandPath(const std::string& path) const;
    std::string trim(const std::string& str);
    void createDefaultConfig();
    std::string getConfigDir();
    std::string getConfigPath();
};
