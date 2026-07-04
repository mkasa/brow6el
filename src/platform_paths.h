#pragma once

// Cross-platform helpers for locating the running executable and the directory
// that holds bundled application resources (JS files, tutorial.html, scripts/).
//
// Linux: resources sit next to the executable in the build directory.
// macOS:  the executable lives in <app>.app/Contents/MacOS and resources live
//         in <app>.app/Contents/Resources.

#include <string>

#ifdef __APPLE__
#include <cstdint>
#include <mach-o/dyld.h>
#else
#include <unistd.h>
#endif

namespace platform {

// Absolute path to the running executable (empty string on failure).
inline std::string executablePath() {
  char buf[4096];
#ifdef __APPLE__
  uint32_t size = sizeof(buf);
  if (_NSGetExecutablePath(buf, &size) == 0) {
    return std::string(buf);
  }
  return std::string();
#else
  ssize_t len = readlink("/proc/self/exe", buf, sizeof(buf) - 1);
  if (len != -1) {
    buf[len] = '\0';
    return std::string(buf);
  }
  return std::string();
#endif
}

// Directory containing the running executable (".": on failure).
inline std::string executableDir() {
  std::string p = executablePath();
  size_t pos = p.find_last_of('/');
  if (pos == std::string::npos) {
    return std::string(".");
  }
  return p.substr(0, pos);
}

// Directory holding bundled application resources.
inline std::string resourceDir() {
#ifdef __APPLE__
  // executableDir() == <app>.app/Contents/MacOS -> <app>.app/Contents/Resources
  std::string macos_dir = executableDir();
  size_t pos = macos_dir.find_last_of('/');
  std::string contents_dir =
      (pos == std::string::npos) ? macos_dir : macos_dir.substr(0, pos);
  return contents_dir + "/Resources";
#else
  return executableDir();
#endif
}

} // namespace platform
