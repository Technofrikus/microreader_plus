#include <gtest/gtest.h>

#include <array>
#include <cstring>

#include "microreader/Application.h"

namespace {

TEST(ApplicationFontTest, SwitchingToSmallerBundleClampsReaderSetting) {
  constexpr size_t kFontCount = 4;
  std::array<std::array<uint8_t, sizeof(microreader::MbfHeader)>, kFontCount> data{};
  std::array<microreader::BitmapFont, kFontCount> fonts;
  microreader::BitmapFontSet font_set;

  for (size_t i = 0; i < kFontCount; ++i) {
    microreader::MbfHeader header{};
    header.magic = microreader::kMbfMagic;
    header.version = microreader::kMbfVersion;
    header.nominal_size = static_cast<uint16_t>(20 + i * 4);
    header.y_advance = static_cast<uint8_t>(header.nominal_size + 4);
    header.bitmap_data_offset = sizeof(microreader::MbfHeader);
    std::memcpy(data[i].data(), &header, sizeof(header));
    fonts[i].init(data[i].data(), data[i].size());
    ASSERT_TRUE(fonts[i].valid());
    font_set.add(&fonts[i]);
  }

  microreader::Application app;
  app.reader_settings().font_size_idx = 5;
  app.set_reader_font(&font_set);

  EXPECT_EQ(app.reader_settings().font_size_idx, 3);
}

}  // namespace
