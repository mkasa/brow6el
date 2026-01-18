#pragma once

#include <map>
#include <string>
#include <vector>

struct UserScript {
  std::string name;
  std::string filename;
  std::vector<std::string> url_patterns;
  bool enabled;
  std::string source_dir; // Directory where this script came from
};

class UserScriptsManager {
public:
  UserScriptsManager();

  void loadConfig();
  void saveConfig();
  void setAutoInject(bool enabled) {
    auto_inject_enabled_ = enabled;
    saveConfig();
  }
  bool isAutoInjectEnabled() const { return auto_inject_enabled_; }

  std::vector<std::string> getMatchingScripts(const std::string &url) const;
  std::vector<std::string> getAllScriptNames() const;
  std::string getScriptContent(const std::string &script_name) const;
  std::string getScriptPath(const std::string &script_name) const;

  bool matchesPattern(const std::string &url, const std::string &pattern) const;

private:
  std::string getConfigFilePath() const;
  std::string getScriptsDir() const;
  void scanScriptsDirectory();

  std::vector<UserScript> scripts_;
  bool auto_inject_enabled_;
};
