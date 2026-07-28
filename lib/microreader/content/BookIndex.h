#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "StringPool.h"

namespace microreader {

class DrawBuffer;

struct BookIndexEntry {
  StringRef path{};
  StringRef title{};
  StringRef author{};
  uint32_t last_open_order = 0;  // 0 = never opened; higher = more recently opened
};

static constexpr int MAX_BOOKS = 250;
static constexpr uint32_t INDEX_FORMAT_VERSION = 2;

class BookIndex {
 public:
  static BookIndex& instance();

  // Returns true if path ends with .epub (case-insensitive) and has a non-empty stem.
  static bool is_book_path(const char* path);

  bool load(const std::string& index_file);
  bool save(const std::string& index_file) const;

  // Recursively scan root_dir for EPUBs and rebuild the index.
  void build_index(const std::string& root_dir, DrawBuffer& buf);

  const std::vector<BookIndexEntry>& entries() const { return entries_; }
  const StringPool& pool() const { return pool_; }

  // Bumped on every logical mutation (index_file / remove_path / rename_in_place /
  // build_index). MainMenu caches this to detect external changes. load() does not
  // bump; clear_entries() resets it to 0.
  uint64_t generation() const { return generation_; }

  const BookIndexEntry* find_entry(std::string_view path) const;

  // Returns false (no-op) if MAX_BOOKS has been reached.
  bool add_entry(std::string_view path, std::string_view title, std::string_view author, uint32_t last_open_order = 0);

  // Remove the entry at `index` from the in-memory entries vector.
  // The StringPool is not compacted (individual strings cannot be freed).
  void remove_entry(int index);

  // Remove entry by path. No-op if not found.
  void remove_entry(std::string_view path);

  void clear_entries();

 private:
  std::vector<BookIndexEntry> entries_;
  StringPool pool_;
  uint64_t generation_ = 0;
  BookIndex() = default;

  // Loads from index_path if entries_ is empty, preventing .dat truncation
  // when a mutation arrives after MainMenu::stop() cleared in-memory state.
  void ensure_loaded_(const std::string& index_path);
};

}  // namespace microreader
