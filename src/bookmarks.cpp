#include "bookmarks.h"
#include <cstdlib>
#include <fstream>
#include <pwd.h>
#include <sstream>
#include <sys/stat.h>
#include <unistd.h>

BookmarksManager::BookmarksManager() { load(); }

std::string BookmarksManager::getBookmarksFilePath() const {
  const char *home = getenv("HOME");
  if (!home) {
    home = getpwuid(getuid())->pw_dir;
  }
  std::string config_dir = std::string(home) + "/.brow6el";
  // Create directory if it doesn't exist
  mkdir(config_dir.c_str(), 0755);
  return config_dir + "/bookmarks";
}

void BookmarksManager::load() {
  bookmarks_.clear();

  std::ifstream file(getBookmarksFilePath());
  if (!file.is_open()) {
    return;
  }

  std::string line;
  while (std::getline(file, line)) {
    if (line.empty())
      continue;

    size_t separator = line.find('\t');
    if (separator != std::string::npos) {
      Bookmark bookmark;
      bookmark.title = line.substr(0, separator);
      bookmark.url = line.substr(separator + 1);
      bookmarks_.push_back(bookmark);
    }
  }
}

void BookmarksManager::save() {
  std::ofstream file(getBookmarksFilePath());
  if (!file.is_open()) {
    return;
  }

  for (const auto &bookmark : bookmarks_) {
    file << bookmark.title << "\t" << bookmark.url << "\n";
  }
}

void BookmarksManager::addBookmark(const std::string &title,
                                   const std::string &url) {
  // Check if bookmark already exists
  for (const auto &bookmark : bookmarks_) {
    if (bookmark.url == url) {
      return; // Already bookmarked
    }
  }

  Bookmark bookmark;
  bookmark.title = title.empty() ? url : title;
  bookmark.url = url;
  bookmarks_.push_back(bookmark);
  save();
}

void BookmarksManager::removeBookmark(size_t index) {
  if (index < bookmarks_.size()) {
    bookmarks_.erase(bookmarks_.begin() + index);
    save();
  }
}
