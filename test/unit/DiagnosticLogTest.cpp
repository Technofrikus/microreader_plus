#include <chrono>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>

#include <gtest/gtest.h>

#include "microreader/DiagnosticLog.h"

namespace fs = std::filesystem;

namespace {

class DiagnosticLogTest : public ::testing::Test {
 protected:
  void SetUp() override {
    const auto nonce = std::chrono::steady_clock::now().time_since_epoch().count();
    dir_ = fs::temp_directory_path() / ("microreader_diag_" + std::to_string(nonce));
    fs::create_directories(dir_);
  }

  void TearDown() override {
    std::error_code ec;
    fs::remove_all(dir_, ec);
  }

  std::string read(const fs::path& path) {
    std::ifstream in(path, std::ios::binary);
    std::ostringstream out;
    out << in.rdbuf();
    return out.str();
  }

  fs::path dir_;
};

TEST_F(DiagnosticLogTest, WritesDurableSanitizedRecords) {
  microreader::diag::init(dir_.string().c_str());
  microreader::diag::event("unit", "value=%d|second\nline", 7);

  const fs::path log = dir_ / "diagnostics" / "diag.log";
  ASSERT_TRUE(fs::exists(log));
  const std::string contents = read(log);
  EXPECT_NE(contents.find("session|uptime_ms|seq|tag|message\n"), std::string::npos);
  EXPECT_NE(contents.find("|unit|value=7 second line\n"), std::string::npos);
}

TEST_F(DiagnosticLogTest, RotatesAndBoundsOldGenerations) {
  microreader::diag::init(dir_.string().c_str());
  const std::string payload(210, 'x');
  for (int i = 0; i < 5200; ++i)
    microreader::diag::event("rotation", "i=%d %s", i, payload.c_str());

  const fs::path logs = dir_ / "diagnostics";
  EXPECT_TRUE(fs::exists(logs / "diag.log"));
  EXPECT_TRUE(fs::exists(logs / "diag.1.log"));
  EXPECT_TRUE(fs::exists(logs / "diag.2.log"));
  EXPECT_TRUE(fs::exists(logs / "diag.3.log"));
  EXPECT_FALSE(fs::exists(logs / "diag.4.log"));

  uintmax_t total = 0;
  for (unsigned generation = 0; generation < 4; ++generation) {
    const fs::path path = generation == 0 ? logs / "diag.log" : logs / ("diag." + std::to_string(generation) + ".log");
    total += fs::file_size(path);
  }
  EXPECT_LT(total, 4u * 270u * 1024u);
}

}  // namespace
