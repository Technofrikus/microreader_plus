// FolderBrowsingTest.cpp — tests for MainMenu folder-based browsing and navigation.

#include <gtest/gtest.h>

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <string>

#include "microreader/Application.h"
#include "microreader/content/BookIndex.h"
#include "microreader/screens/MainMenu.h"

namespace fs = std::filesystem;
using namespace microreader;

class FolderBrowsingTest : public ::testing::Test {
 protected:
  fs::path root_dir_;
  fs::path data_dir_;
  // Persistent string so the const char* handed to set_data_dir() stays valid
  // for the whole test (Application stores it as a raw pointer).
  std::string data_dir_str_;

  void SetUp() override {
    root_dir_ = fs::temp_directory_path() / "mr_folder_test_root";
    data_dir_ = fs::temp_directory_path() / "mr_folder_test_data";
    data_dir_str_ = data_dir_.string();

    fs::remove_all(root_dir_);
    fs::remove_all(data_dir_);

    fs::create_directories(root_dir_ / "Fiction");
    fs::create_directories(root_dir_ / "Sci-Fi");
    fs::create_directories(data_dir_);

    // Create dummy .epub files
    std::ofstream(root_dir_ / "root_book.epub") << "dummy";
    std::ofstream(root_dir_ / "Fiction" / "dune.epub") << "dummy";
    std::ofstream(root_dir_ / "Sci-Fi" / "foundation.epub") << "dummy";
  }

  void TearDown() override {
    fs::remove_all(root_dir_);
    fs::remove_all(data_dir_);
  }
};

TEST_F(FolderBrowsingTest, FindEntry_BookIndex) {
  BookIndex& index = BookIndex::instance();
  std::string book_path = (root_dir_ / "Fiction" / "dune.epub").string();
  index.add_entry(book_path, "Dune", "Frank Herbert", 5);

  const BookIndexEntry* entry = index.find_entry(book_path);
  ASSERT_NE(entry, nullptr);
  EXPECT_EQ(entry->last_open_order, 5u);

  const BookIndexEntry* missing = index.find_entry("non_existent.epub");
  EXPECT_EQ(missing, nullptr);

  index.clear_entries();
}

TEST_F(FolderBrowsingTest, MainMenu_FolderList_FormatAndSorting) {
  Application app;
  app.set_data_dir(data_dir_str_.c_str());

  MainMenu menu;
  menu.set_app(&app);
  menu.set_books_dir(root_dir_.string().c_str());

  menu.set_align_left(true);
  EXPECT_TRUE(menu.align_left());

  std::string dune_path = (root_dir_ / "Fiction" / "dune.epub").string();
  std::string root_book_path = (root_dir_ / "root_book.epub").string();

  BookIndex::instance().add_entry(dune_path, "Dune", "Frank Herbert", 1);
  BookIndex::instance().add_entry(root_book_path, "Root Book Title", "Author Name", 2);

  std::string index_path = (data_dir_ / "book_index.dat").string();
  BookIndex::instance().save(index_path);

  // Restore initial selection to root_book
  menu.set_initial_selection(root_book_path.c_str());

  // Call test_on_start()
  menu.test_on_start();

  // Root layout (alphabetical sort):
  //   [0] "Recent" header (separator)
  //   [1] Root Book Title   (recent, order 2)
  //   [2] Dune              (recent, order 1)
  //   [3] thin divider (separator)
  //   [4] /Fiction
  //   [5] /Sci-Fi
  //   [6] Root Book Title   (the root folder's own book)
  int n = menu.count();
  EXPECT_EQ(n, 7);

  EXPECT_EQ(menu.get_item_label(0), "Recent");
  EXPECT_EQ(menu.get_item_label(1), "Root Book Title");
  EXPECT_EQ(menu.get_item_label(2), "Dune");
  EXPECT_EQ(menu.get_item_label(3), "");  // thin divider
  // Folders must have leading /
  EXPECT_EQ(menu.get_item_label(4), "/Fiction");
  EXPECT_EQ(menu.get_item_label(5), "/Sci-Fi");
  EXPECT_EQ(menu.get_item_label(6), "Root Book Title");

  // Initial selection lands on the most recent book (root_book, order 2).
  EXPECT_EQ(menu.selected_index(), 1);

  // Select /Fiction (index 4) -> navigate into Fiction
  menu.test_select(4);
  EXPECT_STREQ(menu.current_dir(), (root_dir_ / "Fiction").string().c_str());

  // In Fiction (not root): no Recent section — just /.. and dune.epub
  EXPECT_EQ(menu.count(), 2);
  EXPECT_EQ(menu.get_item_label(0), "/..");
  EXPECT_EQ(menu.get_item_label(1), "Dune");

  // Select /.. (index 0) -> navigate up to root_dir
  menu.test_select(0);
  EXPECT_STREQ(menu.current_dir(), root_dir_.string().c_str());
  EXPECT_EQ(menu.count(), 7);
  // Highlight should be restored to /Fiction (now index 4)
  EXPECT_EQ(menu.selected_index(), 4);

  BookIndex::instance().clear_entries();
}

TEST_F(FolderBrowsingTest, MainMenu_RecentSection_ShowsGlobalRecentBooksAtRoot) {
  Application app;
  app.set_data_dir(data_dir_str_.c_str());

  MainMenu menu;
  menu.set_app(&app);
  menu.set_books_dir(root_dir_.string().c_str());
  menu.set_align_left(true);

  std::string dune_path = (root_dir_ / "Fiction" / "dune.epub").string();
  std::string foundation_path = (root_dir_ / "Sci-Fi" / "foundation.epub").string();
  std::string root_book_path = (root_dir_ / "root_book.epub").string();

  // Mark books as opened with distinct recency. dune lives in a subfolder, so
  // it would not appear in the root folder list — only the Recent section.
  BookIndex::instance().add_entry(dune_path, "Dune", "Frank Herbert", 3);
  BookIndex::instance().add_entry(foundation_path, "Foundation", "Asimov", 2);
  BookIndex::instance().add_entry(root_book_path, "Root Book Title", "Author Name", 1);

  std::string index_path = (data_dir_ / "book_index.dat").string();
  BookIndex::instance().save(index_path);

  // Restore selection to a book in the root folder so current_dir_ stays at
  // root (restoring a subfolder book would navigate into that subfolder).
  menu.set_initial_selection(root_book_path.c_str());
  menu.test_on_start();

  // Root layout (alphabetical sort):
  //   [0] "Recent" header
  //   [1] Dune        (order 3)
  //   [2] Foundation  (order 2)
  //   [3] Root Book Title (order 1)
  //   [4] thin divider
  //   [5] /Fiction
  //   [6] /Sci-Fi
  //   [7] Root Book Title (root folder's own book)
  EXPECT_EQ(menu.count(), 8);
  EXPECT_EQ(menu.get_item_label(0), "Recent");
  EXPECT_EQ(menu.get_item_label(1), "Dune");
  EXPECT_EQ(menu.get_item_label(2), "Foundation");
  EXPECT_EQ(menu.get_item_label(3), "Root Book Title");
  EXPECT_EQ(menu.get_item_label(4), "");
  EXPECT_EQ(menu.get_item_label(5), "/Fiction");
  EXPECT_EQ(menu.get_item_label(6), "/Sci-Fi");
  EXPECT_EQ(menu.get_item_label(7), "Root Book Title");

  // Cursor lands on the restored book (root_book, the recent entry at index 3).
  EXPECT_EQ(menu.selected_index(), 3);

  // Selecting a Recent entry opens the book directly (no folder navigation).
  // Dune is in Fiction/, so opening it must jump straight to the Reader.
  menu.test_select(1);
  EXPECT_STREQ(menu.last_selected_book_path().c_str(), dune_path.c_str());

  BookIndex::instance().clear_entries();
}

TEST_F(FolderBrowsingTest, MainMenu_RecentSection_NotShownInSubfolder) {
  Application app;
  app.set_data_dir(data_dir_str_.c_str());

  MainMenu menu;
  menu.set_app(&app);
  menu.set_books_dir(root_dir_.string().c_str());
  menu.set_align_left(true);

  std::string dune_path = (root_dir_ / "Fiction" / "dune.epub").string();
  std::string root_book_path = (root_dir_ / "root_book.epub").string();

  BookIndex::instance().add_entry(dune_path, "Dune", "Frank Herbert", 3);
  BookIndex::instance().add_entry(root_book_path, "Root Book Title", "Author Name", 1);
  std::string index_path = (data_dir_ / "book_index.dat").string();
  BookIndex::instance().save(index_path);

  menu.test_on_start();

  // Navigate into Fiction. The Recent section must NOT appear in a subfolder.
  menu.test_select(4);  // /Fiction (index 4 at root)
  EXPECT_STREQ(menu.current_dir(), (root_dir_ / "Fiction").string().c_str());
  EXPECT_EQ(menu.count(), 2);
  EXPECT_EQ(menu.get_item_label(0), "/..");
  EXPECT_EQ(menu.get_item_label(1), "Dune");

  BookIndex::instance().clear_entries();
}

TEST_F(FolderBrowsingTest, MainMenu_ReturnFromRecents_StaysAtRoot) {
  Application app;
  app.set_data_dir(data_dir_str_.c_str());

  MainMenu menu;
  menu.set_app(&app);
  menu.set_books_dir(root_dir_.string().c_str());
  menu.set_align_left(true);

  std::string dune_path = (root_dir_ / "Fiction" / "dune.epub").string();
  std::string root_book_path = (root_dir_ / "root_book.epub").string();

  BookIndex::instance().add_entry(dune_path, "Dune", "Frank Herbert", 3);
  BookIndex::instance().add_entry(root_book_path, "Root Book Title", "Author Name", 1);
  std::string index_path = (data_dir_ / "book_index.dat").string();
  BookIndex::instance().save(index_path);

  menu.set_initial_selection(root_book_path.c_str());
  menu.test_on_start();

  // Root layout: [0] Recent, [1] Dune, [2] Root Book Title, [3] divider,
  //              [4] /Fiction, [5] /Sci-Fi, [6] Root Book Title
  EXPECT_EQ(menu.count(), 7);

  // Open Dune from the Recent section (index 1).
  menu.test_select(1);
  EXPECT_STREQ(menu.last_selected_book_path().c_str(), dune_path.c_str());

  // Simulate returning from the Reader: pause() -> stop() then resume() -> start().
  menu.stop();
  menu.set_initial_selection(dune_path.c_str());
  menu.test_on_start();

  // Because Dune was opened from recents, we stay at the root (not Fiction/),
  // and the cursor lands back on Dune in the Recent section.
  EXPECT_STREQ(menu.current_dir(), root_dir_.string().c_str());
  EXPECT_EQ(menu.count(), 7);
  EXPECT_EQ(menu.selected_index(), 1);

  BookIndex::instance().clear_entries();
}

TEST_F(FolderBrowsingTest, MainMenu_ReturnFromFolder_StaysInFolder) {
  Application app;
  app.set_data_dir(data_dir_str_.c_str());

  MainMenu menu;
  menu.set_app(&app);
  menu.set_books_dir(root_dir_.string().c_str());
  menu.set_align_left(true);

  std::string dune_path = (root_dir_ / "Fiction" / "dune.epub").string();
  std::string root_book_path = (root_dir_ / "root_book.epub").string();

  BookIndex::instance().add_entry(dune_path, "Dune", "Frank Herbert", 3);
  BookIndex::instance().add_entry(root_book_path, "Root Book Title", "Author Name", 1);
  std::string index_path = (data_dir_ / "book_index.dat").string();
  BookIndex::instance().save(index_path);

  menu.set_initial_selection(root_book_path.c_str());
  menu.test_on_start();

  // Navigate into Fiction, then open Dune from within the folder (index 1).
  menu.test_select(4);  // /Fiction
  EXPECT_STREQ(menu.current_dir(), (root_dir_ / "Fiction").string().c_str());
  menu.test_select(1);  // Dune
  EXPECT_STREQ(menu.last_selected_book_path().c_str(), dune_path.c_str());

  // Simulate returning from the Reader.
  menu.stop();
  menu.set_initial_selection(dune_path.c_str());
  menu.test_on_start();

  // Opened from within the folder: return to Fiction/ with cursor on Dune.
  EXPECT_STREQ(menu.current_dir(), (root_dir_ / "Fiction").string().c_str());
  EXPECT_EQ(menu.count(), 2);
  EXPECT_EQ(menu.selected_index(), 1);

  BookIndex::instance().clear_entries();
}
