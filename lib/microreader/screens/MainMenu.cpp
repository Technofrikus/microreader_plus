#include "MainMenu.h"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstring>

#include "../Application.h"
#include "../content/BookIndex.h"

#ifdef ESP_PLATFORM
#include <dirent.h>
#include <strings.h>
#include <sys/stat.h>
#else
#include <filesystem>
namespace fs = std::filesystem;
#endif

namespace microreader {

// Returns a view into `path` pointing at the bare filename without extension.
static std::string_view filename_sv(const std::string& path) {
  const char* name = path.c_str();
  const char* sep = std::strrchr(name, '/');
#ifdef _WIN32
  const char* bsep = std::strrchr(name, '\\');
  if (bsep && (!sep || bsep > sep))
    sep = bsep;
#endif
  if (sep)
    name = sep + 1;
  const char* dot = std::strrchr(name, '.');
  size_t len = dot ? static_cast<size_t>(dot - name) : std::strlen(name);
  return {name, len};
}

static bool ci_less(std::string_view a, std::string_view b) {
  size_t min_len = std::min(a.size(), b.size());
#ifdef _WIN32
  int cmp = _strnicmp(a.data(), b.data(), min_len);
#else
  int cmp = strncasecmp(a.data(), b.data(), min_len);
#endif
  if (cmp != 0) return cmp < 0;
  return a.size() < b.size();
}

static std::string get_parent_dir(const std::string& path) {
  if (path.empty()) return path;
#ifndef ESP_PLATFORM
  fs::path p(path);
  if (p.has_parent_path()) {
    std::string parent = p.parent_path().string();
    for (char& c : parent) if (c == '\\') c = '/';
    return parent;
  }
  return path;
#else
  const char* str = path.c_str();
  const char* last_sep = std::strrchr(str, '/');
  if (last_sep && last_sep != str) {
    return std::string(str, last_sep - str);
  }
  return path;
#endif
}

static bool is_same_dir(const std::string& a, const std::string& b) {
  if (a == b) return true;
#ifndef ESP_PLATFORM
  std::error_code ec;
  if (fs::equivalent(a, b, ec)) return true;
#endif
  return false;
}

static bool is_sub_dir_of(const std::string& path, const std::string& root) {
  if (is_same_dir(path, root)) return true;
#ifndef ESP_PLATFORM
  std::error_code ec;
  fs::path p(path);
  fs::path r(root);
  while (!p.empty() && p != p.root_path()) {
    if (fs::equivalent(p, r, ec)) return true;
    p = p.parent_path();
  }
#endif
  size_t rlen = root.size();
  return (path.size() >= rlen && path.compare(0, rlen, root) == 0);
}

void MainMenu::on_start() {
  title_ = nullptr;
  set_long_back_threshold(20);
  set_alignment_left(align_left_);
  // Folder-browsing menu: a held UP goes up one folder level (long-press);
  // a short tap moves the selection up one. UP auto-repeat is disabled here.
  set_up_uses_long_press(true);

  if (app_ && app_->data_dir_) {
    std::string index_path = std::string(app_->data_dir_) + "/book_index.dat";
    if (!BookIndex::instance().load(index_path)) {
      needs_scan_ = true;
    }
  }

  populate_list_();
}

void MainMenu::update(const ButtonState& buttons, DrawBuffer& buf, IRuntime& runtime) {
  // Detect external mutations (serial upload/delete/rename) while this screen
  // is visible. The generation counter is bumped by BookIndex on every
  // mutation that changes the logical contents.
  if (cached_generation_ != BookIndex::instance().generation()) {
    cached_generation_ = BookIndex::instance().generation();
    populate_list_();
    draw_all_(buf, runtime.battery_percentage());
    buf.full_refresh();
  }

  if (needs_scan_) {
    needs_scan_ = false;
    scan_directory_(buf);
    populate_list_();

    draw_all_(buf, runtime.battery_percentage());
    buf.full_refresh();
    cached_generation_ = BookIndex::instance().generation();
  }

  ListMenuScreen::update(buttons, buf, runtime);
}

void MainMenu::on_select(int index) {
  if (is_separator(index)) return;
  int real = entries_index_for(index);
  if (real < 0 || real >= static_cast<int>(entries_.size())) return;

  const MenuEntry& e = entries_[real];
  if (e.type == EntryType::Directory) {
    if (e.name == ".." || is_same_dir(e.path, get_parent_dir(current_dir_))) {
      std::string old_dir = current_dir_;
      current_dir_ = e.path;
      initial_selection_ = old_dir;
    } else {
      current_dir_ = e.path;
    }
    populate_list_();
    request_redraw();
  } else {
    last_selected_path_ = e.path;
    // A book selected from the Recent section (the first recent_count_ entries)
    // should restore the cursor to recents on return, not navigate into its
    // parent folder.
    opened_from_recents_ = real < recent_count_;
    app_->record_book_opened(e.path);
    app_->reader()->set_path(e.path.c_str());
    app_->push_screen(ScreenId::Reader);
  }
}

void MainMenu::stop() {
  const std::string& cur = current_book_path();
  if (!cur.empty()) {
    initial_selection_ = cur;
    last_selected_path_ = cur;
  } else if (!entries_.empty()) {
    int real = entries_index_for(selected());
    if (real >= 0 && real < static_cast<int>(entries_.size())) {
      initial_selection_ = entries_[real].path;
    }
  }

  { std::vector<MenuEntry> tmp; entries_.swap(tmp); }
  free_items_storage();
  BookIndex::instance().clear_entries();
}

void MainMenu::on_back() {
  const std::string& cur = current_book_path();
  if (!cur.empty()) {
    initial_selection_ = cur;
  } else if (!entries_.empty()) {
    int real = entries_index_for(selected());
    if (real >= 0 && real < static_cast<int>(entries_.size())) {
      initial_selection_ = entries_[real].path;
    }
  }
  app_->push_screen(ScreenId::Settings);
}

void MainMenu::on_long_up() {
  if (books_dir_.empty() || current_dir_.empty() || is_same_dir(current_dir_, books_dir_)) {
    return;
  }
  std::string parent = get_parent_dir(current_dir_);
  if (!is_sub_dir_of(parent, books_dir_)) {
    parent = books_dir_;
  }
  std::string old_dir = current_dir_;
  current_dir_ = parent;
  initial_selection_ = old_dir;
  populate_list_();
  request_redraw();
}

bool MainMenu::draw_custom_header_(DrawBuffer& buf) const {
  if (!header_font_.valid() || !ui_font_.valid())
    return false;

  const int W = buf.width();
  static const char kMicro[] = "micro";
  static const char kReaderPlus[] = "reader\xe2\x81\xba";

  const int w_micro = ui_font_.word_width(kMicro, 5, FontStyle::Regular);
  const int w_reader_plus = header_font_.word_width(kReaderPlus, 9, FontStyle::Regular);
  const int x_start = (W - w_micro - w_reader_plus) / 2;

  const int baseline_reader = kHeaderY + header_font_.baseline();
  const int baseline_micro = kHeaderY + ui_font_.baseline() + 1;

  buf.draw_text_proportional(x_start, baseline_micro, kMicro, 5, ui_font_, false);
  buf.draw_text_proportional(x_start + w_micro, baseline_reader, kReaderPlus, 9, header_font_, false);

  return true;
}

void MainMenu::on_long_back(int index) {
  if (is_separator(index)) return;
  int real = entries_index_for(index);
  if (real < 0 || real >= static_cast<int>(entries_.size()))
    return;
  if (entries_[real].type != EntryType::Book)
    return;
  app_->delete_confirm()->setup(entries_[real].path, entries_[real].last_open_order > 0);
  app_->push_screen(ScreenId::DeleteConfirm);
}

void MainMenu::scan_directory_(DrawBuffer& buf) {
  if (books_dir_.empty() || !app_->data_dir_)
    return;

  std::string root_dir = books_dir_;
  const std::string index_path = std::string(app_->data_dir_) + "/book_index.dat";

  buf.sync_bw_ram();

  BookIndex::instance().build_index(root_dir, buf);
  BookIndex::instance().save(index_path);

  buf.reset_after_scratch(true);
}

int MainMenu::count() const {
  return static_cast<int>(entries_.size()) + static_cast<int>(separators_.size());
}

bool MainMenu::is_separator(int index) const {
  for (const auto& s : separators_)
    if (s.visual_index == index) return true;
  return false;
}

std::string_view MainMenu::get_item_label(int index) const {
  for (const auto& s : separators_)
    if (s.visual_index == index) return s.label;
  int real = entries_index_for(index);
  if (real < 0 || real >= static_cast<int>(entries_.size()))
    return {};
  return entries_[real].display_name;
}

bool MainMenu::directory_has_epubs_(const std::string& dir_path) const {
  const auto& entries = BookIndex::instance().entries();
  const StringPool& pool = BookIndex::instance().pool();
  for (const auto& entry : entries) {
    std::string_view p = entry.path.view(pool);
    if (!p.empty() && is_sub_dir_of(std::string(p), dir_path)) {
      return true;
    }
  }

#ifdef ESP_PLATFORM
  DIR* dir = opendir(dir_path.c_str());
  if (dir) {
    struct dirent* ent;
    while ((ent = readdir(dir)) != nullptr) {
      if (ent->d_name[0] == '.') continue;
      std::string fullpath = dir_path + "/" + ent->d_name;
      struct stat st;
      if (stat(fullpath.c_str(), &st) == 0) {
        if (S_ISDIR(st.st_mode)) {
          if (directory_has_epubs_(fullpath)) {
            closedir(dir);
            return true;
          }
        } else if (S_ISREG(st.st_mode)) {
          size_t len = std::strlen(ent->d_name);
          if (len > 5 && strcasecmp(ent->d_name + len - 5, ".epub") == 0) {
            closedir(dir);
            return true;
          }
        }
      }
    }
    closedir(dir);
  }
#else
  try {
    for (const auto& entry : fs::recursive_directory_iterator(dir_path, fs::directory_options::skip_permission_denied)) {
      std::string fname = entry.path().filename().string();
      if (fname.empty() || fname[0] == '.') continue;
      if (entry.is_regular_file() && fname.size() > 5) {
        std::string ext = fname.substr(fname.size() - 5);
        for (char& c : ext) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        if (ext == ".epub") return true;
      }
    }
  } catch (...) {}
#endif

  return false;
}

MainMenu::MenuEntry MainMenu::make_book_entry_(const std::string& path) const {
  MenuEntry e;
  e.type = EntryType::Book;
  e.name = filename_sv(path);
  e.path = path;
  const StringPool& bpool = BookIndex::instance().pool();
  const BookIndexEntry* idx = BookIndex::instance().find_entry(path);
  if (idx) {
    e.title_ref = idx->title;
    e.author_ref = idx->author;
    e.last_open_order = idx->last_open_order;
  }

  std::string_view title = e.title_ref.view(bpool);
  std::string_view author = e.author_ref.view(bpool);

  if (list_format_ == BookListFormat::TitleOnly) {
    if (!title.empty()) e.display_name = title;
    else e.display_name = filename_sv(e.path);
  } else if (list_format_ == BookListFormat::Filename) {
    e.display_name = filename_sv(e.path);
  } else { // TitleAndAuthor
    if (!title.empty() && !author.empty()) {
      e.display_name = std::string(title) + " - " + std::string(author);
    } else if (!title.empty()) {
      e.display_name = title;
    } else {
      e.display_name = filename_sv(e.path);
    }
  }
  return e;
}

std::vector<MainMenu::MenuEntry> MainMenu::recent_books_(int count, const StringPool& bpool) const {
  std::vector<const BookIndexEntry*> opened;
  for (const auto& e : BookIndex::instance().entries())
    if (e.last_open_order > 0)
      opened.push_back(&e);
  std::stable_sort(opened.begin(), opened.end(),
                   [](const BookIndexEntry* a, const BookIndexEntry* b) {
                     return a->last_open_order > b->last_open_order;
                   });

  std::vector<MenuEntry> result;
  for (const auto* e : opened) {
    if (static_cast<int>(result.size()) >= count) break;
    result.push_back(make_book_entry_(e->path.to_string(bpool)));
  }
  return result;
}

void MainMenu::populate_list_() {
  clear_items();
  entries_.clear();
  separators_.clear();

  if (books_dir_.empty()) return;

  // If the last opened book was selected from the Recent section, keep the
  // cursor at the root (where the recent entry lives) instead of navigating
  // into the book's parent folder on return.
  const bool stay_at_root = opened_from_recents_;
  opened_from_recents_ = false;

  // Check if books_dir_ has a /books subfolder to default to
#ifndef ESP_PLATFORM
  fs::path books_subdir = fs::path(books_dir_) / "books";
  if (fs::exists(books_subdir) && fs::is_directory(books_subdir)) {
    std::string s = books_subdir.string();
    for (char& c : s) if (c == '\\') c = '/';
    books_dir_ = s;
  }
#else
  std::string books_subdir = books_dir_ + "/books";
  struct stat st;
  if (stat(books_subdir.c_str(), &st) == 0 && S_ISDIR(st.st_mode)) {
    books_dir_ = books_subdir;
  }
#endif

  if (!initial_selection_.empty()) {
    std::string target_path = initial_selection_;
    std::string parent_dir = get_parent_dir(target_path);

    if (stay_at_root || !is_sub_dir_of(parent_dir, books_dir_)) {
      current_dir_ = books_dir_;
    } else {
      current_dir_ = parent_dir;
    }
  }

  if (current_dir_.empty()) {
    current_dir_ = books_dir_;
  }

  std::vector<std::string> subdirs;
  std::vector<std::string> epubs;

#ifdef ESP_PLATFORM
  DIR* dir = opendir(current_dir_.c_str());
  if (dir) {
    struct dirent* ent;
    while ((ent = readdir(dir)) != nullptr) {
      if (ent->d_name[0] == '.') continue;
      std::string fullpath = current_dir_ + "/" + ent->d_name;
      struct stat st;
      if (stat(fullpath.c_str(), &st) == 0) {
        if (S_ISDIR(st.st_mode)) {
          subdirs.push_back(ent->d_name);
        } else if (S_ISREG(st.st_mode)) {
          size_t len = std::strlen(ent->d_name);
          if (len > 5) {
            const char* ext = ent->d_name + len - 5;
            if (strcasecmp(ext, ".epub") == 0) {
              epubs.push_back(ent->d_name);
            }
          }
        }
      }
    }
    closedir(dir);
  }
#else
  try {
    for (const auto& entry : fs::directory_iterator(current_dir_, fs::directory_options::skip_permission_denied)) {
      std::string fname = entry.path().filename().string();
      if (fname.empty() || fname[0] == '.') continue;
      std::string fullpath = entry.path().string();
      for (char& c : fullpath) if (c == '\\') c = '/';
      if (entry.is_directory()) {
        subdirs.push_back(fname);
      } else if (entry.is_regular_file()) {
        if (fname.size() > 5) {
          std::string ext = fname.substr(fname.size() - 5);
          for (char& c : ext) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
          if (ext == ".epub") {
            epubs.push_back(fname);
          }
        }
      }
    }
  } catch (...) {}
#endif

  std::sort(subdirs.begin(), subdirs.end(), [](const std::string& a, const std::string& b) {
    return ci_less(a, b);
  });

  const StringPool& bpool = BookIndex::instance().pool();
  const bool at_root = is_same_dir(current_dir_, books_dir_);

  separators_.clear();
  int real_count = 0;

  // ── Recent section (root folder only) ─────────────────────────────────────
  // Pins the globally-most-recently-opened books at the top so they can be
  // jumped to without navigating folders. Only shown at the root; subfolders
  // keep the plain folder list.
  std::vector<MenuEntry> recent =
      at_root ? recent_books_(kRecentCount, bpool) : std::vector<MenuEntry>();
  recent_count_ = static_cast<int>(recent.size());
  if (!recent.empty()) {
    for (auto& r : recent) {
      entries_.push_back(std::move(r));
      ++real_count;
    }
    // Thin divider below the recent section.
    SeparatorInfo div;
    div.before_count = real_count;
    separators_.push_back(div);
  }

  // ── Folder entries ────────────────────────────────────────────────────────
  if (!is_same_dir(current_dir_, books_dir_)) {
    MenuEntry up_entry;
    up_entry.type = EntryType::Directory;
    up_entry.name = "..";
    up_entry.path = get_parent_dir(current_dir_);
    up_entry.display_name = "/..";
    entries_.push_back(std::move(up_entry));
    ++real_count;
  }

  for (const auto& d : subdirs) {
    std::string fullpath = current_dir_ + "/" + d;
    if (!directory_has_epubs_(fullpath)) {
      continue;
    }
    MenuEntry e;
    e.type = EntryType::Directory;
    e.name = d;
    e.path = fullpath;
    e.display_name = "/" + d;
    entries_.push_back(std::move(e));
    ++real_count;
  }

  // ── Book entries ──────────────────────────────────────────────────────────
  std::vector<MenuEntry> book_entries;
  for (const auto& f : epubs) {
    book_entries.push_back(make_book_entry_(current_dir_ + "/" + f));
  }

  if (sort_order_ == BookSortOrder::LastOpened) {
    const auto fmt = list_format_;
    std::stable_sort(book_entries.begin(), book_entries.end(),
                     [&bpool, fmt](const MenuEntry& a, const MenuEntry& b) {
                      if (a.last_open_order != b.last_open_order)
                        return a.last_open_order > b.last_open_order;
                      if (fmt == BookListFormat::Filename)
                        return ci_less(filename_sv(a.path), filename_sv(b.path));
                      return ci_less(a.title_ref.view(bpool), b.title_ref.view(bpool));
                     });
  } else if (list_format_ == BookListFormat::Filename) {
    std::stable_sort(book_entries.begin(), book_entries.end(),
                     [](const MenuEntry& a, const MenuEntry& b) { return ci_less(filename_sv(a.path), filename_sv(b.path)); });
  } else {
    std::stable_sort(book_entries.begin(), book_entries.end(),
                     [&bpool](const MenuEntry& a, const MenuEntry& b) {
                      std::string_view ta = a.title_ref.view(bpool);
                      std::string_view tb = b.title_ref.view(bpool);
                      if (ta.empty()) ta = filename_sv(a.path);
                      if (tb.empty()) tb = filename_sv(b.path);
                      return ci_less(ta, tb);
                     });
  }

  const int book_start = real_count;
  for (auto& b : book_entries) {
    entries_.push_back(std::move(b));
    ++real_count;
  }

  // Separator between opened and never-opened books (LastOpened sort only).
  if (sort_order_ == BookSortOrder::LastOpened && !book_entries.empty()) {
    for (int i = 0; i < static_cast<int>(book_entries.size()); ++i) {
      if (i > 0 && book_entries[i].last_open_order == 0 &&
          book_entries[i - 1].last_open_order > 0) {
        SeparatorInfo s;
        s.before_count = book_start + i;
        separators_.push_back(s);
        break;
      }
    }
  }

  // Compute each separator's position in the combined visual list.
  std::stable_sort(separators_.begin(), separators_.end(),
                   [](const SeparatorInfo& a, const SeparatorInfo& b) {
                     return a.before_count < b.before_count;
                   });
  for (size_t i = 0; i < separators_.size(); ++i) {
    separators_[i].visual_index = separators_[i].before_count + static_cast<int>(i);
  }

  if (!initial_selection_.empty()) {
    for (int i = 0; i < static_cast<int>(entries_.size()); ++i) {
      if (entries_[i].path == initial_selection_ || is_same_dir(entries_[i].path, initial_selection_)) {
        set_selected(visual_for_entries(i));
        break;
      }
    }
    initial_selection_.clear();
  }
}

}  // namespace microreader
