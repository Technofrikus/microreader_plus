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

  void SetUp() override {
    root_dir_ = fs::temp_directory_path() / "mr_folder_test_root";
    data_dir_ = fs::temp_directory_path() / "mr_folder_test_data";

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
  app.set_data_dir(data_dir_.string().c_str());

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

  // Count items at root: /Fiction, /Sci-Fi, and Root Book Title
  int n = menu.count();
  EXPECT_EQ(n, 3);

  std::string_view item0 = menu.get_item_label(0);
  std::string_view item1 = menu.get_item_label(1);
  std::string_view item2 = menu.get_item_label(2);

  // Folders must have leading /
  EXPECT_EQ(item0, "/Fiction");
  EXPECT_EQ(item1, "/Sci-Fi");
  EXPECT_EQ(item2, "Root Book Title");

  // Select item 0 (/Fiction) -> navigate into Fiction
  menu.test_select(0);
  EXPECT_STREQ(menu.current_dir(), (root_dir_ / "Fiction").string().c_str());

  // In Fiction: /.. and dune.epub
  EXPECT_EQ(menu.count(), 2);
  EXPECT_EQ(menu.get_item_label(0), "/..");
  EXPECT_EQ(menu.get_item_label(1), "Dune");

  // Select /.. (index 0) -> navigate up to root_dir
  menu.test_select(0);
  EXPECT_STREQ(menu.current_dir(), root_dir_.string().c_str());
  EXPECT_EQ(menu.count(), 3);
  // Highlight should be restored to /Fiction (index 0)
  EXPECT_EQ(menu.selected_index(), 0);

  BookIndex::instance().clear_entries();
}
