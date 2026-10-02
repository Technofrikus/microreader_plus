#include <gtest/gtest.h>

#include <algorithm>
#include <cstring>
#include <string>
#include <vector>

#include "microreader/content/CssParser.h"

using namespace microreader;

// ---------------------------------------------------------------------------
// CssRule::parse
// ---------------------------------------------------------------------------

TEST(CssRule, Empty) {
  auto rule = CssRule::parse("");
  EXPECT_FALSE(bool(rule.has_any()));
}

TEST(CssRule, TextAlign) {
  auto rule = CssRule::parse("text-align: center");
  ASSERT_TRUE(rule.has_alignment_);
  EXPECT_EQ(rule.alignment, Alignment::Center);
}

TEST(CssRule, FontWeight) {
  auto rule = CssRule::parse("font-weight: bold");
  ASSERT_TRUE(rule.has_bold_);
  EXPECT_TRUE(bool(rule.bold));
}

TEST(CssRule, FontStyle) {
  auto rule = CssRule::parse("font-style: italic");
  ASSERT_TRUE(rule.has_italic_);
  EXPECT_TRUE(bool(rule.italic));
}

TEST(CssRule, TextIndent) {
  auto rule = CssRule::parse("text-indent: 20px");
  ASSERT_TRUE(bool(rule.has_indent_));
  EXPECT_EQ(rule.indent, 20);
}

TEST(CssRule, MultipleDeclarations) {
  auto rule = CssRule::parse("text-align: justify; font-weight: bold; font-style: italic; text-indent: 10px");
  ASSERT_TRUE(rule.has_alignment_);
  EXPECT_EQ(rule.alignment, Alignment::Justify);
  ASSERT_TRUE(rule.has_bold_);
  EXPECT_TRUE(bool(rule.bold));
  ASSERT_TRUE(rule.has_italic_);
  EXPECT_TRUE(bool(rule.italic));
  ASSERT_TRUE(bool(rule.has_indent_));
  EXPECT_EQ(rule.indent, 10);
}

TEST(CssRule, AlignmentLeft) {
  EXPECT_EQ(CssRule::parse("text-align: left").alignment, Alignment::Start);
}

TEST(CssRule, AlignmentRight) {
  EXPECT_EQ(CssRule::parse("text-align: right").alignment, Alignment::End);
}

TEST(CssRule, AlignmentStart) {
  EXPECT_EQ(CssRule::parse("text-align: start").alignment, Alignment::Start);
}

TEST(CssRule, NormalWeight) {
  auto rule = CssRule::parse("font-weight: normal");
  ASSERT_TRUE(rule.has_bold_);
  EXPECT_FALSE(bool(rule.bold));
}

TEST(CssRule, NormalStyle) {
  auto rule = CssRule::parse("font-style: normal");
  ASSERT_TRUE(rule.has_italic_);
  EXPECT_FALSE(bool(rule.italic));
}

TEST(CssRule, Plus) {
  CssRule a;
  a.set_alignment(Alignment::Center);
  CssRule b;
  b.set_bold(true);
  auto c = a + b;
  EXPECT_EQ(c.alignment, Alignment::Center);
  EXPECT_TRUE(bool(c.bold));
}

TEST(CssRule, PlusOverrides) {
  CssRule a;
  a.set_alignment(Alignment::Center);
  a.set_bold(false);
  CssRule b;
  b.set_alignment(Alignment::Justify);
  auto c = a + b;
  EXPECT_EQ(c.alignment, Alignment::Justify);
  EXPECT_FALSE(bool(c.bold));  // not overridden
}

// ---------------------------------------------------------------------------
// CssConfig: em and % conversion
// ---------------------------------------------------------------------------

TEST(CssRule, TextIndentEm) {
  CssConfig config{10, 400};  // 10px glyph, 400px content
  auto rule = CssRule::parse("text-indent: 1.25em", config);
  ASSERT_TRUE(bool(rule.has_indent_));
  EXPECT_EQ(rule.indent, 13);  // 1.25 * 10 + 0.5 = 13
}

TEST(CssRule, TextIndentPercent) {
  CssConfig config{10, 400};
  auto rule = CssRule::parse("text-indent: 5%", config);
  ASSERT_TRUE(bool(rule.has_indent_));
  EXPECT_EQ(rule.indent, 20);  // 5 * 400 / 100 = 20
}

TEST(CssRule, MarginLeftEm) {
  CssConfig config{10, 400};
  auto rule = CssRule::parse("margin-left: 3em", config);
  ASSERT_TRUE(bool(rule.has_margin_left_));
  EXPECT_EQ(rule.margin_left, 30);  // 3 * 10 = 30
}

TEST(CssRule, MarginLeftPercentClamped) {
  CssConfig config{10, 400};
  auto rule = CssRule::parse("margin-left: 30%", config);
  ASSERT_TRUE(bool(rule.has_margin_left_));
  EXPECT_EQ(rule.margin_left, 120);  // 30% of 400 = 120
}

TEST(CssRule, MarginLeftPercentUnderMax) {
  CssConfig config{10, 400};
  auto rule = CssRule::parse("margin-left: 5%", config);
  ASSERT_TRUE(bool(rule.has_margin_left_));
  EXPECT_EQ(rule.margin_left, 20);  // 5% of 400 = 20, under max
}

TEST(CssRule, MarginRightEm) {
  CssConfig config{10, 400};
  auto rule = CssRule::parse("margin-right: 2em", config);
  ASSERT_TRUE(rule.has_margin_right_);
  EXPECT_EQ(rule.margin_right, 20);  // 2 * 10 = 20
}

TEST(CssRule, MarginRightPercentClamped) {
  CssConfig config{10, 400};
  auto rule = CssRule::parse("margin-right: 20%", config);
  ASSERT_TRUE(rule.has_margin_right_);
  EXPECT_EQ(rule.margin_right, 80);  // 20% of 400 = 80
}

TEST(CssRule, BothMarginsClampedProportionally) {
  CssConfig config{10, 400, 15};  // 15% budget = 60px total
  auto rule = CssRule::parse("margin-left: 30%; margin-right: 10%", config);
  ASSERT_TRUE(bool(rule.has_margin_left_));
  ASSERT_TRUE(rule.has_margin_right_);
  // 30% of 400 = 120, 10% of 400 = 40. Total 160 > 60. Scale = 60/160 = 0.375
  EXPECT_EQ(rule.margin_left, 45);   // 120 * 0.375 = 45
  EXPECT_EQ(rule.margin_right, 15);  // 40 * 0.375 = 15
}

TEST(CssRule, BothMarginsUnderBudgetNotClamped) {
  CssConfig config{10, 400, 15};  // 15% budget = 60px total
  auto rule = CssRule::parse("margin-left: 5%; margin-right: 5%", config);
  ASSERT_TRUE(bool(rule.has_margin_left_));
  ASSERT_TRUE(rule.has_margin_right_);
  EXPECT_EQ(rule.margin_left, 20);  // 5% of 400 = 20, under budget
  EXPECT_EQ(rule.margin_right, 20);
}

TEST(CssRule, SingleMarginNotClamped) {
  CssConfig config{10, 400, 15};
  // Only margin-left set — no combined clamping
  auto rule = CssRule::parse("margin-left: 30%", config);
  ASSERT_TRUE(bool(rule.has_margin_left_));
  EXPECT_FALSE(rule.has_margin_right_);
  EXPECT_EQ(rule.margin_left, 120);  // 30% of 400 = 120, unclamped
}

// ---------------------------------------------------------------------------
// CssStylesheet
// ---------------------------------------------------------------------------

TEST(CssStylesheet, BasicElementRule) {
  CssStylesheet sheet;
  sheet.extend_from_sheet("p { text-align: justify; }");
  auto rule = sheet.get("p", nullptr, nullptr);
  ASSERT_TRUE(rule.has_alignment_);
  EXPECT_EQ(rule.alignment, Alignment::Justify);
}

TEST(CssStylesheet, NoMatch) {
  CssStylesheet sheet;
  sheet.extend_from_sheet("p { text-align: justify; }");
  auto rule = sheet.get("div", nullptr, nullptr);
  EXPECT_FALSE(bool(rule.has_any()));
}

TEST(CssStylesheet, ClassSelector) {
  CssStylesheet sheet;
  sheet.extend_from_sheet(".bold { font-weight: bold; }");
  auto rule = sheet.get("span", nullptr, "bold");
  ASSERT_TRUE(rule.has_bold_);
  EXPECT_TRUE(bool(rule.bold));
}

TEST(CssStylesheet, IdSelector) {
  CssStylesheet sheet;
  sheet.extend_from_sheet("#title { text-align: center; }");
  auto rule = sheet.get("h1", "title", nullptr);
  ASSERT_TRUE(rule.has_alignment_);
  EXPECT_EQ(rule.alignment, Alignment::Center);
}

TEST(CssStylesheet, CompoundSelector) {
  CssStylesheet sheet;
  sheet.extend_from_sheet("p.intro { font-style: italic; }");

  auto rule = sheet.get("p", nullptr, "intro");
  ASSERT_TRUE(rule.has_italic_);
  EXPECT_TRUE(bool(rule.italic));

  // Should not match div
  auto no_match = sheet.get("div", nullptr, "intro");
  EXPECT_FALSE(bool(no_match.has_any()));
}

TEST(CssStylesheet, MultipleRules) {
  CssStylesheet sheet;
  sheet.extend_from_sheet(
      "p { text-align: justify; }\n"
      "h1 { font-weight: bold; text-align: center; }\n"
      ".italic { font-style: italic; }\n");

  auto p_rule = sheet.get("p", nullptr, nullptr);
  EXPECT_EQ(p_rule.alignment, Alignment::Justify);

  auto h1_rule = sheet.get("h1", nullptr, nullptr);
  EXPECT_EQ(h1_rule.alignment, Alignment::Center);
  EXPECT_TRUE(bool(h1_rule.bold));
}

TEST(CssStylesheet, Comments) {
  CssStylesheet sheet;
  sheet.extend_from_sheet("/* comment */ p { /* inline */ text-align: center; }");
  auto rule = sheet.get("p", nullptr, nullptr);
  ASSERT_TRUE(rule.has_alignment_);
  EXPECT_EQ(rule.alignment, Alignment::Center);
}

TEST(CssStylesheet, AtRuleSkipped) {
  CssStylesheet sheet;
  sheet.extend_from_sheet(
      "@charset \"utf-8\";\n"
      "@import url(\"styles.css\");\n"
      "p { text-align: justify; }");
  auto rule = sheet.get("p", nullptr, nullptr);
  ASSERT_TRUE(rule.has_alignment_);
  EXPECT_EQ(rule.alignment, Alignment::Justify);
}

TEST(CssStylesheet, MediaQuerySkipped) {
  CssStylesheet sheet;
  sheet.extend_from_sheet(
      "@media screen { body { margin: 0; } }\n"
      "p { font-weight: bold; }");
  auto rule = sheet.get("p", nullptr, nullptr);
  ASSERT_TRUE(rule.has_bold_);
  EXPECT_TRUE(bool(rule.bold));
}

TEST(CssStylesheet, GroupedSelectors) {
  CssStylesheet sheet;
  sheet.extend_from_sheet("h1, h2, h3 { font-weight: bold; }");

  EXPECT_TRUE(sheet.get("h1", nullptr, nullptr).bold);
  EXPECT_TRUE(sheet.get("h2", nullptr, nullptr).bold);
  EXPECT_TRUE(sheet.get("h3", nullptr, nullptr).bold);
  EXPECT_FALSE(sheet.get("h4", nullptr, nullptr).has_any());
}

TEST(CssStylesheet, Specificity) {
  CssStylesheet sheet;
  sheet.extend_from_sheet(
      "p { text-align: left; }\n"
      ".center { text-align: center; }\n"
      "#main { text-align: right; }\n");

  // #main has highest specificity
  auto rule = sheet.get("p", "main", "center");
  EXPECT_EQ(rule.alignment, Alignment::End);  // right
}

TEST(CssStylesheet, MultipleClasses) {
  CssStylesheet sheet;
  sheet.extend_from_sheet(".bold { font-weight: bold; } .italic { font-style: italic; }");

  auto rule = sheet.get("span", nullptr, "bold italic");
  EXPECT_TRUE(bool(rule.bold));
  EXPECT_TRUE(bool(rule.italic));
}

// ---------------------------------------------------------------------------
// Font-size parsing
// ---------------------------------------------------------------------------

TEST(CssParserTest, ParseFontSizeSmall) {
  auto rule = CssRule::parse("font-size: small");
  ASSERT_TRUE(rule.has_font_size_pct_);
  EXPECT_EQ(rule.font_size_pct, 80);
}

TEST(CssParserTest, ParseFontSizeLarge) {
  auto rule = CssRule::parse("font-size: large");
  ASSERT_TRUE(rule.has_font_size_pct_);
  EXPECT_EQ(rule.font_size_pct, 120);
}

TEST(CssParserTest, ParseFontSizeXLarge) {
  auto rule = CssRule::parse("font-size: x-large");
  ASSERT_TRUE(rule.has_font_size_pct_);
  EXPECT_EQ(rule.font_size_pct, 140);
}

TEST(CssParserTest, ParseFontSizeSmaller) {
  auto rule = CssRule::parse("font-size: smaller");
  ASSERT_TRUE(rule.has_font_size_pct_);
  EXPECT_EQ(rule.font_size_pct, 80);
}

TEST(CssParserTest, ParseFontSizeMedium) {
  auto rule = CssRule::parse("font-size: medium");
  ASSERT_TRUE(rule.has_font_size_pct_);
  EXPECT_EQ(rule.font_size_pct, 100);
}

TEST(CssParserTest, FontSize14px) {
  auto rule = CssRule::parse("font-size: 14px");
  ASSERT_TRUE(rule.has_font_size_pct_);
  EXPECT_EQ(rule.font_size_pct, 58);
}

TEST(CssParserTest, FontSizeMerge) {
  CssRule a;
  a.set_font_size_pct(80);
  CssRule b;
  b.set_font_size_pct(120);

  auto merged = a + b;
  ASSERT_TRUE(merged.has_font_size_pct_);
  EXPECT_EQ(merged.font_size_pct, 120);  // rhs wins
}

TEST(CssParserTest, FontSizeMergePreservesLhs) {
  CssRule a;
  a.set_font_size_pct(80);
  CssRule b;

  auto merged = a + b;
  ASSERT_TRUE(merged.has_font_size_pct_);
  EXPECT_EQ(merged.font_size_pct, 80);  // lhs preserved when rhs empty
}

TEST(CssParserTest, FontSizeInStylesheet) {
  CssStylesheet sheet;
  sheet.extend_from_sheet("h1 { font-size: large; } .footnote { font-size: small; }");

  auto h1 = sheet.get("h1", nullptr, nullptr);
  ASSERT_TRUE(h1.has_font_size_pct_);
  EXPECT_EQ(h1.font_size_pct, 120);

  auto fn = sheet.get("span", nullptr, "footnote");
  ASSERT_TRUE(fn.has_font_size_pct_);
  EXPECT_EQ(fn.font_size_pct, 80);
}

// ---------------------------------------------------------------------------
// Font-size: percentage values
// ---------------------------------------------------------------------------

TEST(CssParserTest, FontSizePercent90) {
  // 90% is within the normal band (90-105%)
  auto rule = CssRule::parse("font-size: 90%");
  ASSERT_TRUE(rule.has_font_size_pct_);
  EXPECT_EQ(rule.font_size_pct, 90);
}

TEST(CssParserTest, FontSizePercent60) {
  auto rule = CssRule::parse("font-size: 60%");
  ASSERT_TRUE(rule.has_font_size_pct_);
  EXPECT_EQ(rule.font_size_pct, 60);
}

TEST(CssParserTest, FontSizePercent100IsNormal) {
  auto rule = CssRule::parse("font-size: 100%");
  ASSERT_TRUE(rule.has_font_size_pct_);
  EXPECT_EQ(rule.font_size_pct, 100);
}

TEST(CssParserTest, FontSizePercent110) {
  auto rule = CssRule::parse("font-size: 110%");
  ASSERT_TRUE(rule.has_font_size_pct_);
  EXPECT_EQ(rule.font_size_pct, 110);
}

TEST(CssParserTest, FontSizePercent120) {
  auto rule = CssRule::parse("font-size: 120%");
  ASSERT_TRUE(rule.has_font_size_pct_);
  EXPECT_EQ(rule.font_size_pct, 120);
}

TEST(CssParserTest, FontSizePercent150) {
  auto rule = CssRule::parse("font-size: 150%");
  ASSERT_TRUE(rule.has_font_size_pct_);
  EXPECT_EQ(rule.font_size_pct, 150);
}

TEST(CssParserTest, FontSizePercent300) {
  auto rule = CssRule::parse("font-size: 300%");
  ASSERT_TRUE(rule.has_font_size_pct_);
  EXPECT_EQ(rule.font_size_pct, 250);
}

// ---------------------------------------------------------------------------
// Font-size: em values
// ---------------------------------------------------------------------------

TEST(CssParserTest, FontSizeEm09) {
  auto rule = CssRule::parse("font-size: 0.9em");
  ASSERT_TRUE(rule.has_font_size_pct_);
  EXPECT_EQ(rule.font_size_pct, 90);
}

TEST(CssParserTest, FontSizeEm175) {
  auto rule = CssRule::parse("font-size: 1.75em");
  ASSERT_TRUE(rule.has_font_size_pct_);
  EXPECT_EQ(rule.font_size_pct, 175);
}

// ---------------------------------------------------------------------------
// Font-size: boundary values
// ---------------------------------------------------------------------------

TEST(CssParserTest, FontSizePercent85) {
  auto rule = CssRule::parse("font-size: 85%");
  ASSERT_TRUE(rule.has_font_size_pct_);
  EXPECT_EQ(rule.font_size_pct, 85);
}

TEST(CssParserTest, FontSizePercent84) {
  auto rule = CssRule::parse("font-size: 84%");
  ASSERT_TRUE(rule.has_font_size_pct_);
  EXPECT_EQ(rule.font_size_pct, 84);
}

TEST(CssParserTest, FontSizePercent115) {
  auto rule = CssRule::parse("font-size: 115%");
  ASSERT_TRUE(rule.has_font_size_pct_);
  EXPECT_EQ(rule.font_size_pct, 115);
}

TEST(CssParserTest, FontSizePercent116) {
  auto rule = CssRule::parse("font-size: 116%");
  ASSERT_TRUE(rule.has_font_size_pct_);
  EXPECT_EQ(rule.font_size_pct, 116);
}

TEST(CssParserTest, FontSizePx) {
  auto rule = CssRule::parse("font-size: 14px");
  ASSERT_TRUE(rule.has_font_size_pct_);
  EXPECT_EQ(rule.font_size_pct, 58);  // 14 * 100 / 24
}

// ---------------------------------------------------------------------------
// Font-size: pt and rem values
// ---------------------------------------------------------------------------

TEST(CssParserTest, FontSizePt10) {
  // 10pt / 12pt base = 0.833, below 0.90 threshold
  auto rule = CssRule::parse("font-size: 10pt");
  ASSERT_TRUE(rule.has_font_size_pct_);
  EXPECT_EQ(rule.font_size_pct, 83);
}

TEST(CssParserTest, FontSizePt12IsNormal) {
  // 12pt / 12pt base = 1.0, within normal band
  auto rule = CssRule::parse("font-size: 12pt");
  ASSERT_TRUE(rule.has_font_size_pct_);
  EXPECT_EQ(rule.font_size_pct, 100);
}

TEST(CssParserTest, FontSizePt16) {
  // 16pt / 12pt base = 1.333, above 1.30 threshold
  auto rule = CssRule::parse("font-size: 16pt");
  ASSERT_TRUE(rule.has_font_size_pct_);
  EXPECT_EQ(rule.font_size_pct, 133);
}

TEST(CssParserTest, FontSizeRem09) {
  auto rule = CssRule::parse("font-size: 0.9rem");
  ASSERT_TRUE(rule.has_font_size_pct_);
  EXPECT_EQ(rule.font_size_pct, 90);
}

TEST(CssParserTest, FontSizeEm075) {
  auto rule = CssRule::parse("font-size: 0.75rem");
  ASSERT_TRUE(rule.has_font_size_pct_);
  EXPECT_EQ(rule.font_size_pct, 75);
}

TEST(CssParserTest, FontSizeRem15) {
  auto rule = CssRule::parse("font-size: 1.5rem");
  ASSERT_TRUE(rule.has_font_size_pct_);
  EXPECT_EQ(rule.font_size_pct, 150);
}

// ---------------------------------------------------------------------------
// Margin shorthand
// ---------------------------------------------------------------------------

TEST(CssParserTest, MarginShorthand1Value) {
  CssConfig cfg{12, 440, 15};
  auto rule = CssRule::parse("margin: 24px", cfg);
  ASSERT_TRUE(bool(rule.has_margin_left_));
  ASSERT_TRUE(rule.has_margin_right_);
  EXPECT_EQ(rule.margin_left, 24);
  EXPECT_EQ(rule.margin_right, 24);
}

TEST(CssParserTest, MarginShorthand2Values) {
  CssConfig cfg{12, 440, 15};
  auto rule = CssRule::parse("margin: 10px 36px", cfg);
  ASSERT_TRUE(bool(rule.has_margin_left_));
  ASSERT_TRUE(rule.has_margin_right_);
  // Total 72px > budget 66px (15% of 440), clamped proportionally: 36*66/72=33
  EXPECT_EQ(rule.margin_left, 33);
  EXPECT_EQ(rule.margin_right, 33);
}

TEST(CssParserTest, MarginShorthand4Values) {
  CssConfig cfg{12, 440, 15};
  auto rule = CssRule::parse("margin: 5px 48px 5px 24px", cfg);
  ASSERT_TRUE(bool(rule.has_margin_left_));
  ASSERT_TRUE(rule.has_margin_right_);
  // Total 72px > budget 66px, clamped: L=24*66/72=22, R=48*66/72=44
  EXPECT_EQ(rule.margin_left, 22);
  EXPECT_EQ(rule.margin_right, 44);
}

TEST(CssParserTest, MarginShorthandEm) {
  CssConfig cfg{12, 440, 15};
  auto rule = CssRule::parse("margin: 2em", cfg);
  ASSERT_TRUE(bool(rule.has_margin_left_));
  EXPECT_EQ(rule.margin_left, 24);
}

TEST(CssParserTest, MarginShorthandPercent) {
  CssConfig cfg{12, 440, 15};
  auto rule = CssRule::parse("margin: 0 10%", cfg);
  ASSERT_TRUE(bool(rule.has_margin_left_));
  // 10% of 440 = 44px each side, total 88 > budget 66, clamped: 44*66/88=33
  EXPECT_EQ(rule.margin_left, 33);
}

TEST(CssParserTest, MarginShorthandZero) {
  CssConfig cfg{12, 440, 15};
  auto rule = CssRule::parse("margin: 0", cfg);
  EXPECT_FALSE(bool(rule.has_margin_left_));
  EXPECT_FALSE(rule.has_margin_right_);
}

TEST(CssParserTest, MarginShorthandDoesNotOverrideExplicit) {
  CssConfig cfg{12, 440, 15};
  // margin-left explicit should take precedence when merged
  auto shorthand = CssRule::parse("margin: 24px", cfg);
  auto explicit_left = CssRule::parse("margin-left: 48px", cfg);
  auto merged = shorthand + explicit_left;
  ASSERT_TRUE(bool(merged.has_margin_left_));
  EXPECT_EQ(merged.margin_left, 48);
}

// ---------------------------------------------------------------------------
// pt and rem unit support
// ---------------------------------------------------------------------------

TEST(CssParserTest, MarginLeftPt) {
  CssConfig cfg{12, 440, 15};
  auto rule = CssRule::parse("margin-left: 12pt", cfg);
  ASSERT_TRUE(bool(rule.has_margin_left_));
  // 12pt * 4/3 = 16px
  EXPECT_EQ(rule.margin_left, 16);
}

TEST(CssParserTest, TextIndentPt) {
  CssConfig cfg{12, 440, 15};
  auto rule = CssRule::parse("text-indent: 18pt", cfg);
  ASSERT_TRUE(bool(rule.has_indent_));
  // 18pt * 4/3 = 24px
  EXPECT_EQ(rule.indent, 24);
}

TEST(CssParserTest, MarginLeftRem) {
  CssConfig cfg{12, 440, 15};
  auto rule = CssRule::parse("margin-left: 2rem", cfg);
  ASSERT_TRUE(bool(rule.has_margin_left_));
  EXPECT_EQ(rule.margin_left, 24);  // 2 * 12 = 24px
}

TEST(CssParserTest, MarginShorthandPt) {
  CssConfig cfg{12, 440, 15};
  auto rule = CssRule::parse("margin: 0 9pt", cfg);
  ASSERT_TRUE(bool(rule.has_margin_left_));
  // 9pt * 4/3 = 12px
  EXPECT_EQ(rule.margin_left, 12);
}

// ---------------------------------------------------------------------------
// margin-top and margin-bottom
// ---------------------------------------------------------------------------

TEST(CssParserTest, MarginTopPx) {
  CssConfig cfg{12, 440, 15};
  auto rule = CssRule::parse("margin-top: 16px", cfg);
  ASSERT_TRUE(rule.has_margin_top_);
  EXPECT_EQ(rule.margin_top, 16);
}

TEST(CssParserTest, MarginBottomEm) {
  CssConfig cfg{12, 440, 15};
  auto rule = CssRule::parse("margin-bottom: 2em", cfg);
  ASSERT_TRUE(rule.has_margin_bottom_);
  EXPECT_EQ(rule.margin_bottom, 24);
}

TEST(CssParserTest, MarginTopZero) {
  CssConfig cfg{12, 440, 15};
  auto rule = CssRule::parse("margin-top: 0", cfg);
  ASSERT_TRUE(rule.has_margin_top_);
  EXPECT_EQ(rule.margin_top, 0);
}

TEST(CssParserTest, MarginShorthandExtractsTopBottom) {
  CssConfig cfg{12, 440, 15};
  auto rule = CssRule::parse("margin: 16px 24px", cfg);
  ASSERT_TRUE(rule.has_margin_top_);
  ASSERT_TRUE(rule.has_margin_bottom_);
  EXPECT_EQ(rule.margin_top, 16);
  EXPECT_EQ(rule.margin_bottom, 16);
}

TEST(CssParserTest, MarginShorthand4ValuesTopBottom) {
  CssConfig cfg{12, 440, 15};
  auto rule = CssRule::parse("margin: 10px 20px 30px 40px", cfg);
  ASSERT_TRUE(rule.has_margin_top_);
  ASSERT_TRUE(rule.has_margin_bottom_);
  EXPECT_EQ(rule.margin_top, 10);
  EXPECT_EQ(rule.margin_bottom, 30);
}

TEST(CssParserTest, MarginTopBottomMerge) {
  CssConfig cfg{12, 440, 15};
  auto shorthand = CssRule::parse("margin: 10px", cfg);
  auto explicit_top = CssRule::parse("margin-top: 20px", cfg);
  auto merged = shorthand + explicit_top;
  ASSERT_TRUE(merged.has_margin_top_);
  EXPECT_EQ(merged.margin_top, 20);  // explicit overrides
  ASSERT_TRUE(merged.has_margin_bottom_);
  EXPECT_EQ(merged.margin_bottom, 10);  // from shorthand
}

// ---------------------------------------------------------------------------
// padding (treated as additional margin)
// ---------------------------------------------------------------------------

TEST(CssParserTest, PaddingLeftAddsToMargin) {
  CssConfig cfg{12, 440, 15};
  auto rule = CssRule::parse("padding-left: 12px", cfg);
  ASSERT_TRUE(bool(rule.has_margin_left_));
  EXPECT_EQ(rule.margin_left, 12);
}

TEST(CssParserTest, PaddingLeftAdditive) {
  CssConfig cfg{12, 440, 15};
  auto rule = CssRule::parse("margin-left: 10px; padding-left: 8px", cfg);
  ASSERT_TRUE(bool(rule.has_margin_left_));
  EXPECT_EQ(rule.margin_left, 18);  // 10 + 8
}

TEST(CssParserTest, PaddingShorthand) {
  CssConfig cfg{12, 440, 15};
  auto rule = CssRule::parse("padding: 10px 20px", cfg);
  ASSERT_TRUE(rule.has_margin_top_);
  EXPECT_EQ(rule.margin_top, 10);
  ASSERT_TRUE(bool(rule.has_margin_left_));
  EXPECT_EQ(rule.margin_left, 20);
}

TEST(CssParserTest, PaddingTopSetsMarginTop) {
  CssConfig cfg{12, 440, 15};
  auto rule = CssRule::parse("padding-top: 16px", cfg);
  ASSERT_TRUE(rule.has_margin_top_);
  EXPECT_EQ(rule.margin_top, 16);
}

// ---------------------------------------------------------------------------
// page-break-after
// ---------------------------------------------------------------------------

TEST(CssParserTest, PageBreakAfterAlways) {
  auto rule = CssRule::parse("page-break-after: always");
  ASSERT_TRUE(rule.has_page_break_after_);
  EXPECT_TRUE(bool(rule.page_break_after));
}

TEST(CssParserTest, PageBreakAfterAvoid) {
  auto rule = CssRule::parse("page-break-after: avoid");
  ASSERT_TRUE(rule.has_page_break_after_);
  EXPECT_FALSE(bool(rule.page_break_after));
}

// ---------------------------------------------------------------------------
// text-transform
// ---------------------------------------------------------------------------

TEST(CssParserTest, TextTransformUppercase) {
  auto rule = CssRule::parse("text-transform: uppercase");
  ASSERT_TRUE(rule.has_text_transform_);
  EXPECT_EQ(rule.text_transform, TextTransform::Uppercase);
}

TEST(CssParserTest, TextTransformLowercase) {
  auto rule = CssRule::parse("text-transform: lowercase");
  ASSERT_TRUE(rule.has_text_transform_);
  EXPECT_EQ(rule.text_transform, TextTransform::Lowercase);
}

TEST(CssParserTest, TextTransformCapitalize) {
  auto rule = CssRule::parse("text-transform: capitalize");
  ASSERT_TRUE(rule.has_text_transform_);
  EXPECT_EQ(rule.text_transform, TextTransform::Capitalize);
}

TEST(CssParserTest, TextTransformNone) {
  auto rule = CssRule::parse("text-transform: none");
  ASSERT_TRUE(rule.has_text_transform_);
  EXPECT_EQ(rule.text_transform, TextTransform::None);
}

// ---------------------------------------------------------------------------
// font-variant: small-caps
// ---------------------------------------------------------------------------

TEST(CssParserTest, FontVariantSmallCapsAsUppercase) {
  auto rule = CssRule::parse("font-variant: small-caps");
  ASSERT_TRUE(rule.has_text_transform_);
  EXPECT_EQ(rule.text_transform, TextTransform::Uppercase);
}

TEST(CssParserTest, FontVariantSmallCapsDoesNotOverrideExplicitTransform) {
  auto rule = CssRule::parse("text-transform: capitalize; font-variant: small-caps");
  ASSERT_TRUE(rule.has_text_transform_);
  EXPECT_EQ(rule.text_transform, TextTransform::Capitalize);
}

// ---------------------------------------------------------------------------
// vertical-align: super/sub
// ---------------------------------------------------------------------------

TEST(CssParserTest, VerticalAlignSuper) {
  auto rule = CssRule::parse("vertical-align: super");
  ASSERT_TRUE(rule.has_font_size_pct_);
  EXPECT_EQ(rule.font_size_pct, 75);
}

TEST(CssParserTest, VerticalAlignSub) {
  auto rule = CssRule::parse("vertical-align: sub");
  ASSERT_TRUE(rule.has_font_size_pct_);
  EXPECT_EQ(rule.font_size_pct, 75);
}

TEST(CssParserTest, VerticalAlignDoesNotOverrideFontSize) {
  auto rule = CssRule::parse("font-size: large; vertical-align: super");
  ASSERT_TRUE(rule.has_font_size_pct_);
  EXPECT_EQ(rule.font_size_pct, 120);
}

// ---------------------------------------------------------------------------
// line-height
// ---------------------------------------------------------------------------

TEST(CssParserTest, LineHeightNormal) {
  auto rule = CssRule::parse("line-height: normal");
  ASSERT_TRUE(rule.has_line_height_pct_);
  EXPECT_EQ(rule.line_height_pct, 100);
}

TEST(CssParserTest, LineHeightInherit) {
  auto rule = CssRule::parse("line-height: inherit");
  ASSERT_TRUE(rule.has_line_height_pct_);
  EXPECT_EQ(rule.line_height_pct, 100);
}

TEST(CssParserTest, LineHeightPercent150IsDefault) {
  // 150% / 150 = 100% — 150% CSS equals our natural y_advance
  auto rule = CssRule::parse("line-height: 150%");
  ASSERT_TRUE(rule.has_line_height_pct_);
  EXPECT_EQ(rule.line_height_pct, 100);
}

TEST(CssParserTest, LineHeightPercent120) {
  // 120% / 1.5 * 100 = 80 — browser default is tighter than our y_advance
  auto rule = CssRule::parse("line-height: 120%");
  ASSERT_TRUE(rule.has_line_height_pct_);
  EXPECT_EQ(rule.line_height_pct, 80);
}

TEST(CssParserTest, LineHeightPercent100) {
  // 100% / 1.5 * 100 = ~66 → clamped to 70
  auto rule = CssRule::parse("line-height: 100%");
  ASSERT_TRUE(rule.has_line_height_pct_);
  EXPECT_EQ(rule.line_height_pct, 70);
}

TEST(CssParserTest, LineHeightUnitless1_5IsDefault) {
  // 1.5 / 1.5 * 100 = 100% — equals our natural y_advance
  auto rule = CssRule::parse("line-height: 1.5");
  ASSERT_TRUE(rule.has_line_height_pct_);
  EXPECT_EQ(rule.line_height_pct, 100);
}

TEST(CssParserTest, LineHeightUnitless1_2) {
  // 1.2 / 1.5 * 100 = 80 — browser normal is tighter than our y_advance
  auto rule = CssRule::parse("line-height: 1.2");
  ASSERT_TRUE(rule.has_line_height_pct_);
  EXPECT_EQ(rule.line_height_pct, 80);
}

TEST(CssParserTest, LineHeightEm1_5) {
  // 1.5em / 1.5 * 100 = 100
  auto rule = CssRule::parse("line-height: 1.5em");
  ASSERT_TRUE(rule.has_line_height_pct_);
  EXPECT_EQ(rule.line_height_pct, 100);
}

TEST(CssParserTest, LineHeightClampLow) {
  // Very small value should clamp to 70
  auto rule = CssRule::parse("line-height: 0.5");
  ASSERT_TRUE(rule.has_line_height_pct_);
  EXPECT_EQ(rule.line_height_pct, 70);
}

TEST(CssParserTest, LineHeightClampHigh) {
  // Very large value should clamp to 200
  auto rule = CssRule::parse("line-height: 5.0");
  ASSERT_TRUE(rule.has_line_height_pct_);
  EXPECT_EQ(rule.line_height_pct, 200);
}

TEST(CssParserTest, LineHeightMerge) {
  auto lhs = CssRule::parse("line-height: 1.5");
  auto rhs = CssRule::parse("line-height: 1.2");
  auto merged = lhs + rhs;
  ASSERT_TRUE(merged.has_line_height_pct_);
  // rhs wins in merge (operator+ prefers rhs); 1.2 / 1.5 * 100 = 80
  EXPECT_EQ(merged.line_height_pct, 80);
}

TEST(CssParserTest, LineHeightNoValue) {
  auto rule = CssRule::parse("font-size: large");
  EXPECT_FALSE(rule.has_line_height_pct_);
}

// ---------------------------------------------------------------------------
// CssStylesheet::Parser — streaming, fixed-size storage, name filter
// ---------------------------------------------------------------------------

namespace {

// Covers comments, @-rules (with and without blocks, and '@' inside a
// selector), nested blocks, comma lists, compound and complex selectors,
// declarations that depend on each other, and an overlong declaration.
const char* kStreamCss =
    "@charset \"utf-8\";\n"
    "/* header */ p { text-indent: 1.5em; /* mid */ margin: 0 2em; }\n"
    "h1, h2 , .title{text-align:center;font-weight:bold}\n"
    "@media screen { p { text-align: right; } .x { font-style: italic } }\n"
    "a[href^='mailto:a@b'] { font-weight: bold; }\n"
    "div p { text-align: justify; }\n"
    ".pad { margin-left: 1em; padding-left: 1em; }\n"
    ".sc { text-transform: none; font-variant: small-caps; }\n"
    ".big { background: url(data:image/png;base64,"
    "AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA"
    "AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA"
    "); font-style: italic; }\n"
    "p.intro#first { font-size: 120%; }\n"
    ".nest { a { b: c } text-align: center; }\n"
    "span { vertical-align: super }\n"
    ".unterminated { font-weight: bold;";

struct Probe {
  const char* element;
  const char* id;
  const char* cls;
};

const Probe kProbes[] = {
    {"p", nullptr, nullptr},      {"h1", nullptr, nullptr},     {"h2", nullptr, "title"}, {"div", nullptr, "x"},
    {"a", nullptr, nullptr},      {"p", nullptr, "pad"},        {"span", nullptr, "sc"},  {"p", nullptr, "big"},
    {"p", "first", "intro more"}, {"p", nullptr, "nest"},       {"span", nullptr, nullptr},
    {"p", nullptr, "unterminated"},
};

bool same_rule(const CssRule& a, const CssRule& b) {
  // CssRule has padding and bitfields; compare through the merge result.
  CssRule x = CssRule() + a, y = CssRule() + b;
  return x.alignment_opt().value_or(Alignment::Start) == y.alignment_opt().value_or(Alignment::Start) &&
         x.has_alignment_ == y.has_alignment_ && x.bold_opt().value_or(false) == y.bold_opt().value_or(false) &&
         x.has_bold_ == y.has_bold_ && x.italic_opt().value_or(false) == y.italic_opt().value_or(false) &&
         x.has_italic_ == y.has_italic_ && x.indent_opt().value_or(-1) == y.indent_opt().value_or(-1) &&
         x.margin_left_opt().value_or(0) == y.margin_left_opt().value_or(0) &&
         x.margin_right_opt().value_or(0) == y.margin_right_opt().value_or(0) &&
         x.font_size_pct_opt().value_or(0) == y.font_size_pct_opt().value_or(0) &&
         x.text_transform_opt().value_or(TextTransform::None) == y.text_transform_opt().value_or(TextTransform::None) &&
         x.font_variant_small_caps_opt().value_or(false) == y.font_variant_small_caps_opt().value_or(false) &&
         x.vertical_align_opt().value_or(VerticalAlign::Baseline) ==
             y.vertical_align_opt().value_or(VerticalAlign::Baseline);
}

}  // namespace

TEST(CssStreamParser, WholeSheetResults) {
  CssStylesheet sheet;
  sheet.extend_from_sheet(kStreamCss);
  EXPECT_EQ(sheet.get("p", nullptr, nullptr).indent_opt().value_or(0), 18);
  EXPECT_EQ(sheet.get("p", nullptr, nullptr).alignment_opt().value_or(Alignment::Start), Alignment::Start)
      << "rules inside @media are skipped";
  EXPECT_TRUE(sheet.get("h2", nullptr, nullptr).bold_opt().value_or(false));
  EXPECT_EQ(sheet.get("div", nullptr, "title").alignment_opt().value_or(Alignment::Start), Alignment::Center);
  EXPECT_FALSE(sheet.get("a", nullptr, nullptr).has_bold_) << "'@' in a selector skips the rule";
  EXPECT_FALSE(sheet.get("p", nullptr, "x").has_italic_);
  // padding-left adds to margin-left within the same rule.
  EXPECT_EQ(sheet.get("p", nullptr, "pad").margin_left_opt().value_or(0), 24);
  // font-variant: small-caps sees the earlier explicit text-transform.
  EXPECT_EQ(sheet.get("span", nullptr, "sc").text_transform_opt().value_or(TextTransform::Uppercase),
            TextTransform::None);
  // An overlong declaration is dropped, the rest of its rule is kept.
  EXPECT_TRUE(sheet.get("p", nullptr, "big").italic_opt().value_or(false));
  EXPECT_EQ(sheet.get("p", "first", "intro more").font_size_pct_opt().value_or(0), 120);
  EXPECT_FALSE(sheet.get("p", nullptr, "nest").has_alignment_) << "a block with nested braces is skipped";
  EXPECT_FALSE(sheet.get("p", nullptr, "unterminated").has_bold_);
  EXPECT_FALSE(sheet.overflow());
}

TEST(CssStreamParser, ChunkingDoesNotMatter) {
  CssStylesheet whole;
  whole.extend_from_sheet(kStreamCss);
  const std::string css = kStreamCss;
  std::vector<char> scratch(CssStylesheet::Parser::kScratchSize);
  for (size_t chunk : {size_t(1), size_t(2), size_t(3), size_t(7), size_t(64)}) {
    CssStylesheet sheet;
    CssStylesheet::Parser parser(sheet, scratch.data());
    for (size_t pos = 0; pos < css.size(); pos += chunk)
      parser.feed(css.data() + pos, std::min(chunk, css.size() - pos));
    parser.finish();
    EXPECT_EQ(sheet.rule_count(), whole.rule_count()) << "chunk " << chunk;
    EXPECT_EQ(sheet.size_bytes(), whole.size_bytes()) << "chunk " << chunk;
    for (const Probe& p : kProbes)
      EXPECT_TRUE(same_rule(sheet.get(p.element, p.id, p.cls), whole.get(p.element, p.id, p.cls)))
          << "chunk " << chunk << " element " << p.element << " class " << (p.cls ? p.cls : "");
  }
}

// EPUB tools wrap <style> contents as /*<![CDATA[*/ ... /*]]>*/; the XML reader
// hands that over in three pieces, split inside the comments.
TEST(CssStreamParser, CdataWrappedStyleBlock) {
  CssStylesheet sheet;
  std::vector<char> scratch(CssStylesheet::Parser::kScratchSize);
  CssStylesheet::Parser parser(sheet, scratch.data());
  const char* pieces[] = {"\n/*", "*/\n  p.sgc-1 {text-align: justify;}\n  /*", "*/\n"};
  for (const char* piece : pieces)
    parser.feed(piece, std::strlen(piece));
  parser.finish();
  EXPECT_EQ(sheet.get("p", nullptr, "sgc-1").alignment_opt().value_or(Alignment::Start), Alignment::Justify);
}

TEST(CssStreamParser, ExternalStorageOverflowKeepsWholeRules) {
  std::string css;
  for (int i = 0; i < 100; ++i)
    css += ".c" + std::to_string(i) + " { font-weight: bold; }\n";
  CssStylesheet full;
  full.extend_from_sheet(css);
  ASSERT_EQ(full.rule_count(), 100u);

  std::vector<uint8_t> region(full.size_bytes() / 2);
  std::vector<char> scratch(CssStylesheet::Parser::kScratchSize);
  CssStylesheet sheet;
  sheet.use_external(region.data(), region.size());
  CssStylesheet::Parser parser(sheet, scratch.data());
  parser.feed(css.data(), css.size());
  parser.finish();
  EXPECT_TRUE(sheet.overflow());
  EXPECT_LE(sheet.size_bytes(), region.size());
  ASSERT_GT(sheet.rule_count(), 0u);
  ASSERT_LT(sheet.rule_count(), 100u);
  // The rules that fit are intact, the rest are absent.
  EXPECT_TRUE(sheet.get("p", nullptr, "c0").bold_opt().value_or(false));
  const std::string last_kept = "c" + std::to_string(sheet.rule_count() - 1);
  EXPECT_TRUE(sheet.get("p", nullptr, last_kept.c_str()).bold_opt().value_or(false));
  EXPECT_FALSE(sheet.get("p", nullptr, "c99").has_bold_);
}

TEST(CssStreamParser, NameFilterKeepsOnlyUsableRules) {
  const char* css = "p { text-indent: 1em; } .a { font-weight: bold; } .b { font-style: italic; } "
                    "p.a.c { text-align: center; } #id { text-align: right; } h1, .a { font-size: 150%; }";
  std::vector<uint32_t> set;
  for (const char* name : {"p", "a"})
    set.push_back(CssNameSet::hash(name, std::strlen(name)));
  std::sort(set.begin(), set.end());
  const CssNameSet names{set.data(), set.size()};

  CssStylesheet sheet;
  std::vector<char> scratch(CssStylesheet::Parser::kScratchSize);
  CssStylesheet::Parser parser(sheet, scratch.data(), &names);
  parser.feed(css, std::strlen(css));
  parser.finish();
  // Kept: p, .a, and ".a" of the comma list. Dropped: .b, p.a.c (needs c), #id, h1.
  EXPECT_EQ(sheet.rule_count(), 3u);
  const CssRule r = sheet.get("p", nullptr, "a");
  EXPECT_TRUE(r.bold_opt().value_or(false));
  EXPECT_EQ(r.font_size_pct_opt().value_or(0), 150);
  EXPECT_EQ(r.indent_opt().value_or(0), 12);
}

// ---------------------------------------------------------------------------
// Allocation-free apply(): shorthand part counts, case, units, over-long input
// ---------------------------------------------------------------------------

TEST(CssRuleParse, MarginShorthandPartCounts) {
  CssConfig cfg;
  cfg.content_width = 1000;
  cfg.max_margin_pct = 100;
  auto r1 = CssRule::parse("margin: 4px", cfg);
  EXPECT_EQ(r1.margin_top_opt().value_or(99), 4);
  EXPECT_EQ(r1.margin_left_opt().value_or(99), 4);
  auto r2 = CssRule::parse("margin: 4px 8px", cfg);
  EXPECT_EQ(r2.margin_top_opt().value_or(99), 4);
  EXPECT_EQ(r2.margin_bottom_opt().value_or(99), 4);
  EXPECT_EQ(r2.margin_left_opt().value_or(99), 8);
  EXPECT_EQ(r2.margin_right_opt().value_or(99), 8);
  auto r3 = CssRule::parse("margin: 4px 8px 12px", cfg);
  EXPECT_EQ(r3.margin_top_opt().value_or(99), 4);
  EXPECT_EQ(r3.margin_left_opt().value_or(99), 8);
  EXPECT_EQ(r3.margin_bottom_opt().value_or(99), 12);
  auto r4 = CssRule::parse("margin: 4px 8px 12px 16px", cfg);
  EXPECT_EQ(r4.margin_right_opt().value_or(99), 8);
  EXPECT_EQ(r4.margin_left_opt().value_or(99), 16);
  auto r5 = CssRule::parse("margin:  4px   8px 12px 16px 20px ", cfg);
  EXPECT_EQ(r5.margin_top_opt().value_or(99), 4);
  EXPECT_EQ(r5.margin_right_opt().value_or(99), 8);
  EXPECT_EQ(r5.margin_bottom_opt().value_or(99), 12);
  EXPECT_EQ(r5.margin_left_opt().value_or(99), 16);
}

TEST(CssRuleParse, UppercaseAndUnits) {
  CssConfig cfg;
  cfg.glyph_width = 10;
  cfg.content_width = 400;
  EXPECT_EQ(CssRule::parse("TEXT-INDENT: 2EM", cfg).indent_opt().value_or(0), 20);
  EXPECT_EQ(CssRule::parse("text-indent: 1.5rem", cfg).indent_opt().value_or(0), 15);
  EXPECT_EQ(CssRule::parse("text-indent: 6pt", cfg).indent_opt().value_or(0), 8);
  EXPECT_EQ(CssRule::parse("text-indent: 5%", cfg).indent_opt().value_or(0), 20);
  EXPECT_EQ(CssRule::parse("text-indent: 7PX", cfg).indent_opt().value_or(0), 7);
  EXPECT_TRUE(CssRule::parse("Font-Weight: BOLD").bold_opt().value_or(false));
}

TEST(CssRuleParse, OverLongDeclarationIgnored) {
  std::string decl = "font-family: " + std::string(1400, 'a') + "; font-weight: bold";
  auto r = CssRule::parse(decl);
  EXPECT_TRUE(r.bold_opt().value_or(false));
  std::string only = "font-weight: " + std::string(1500, 'b');
  EXPECT_FALSE(CssRule::parse(only).has_bold_);
}
