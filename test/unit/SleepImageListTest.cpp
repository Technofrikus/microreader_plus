#include <gtest/gtest.h>

#include <cstdio>
#include <filesystem>
#include <string>
#include <vector>

#include "microreader/content/SleepImageList.h"

using namespace microreader;

namespace {

namespace fs = std::filesystem;

// Scratch sleep folder next to the fixtures, so the suite stays runnable from
// any working directory (the same convention the other tests use).
class SleepImageListTest : public ::testing::Test {
 protected:
  void SetUp() override {
    dir_ = std::string(TEST_FIXTURES_DIR) + "/sleep_list_tmp";
    fs::remove_all(dir_);
    fs::create_directories(dir_);
  }
  void TearDown() override { fs::remove_all(dir_); }

  void touch(const char* name) {
    std::FILE* f = std::fopen((dir_ + "/" + name).c_str(), "wb");
    ASSERT_NE(f, nullptr);
    std::fclose(f);
  }

  std::string dir_;
};

}  // namespace

TEST_F(SleepImageListTest, ListsMgrAndBmpOnly) {
  touch("bird.mgr");
  touch("stone.bmp");
  touch("notes.txt");
  touch("readme");

  const auto images = list_sleep_images(dir_.c_str());
  ASSERT_EQ(images.size(), 2u);
  EXPECT_EQ(images[0].file, "bird.mgr");
  EXPECT_FALSE(images[0].bmp);
  EXPECT_EQ(images[1].file, "stone.bmp");
  EXPECT_TRUE(images[1].bmp);
}

TEST_F(SleepImageListTest, SortedCaseInsensitively) {
  touch("Zebra.mgr");
  touch("apple.mgr");
  touch("Banana.bmp");

  const auto images = list_sleep_images(dir_.c_str());
  ASSERT_EQ(images.size(), 3u);
  EXPECT_EQ(images[0].label(), "apple");
  EXPECT_EQ(images[1].label(), "Banana");
  EXPECT_EQ(images[2].label(), "Zebra");
}

TEST_F(SleepImageListTest, HidesDotFiles) {
  touch(".hidden.mgr");
  touch("visible.mgr");

  const auto images = list_sleep_images(dir_.c_str());
  ASSERT_EQ(images.size(), 1u);
  EXPECT_EQ(images[0].file, "visible.mgr");
}

TEST_F(SleepImageListTest, RespectsMaxEntries) {
  touch("a.mgr");
  touch("b.mgr");
  touch("c.mgr");

  EXPECT_EQ(list_sleep_images(dir_.c_str(), 2).size(), 2u);
}

TEST_F(SleepImageListTest, MissingDirectoryIsEmptyNotAnError) {
  EXPECT_TRUE(list_sleep_images((dir_ + "/nope").c_str()).empty());
}

// The value is what Application::sleep_image_path() persists, so its exact
// spelling decides whether a saved setting still resolves after a reboot.
TEST(SleepImageEntryTest, MgrValueIsABarePath) {
  const SleepImageEntry e{"bird.mgr", false};
  EXPECT_EQ(e.value("/sdcard/sleep"), "/sdcard/sleep/bird.mgr");
}

TEST(SleepImageEntryTest, BmpValueCarriesTheConversionPrefix) {
  const SleepImageEntry e{"stone.bmp", true};
  EXPECT_EQ(e.value("/sdcard/sleep"), "bmp:/sdcard/sleep/stone.bmp");
}

TEST(SleepImageEntryTest, LabelDropsTheExtension) {
  EXPECT_EQ(SleepImageEntry({"bird.mgr", false}).label(), "bird");
  EXPECT_EQ(SleepImageEntry({"cabin.in.snow.bmp", true}).label(), "cabin.in.snow");
  EXPECT_EQ(SleepImageEntry({"noext", false}).label(), "noext");
}
