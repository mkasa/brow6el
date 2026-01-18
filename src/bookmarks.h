#pragma once

#include <string>
#include <vector>

struct Bookmark {
  std::string title;
  std::string url;
};

class BookmarksManager {
public:
  BookmarksManager();

  void load();
  void save();
  void addBookmark(const std::string &title, const std::string &url);
  void removeBookmark(size_t index);
  const std::vector<Bookmark> &getBookmarks() const { return bookmarks_; }

private:
  std::vector<Bookmark> bookmarks_;
  std::string getBookmarksFilePath() const;
};
