#include "user_scripts.h"
#include <algorithm>
#include <cstdlib>
#include <dirent.h>
#include <fstream>
#include <pwd.h>
#include <sstream>
#include <sys/stat.h>
#include <unistd.h>

UserScriptsManager::UserScriptsManager() : auto_inject_enabled_(true) {
  loadConfig();
}

std::string UserScriptsManager::getConfigFilePath() const {
  const char *home = getenv("HOME");
  if (!home) {
    home = getpwuid(getuid())->pw_dir;
  }
  std::string config_dir = std::string(home) + "/.brow6el";
  // Create directory if it doesn't exist
  mkdir(config_dir.c_str(), 0755);
  return config_dir + "/userscripts.conf";
}

std::string UserScriptsManager::getScriptsDir() const {
  const char *home = getenv("HOME");
  if (!home) {
    home = getpwuid(getuid())->pw_dir;
  }
  std::string config_dir = std::string(home) + "/.brow6el";
  std::string scripts_dir = config_dir + "/userscripts";
  // Create directory if it doesn't exist
  mkdir(config_dir.c_str(), 0755);
  mkdir(scripts_dir.c_str(), 0755);
  return scripts_dir;
}

void UserScriptsManager::scanScriptsDirectory() {
  // Scan both user scripts and bundled scripts directories
  std::vector<std::string> dirs_to_scan;

  // User scripts directory
  std::string user_scripts_dir = getScriptsDir();
  mkdir(user_scripts_dir.c_str(), 0755);
  dirs_to_scan.push_back(user_scripts_dir);

  // Bundled scripts directory (relative to executable)
  char exe_path[1024];
  ssize_t len = readlink("/proc/self/exe", exe_path, sizeof(exe_path) - 1);
  if (len != -1) {
    exe_path[len] = '\0';
    std::string exe_dir = std::string(exe_path);
    size_t last_slash = exe_dir.find_last_of('/');
    if (last_slash != std::string::npos) {
      exe_dir = exe_dir.substr(0, last_slash);
      std::string bundled_scripts_dir = exe_dir + "/scripts";
      dirs_to_scan.push_back(bundled_scripts_dir);
    }
  }

  // Scan all directories
  for (const auto &scripts_dir : dirs_to_scan) {
    DIR *dir = opendir(scripts_dir.c_str());
    if (!dir)
      continue;

    struct dirent *entry;
    while ((entry = readdir(dir)) != nullptr) {
      std::string filename = entry->d_name;

      // Only consider .js files
      if (filename.length() > 3 &&
          filename.substr(filename.length() - 3) == ".js") {
        // Check if script already exists in config
        bool found = false;
        for (auto &script : scripts_) {
          if (script.filename == filename) {
            found = true;
            // Update source_dir if not set
            if (script.source_dir.empty()) {
              script.source_dir = scripts_dir;
            }
            break;
          }
        }

        // If not in config, add with empty pattern (manual only)
        if (!found) {
          UserScript script;
          script.name = filename.substr(0, filename.length() - 3); // Remove .js
          script.filename = filename;
          script.enabled = true;
          script.source_dir = scripts_dir; // Store which directory it came from
          scripts_.push_back(script);
        }
      }
    }

    closedir(dir);
  }
}

void UserScriptsManager::loadConfig() {
  scripts_.clear();
  auto_inject_enabled_ = true;

  std::ifstream file(getConfigFilePath());
  if (!file.is_open()) {
    // No config file, just scan directory
    scanScriptsDirectory();
    return;
  }

  std::string line;
  while (std::getline(file, line)) {
    // Trim whitespace
    line.erase(0, line.find_first_not_of(" \t\r\n"));
    line.erase(line.find_last_not_of(" \t\r\n") + 1);

    // Skip empty lines and comments
    if (line.empty() || line[0] == '#')
      continue;

    // Check for auto-inject setting
    if (line.find("auto_inject=") == 0) {
      auto_inject_enabled_ =
          (line.substr(12) == "true" || line.substr(12) == "1");
      continue;
    }

    // Parse script entry: filename|name|enabled|pattern1,pattern2,...
    size_t pos1 = line.find('|');
    if (pos1 == std::string::npos)
      continue;

    size_t pos2 = line.find('|', pos1 + 1);
    if (pos2 == std::string::npos)
      continue;

    size_t pos3 = line.find('|', pos2 + 1);
    if (pos3 == std::string::npos)
      continue;

    UserScript script;
    script.filename = line.substr(0, pos1);
    script.name = line.substr(pos1 + 1, pos2 - pos1 - 1);
    script.enabled = (line.substr(pos2 + 1, pos3 - pos2 - 1) == "true" ||
                      line.substr(pos2 + 1, pos3 - pos2 - 1) == "1");

    // Parse patterns
    std::string patterns_str = line.substr(pos3 + 1);
    std::istringstream patterns_stream(patterns_str);
    std::string pattern;
    while (std::getline(patterns_stream, pattern, ',')) {
      // Trim pattern
      pattern.erase(0, pattern.find_first_not_of(" \t"));
      pattern.erase(pattern.find_last_not_of(" \t") + 1);
      if (!pattern.empty()) {
        script.url_patterns.push_back(pattern);
      }
    }

    scripts_.push_back(script);
  }

  file.close();

  // Scan directory for any new scripts
  scanScriptsDirectory();
}

void UserScriptsManager::saveConfig() {
  std::ofstream file(getConfigFilePath());
  if (!file.is_open())
    return;

  file << "# brow6el User Scripts Configuration\n";
  file << "# Format: filename|name|enabled|pattern1,pattern2,...\n";
  file << "# Patterns support wildcards: * (any chars), ? (single char)\n\n";

  file << "auto_inject=" << (auto_inject_enabled_ ? "true" : "false") << "\n\n";

  for (const auto &script : scripts_) {
    file << script.filename << "|" << script.name << "|"
         << (script.enabled ? "true" : "false") << "|";

    for (size_t i = 0; i < script.url_patterns.size(); i++) {
      file << script.url_patterns[i];
      if (i < script.url_patterns.size() - 1) {
        file << ",";
      }
    }
    file << "\n";
  }
}

bool UserScriptsManager::matchesPattern(const std::string &url,
                                        const std::string &pattern) const {
  // Simple wildcard matching
  size_t url_pos = 0;
  size_t pattern_pos = 0;
  size_t star_pos = std::string::npos;
  size_t match_pos = 0;

  while (url_pos < url.length()) {
    if (pattern_pos < pattern.length()) {
      if (pattern[pattern_pos] == '*') {
        star_pos = pattern_pos++;
        match_pos = url_pos;
        continue;
      } else if (pattern[pattern_pos] == '?' ||
                 pattern[pattern_pos] == url[url_pos]) {
        url_pos++;
        pattern_pos++;
        continue;
      }
    }

    if (star_pos != std::string::npos) {
      pattern_pos = star_pos + 1;
      url_pos = ++match_pos;
    } else {
      return false;
    }
  }

  // Skip remaining stars
  while (pattern_pos < pattern.length() && pattern[pattern_pos] == '*') {
    pattern_pos++;
  }

  return pattern_pos == pattern.length();
}

std::vector<std::string>
UserScriptsManager::getMatchingScripts(const std::string &url) const {
  std::vector<std::string> matching;

  if (!auto_inject_enabled_) {
    return matching;
  }

  for (const auto &script : scripts_) {
    if (!script.enabled)
      continue;

    for (const auto &pattern : script.url_patterns) {
      if (matchesPattern(url, pattern)) {
        matching.push_back(script.name);
        break;
      }
    }
  }

  return matching;
}

std::vector<std::string> UserScriptsManager::getAllScriptNames() const {
  std::vector<std::string> names;
  for (const auto &script : scripts_) {
    names.push_back(script.name);
  }
  return names;
}

std::string
UserScriptsManager::getScriptContent(const std::string &script_name) const {
  std::string filepath;
  for (const auto &script : scripts_) {
    if (script.name == script_name) {
      // Use source_dir if available, otherwise fall back to user scripts dir
      if (!script.source_dir.empty()) {
        filepath = script.source_dir + "/" + script.filename;
      } else {
        filepath = getScriptsDir() + "/" + script.filename;
      }
      break;
    }
  }

  if (filepath.empty())
    return "";

  std::ifstream file(filepath);
  if (!file.is_open())
    return "";

  std::stringstream buffer;
  buffer << file.rdbuf();
  return buffer.str();
}

std::string
UserScriptsManager::getScriptPath(const std::string &script_name) const {
  for (const auto &script : scripts_) {
    if (script.name == script_name) {
      // Use source_dir if available, otherwise fall back to user scripts dir
      if (!script.source_dir.empty()) {
        return script.source_dir + "/" + script.filename;
      }
      return getScriptsDir() + "/" + script.filename;
    }
  }
  return "";
}
