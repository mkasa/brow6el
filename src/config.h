#pragma once

#include <fstream>
#include <iostream>
#include <map>
#include <pwd.h>
#include <sstream>
#include <string>
#include <sys/stat.h>
#include <unistd.h>
#include <vector>

class Config {
public:
  static Config &getInstance() {
    static Config instance;
    return instance;
  }

  void loadCefFlags() {
    ensureConfigDir();
    std::string config_path = getConfigPath("cef_flags.conf");

    // Check if migration is needed
    if (!checkConfigVersion(config_path)) {
      migrateConfig(config_path);
    }

    std::ifstream file(config_path);
    if (!file.good()) {
      // Create default config file
      createDefaultCefFlagsConfig(config_path);
      file.open(config_path);
    }

    if (file.is_open()) {
      std::string line;
      while (std::getline(file, line)) {
        // Skip comments and empty lines
        if (line.empty() || line[0] == '#') {
          continue;
        }

        // Parse flag=value or just flag
        size_t equals = line.find('=');
        if (equals != std::string::npos) {
          std::string flag = trim(line.substr(0, equals));
          std::string value = trim(line.substr(equals + 1));
          cef_flags_with_value_[flag] = value;
        } else {
          std::string flag = trim(line);
          if (!flag.empty()) {
            cef_flags_.push_back(flag);
          }
        }
      }
      file.close();
    }
  }

  const std::vector<std::string> &getCefFlags() const { return cef_flags_; }

  const std::map<std::string, std::string> &getCefFlagsWithValue() const {
    return cef_flags_with_value_;
  }

private:
  Config() {}

  // Current config version - increment when making breaking changes
  static constexpr int CONFIG_VERSION = 4;

  std::vector<std::string> cef_flags_;
  std::map<std::string, std::string> cef_flags_with_value_;

  std::string getHomeDir() {
    const char *home = getenv("HOME");
    if (home) {
      return std::string(home);
    }
    struct passwd *pw = getpwuid(getuid());
    if (pw) {
      return std::string(pw->pw_dir);
    }
    return "";
  }

  std::string getConfigDir() { return getHomeDir() + "/.brow6el"; }

  std::string getConfigPath(const std::string &filename) {
    return getConfigDir() + "/" + filename;
  }

  void ensureConfigDir() {
    std::string dir = getConfigDir();
    struct stat st;
    if (stat(dir.c_str(), &st) != 0) {
      mkdir(dir.c_str(), 0755);
    }
  }

  bool checkConfigVersion(const std::string &config_path) {
    std::ifstream file(config_path);
    if (!file.good()) {
      return true; // No config exists, will create new one
    }

    std::string line;
    while (std::getline(file, line)) {
      if (line.find("# Config Version: ") == 0) {
        int version = std::stoi(line.substr(18));
        return version == CONFIG_VERSION;
      }
    }

    // No version marker found, old config
    return false;
  }

  void migrateConfig(const std::string &config_path) {
    // Backup old config
    std::string backup_path = config_path + ".bak";
    std::ifstream src(config_path, std::ios::binary);
    std::ofstream dst(backup_path, std::ios::binary);
    dst << src.rdbuf();
    src.close();
    dst.close();

    std::cout << "Migrating CEF flags config from old version...\n";
    std::cout << "  Old config backed up to: " << backup_path << "\n";
    std::cout << "  Creating new config with recommended settings\n";
    std::cout << "  Review changes in ~/.brow6el/cef_flags.conf\n\n";

    // Create new config
    createDefaultCefFlagsConfig(config_path);
  }

  void createDefaultCefFlagsConfig(const std::string &path) {
    std::ofstream file(path);
    if (file.is_open()) {
      file << "# Brow6el CEF Command Line Flags Configuration\n";
      file << "# Config Version: " << CONFIG_VERSION << "\n";
      file << "# Lines starting with # are comments\n";
      file << "# Format: flag_name or flag_name=value\n";
      file << "#\n";
      file << "# To disable a flag, comment it out with #\n";
      file << "# To enable, remove the # at the start of the line\n";
      file << "#\n";
      file << "# Documentation: "
              "https://peter.sh/experiments/chromium-command-line-switches/\n";
      file << "\n";
      file << "# GPU and Rendering\n";
      file << "# Disable hardware GPU (required for terminal rendering)\n";
      file << "disable-gpu\n";
      file << "disable-gpu-compositing\n";
      file << "\n";
      file << "# Use SwiftShader for software-based WebGL/GPU rendering\n";
      file << "use-gl=swiftshader\n";
      file << "use-angle=swiftshader\n";
      file << "\n";
      file << "# WebGL Support\n";
      file << "# Enable WebGL via SwiftShader (software renderer)\n";
      file << "enable-webgl\n";
      file << "ignore-gpu-blacklist\n";
      file << "# Required for SwiftShader WebGL (lower security for trusted "
              "content)\n";
      file << "enable-unsafe-swiftshader\n";
      file << "\n";
      file << "# Frame Scheduling\n";
      file << "enable-begin-frame-scheduling\n";
      file << "\n";
      file << "# Ozone Platform (for better compatibility)\n";
      file << "# DnsOverHttps feature enables DoH support (configure in "
              "browser.conf)\n";
      file << "enable-features=UseOzonePlatform,DnsOverHttps\n";
      file << "ozone-platform=headless\n";
      file << "\n";
      file << "# Stability and Compatibility\n";
      file << "no-xshm\n";
      file << "disable-dev-shm-usage\n";
      file << "disable-setuid-sandbox\n";
      file << "no-sandbox\n";
      file << "\n";
      file << "# HTTP Basic Authentication\n";
      file << "# Disable Chrome's built-in login prompt to enable custom auth dialog\n";
      file << "disable-chrome-login-prompt\n";
      file << "\n";
      file << "# Process Model\n";
      file << "in-process-gpu\n";
      file << "no-zygote\n";
      file << "\n";
      file << "# Limit subprocess spawning to prevent memory leaks\n";
      file << "renderer-process-limit=1\n";
      file << "disable-site-isolation-trials\n";
      file << "disable-features=IsolateOrigins,site-per-process\n";
      file << "\n";
      file << "# PDF Handling\n";
      file << "# Disable built-in PDF viewer to force downloads\n";
      file << "#disable-pdf-extension\n";
      file << "\n";
      file << "# Memory Optimization\n";
      file << "js-flags=--max-old-space-size=512 --no-decommit-pooled-pages\n";
      file << "\n";
      file << "# Additional Options (commented out by default)\n";
      file << "# Uncomment to enable:\n";
      file << "\n";
      file << "# Disable hardware video decoding\n";
      file << "# disable-accelerated-video-decode\n";
      file << "\n";
      file << "# Enable more verbose logging\n";
      file << "# enable-logging\n";
      file << "# v=1\n";
      file << "\n";
      file << "# Disable web security (use with caution!)\n";
      file << "# disable-web-security\n";
      file << "\n";
      file.close();
    }
  }

  std::string trim(const std::string &str) {
    size_t first = str.find_first_not_of(" \t\r\n");
    if (first == std::string::npos) {
      return "";
    }
    size_t last = str.find_last_not_of(" \t\r\n");
    return str.substr(first, last - first + 1);
  }
};
