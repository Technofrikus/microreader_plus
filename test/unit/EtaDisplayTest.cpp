#include <gtest/gtest.h>

#include "microreader/screens/EtaDisplay.h"

namespace microreader::eta_display {

TEST(EtaDisplayTest, FineEtaUsesUnderOneMinuteForSubMinuteDurations) {
  EXPECT_EQ(fine_minutes(0), 0);
  EXPECT_EQ(fine_minutes(59999), 0);
  EXPECT_EQ(fine_minutes(60000), 1);
}

TEST(EtaDisplayTest, CoarseBookEtaPreservesUnderOneMinute) {
  EXPECT_EQ(coarse_book_minutes(0), 0);
  EXPECT_EQ(coarse_book_minutes(59999), 0);
  EXPECT_EQ(coarse_book_minutes(60000), 1);
}

TEST(EtaDisplayTest, CoarseBookEtaRetainsFiveAndTenMinuteSteps) {
  EXPECT_EQ(coarse_book_minutes(61u * 60000u), 60);
  EXPECT_EQ(coarse_book_minutes(64u * 60000u), 65);
  EXPECT_EQ(coarse_book_minutes(181u * 60000u), 180);
  EXPECT_EQ(coarse_book_minutes(186u * 60000u), 190);
}

TEST(EtaDisplayTest, FinalChapterUsesFineEtaOnlyAfterTheSmoothTransition) {
  constexpr uint64_t kFinalSyncWindowMs = 15u * 60000u;
  EXPECT_FALSE(use_fine_book_eta_in_final_chapter(false, 10u * 60000u, kFinalSyncWindowMs));
  EXPECT_FALSE(use_fine_book_eta_in_final_chapter(true, 16u * 60000u, kFinalSyncWindowMs));
  EXPECT_TRUE(use_fine_book_eta_in_final_chapter(true, 15u * 60000u, kFinalSyncWindowMs));
}

TEST(EtaDisplayTest, FormatMinutesIsSharedForBookAndChapter) {
  char out[16];
  format_minutes(0, out, sizeof(out));
  EXPECT_STREQ(out, "<1m");
  format_minutes(65, out, sizeof(out));
  EXPECT_STREQ(out, "1h 5m");
}

}  // namespace microreader::eta_display
