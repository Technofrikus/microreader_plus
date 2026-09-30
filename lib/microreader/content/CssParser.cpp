#include "CssParser.h"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <cstring>

#ifdef ESP_PLATFORM
#include "esp_heap_caps.h"
#include "esp_system.h"
#endif

namespace microreader {

// ---------------------------------------------------------------------------
// CSS length parsing helper: px, em, %, pt, rem
// ---------------------------------------------------------------------------

// Parse a CSS length value (already lowercased) to pixels.
// For em/rem, uses glyph_width. For %, uses ref_width.
// Returns std::nullopt if the value can't be parsed.
static std::optional<int> parse_css_length(const std::string& value, uint16_t glyph_width, uint16_t ref_width) {
  if (value == "0" || value == "auto")
    return 0;
  char* end = nullptr;
  if (value.size() > 2 && value.substr(value.size() - 2) == "px") {
    long v = std::strtol(value.c_str(), &end, 10);
    if (end != value.c_str())
      return static_cast<int>(v);
  } else if (value.size() > 2 && value.substr(value.size() - 2) == "pt") {
    float v = std::strtof(value.c_str(), &end);
    if (end != value.c_str())
      return static_cast<int>(v * 4 / 3 + 0.5f);
  } else if (value.size() > 3 && value.substr(value.size() - 3) == "rem") {
    float v = std::strtof(value.c_str(), &end);
    if (end != value.c_str())
      return static_cast<int>(v * glyph_width + 0.5f);
  } else if (value.size() > 2 && value.substr(value.size() - 2) == "em") {
    float v = std::strtof(value.c_str(), &end);
    if (end != value.c_str())
      return static_cast<int>(v * glyph_width + 0.5f);
  } else if (value.size() > 1 && value.back() == '%') {
    float v = std::strtof(value.c_str(), &end);
    if (end != value.c_str())
      return static_cast<int>(v * ref_width / 100 + 0.5f);
  }
  return std::nullopt;
}

// ---------------------------------------------------------------------------
// CSS shorthand value splitter (shared by margin/padding shorthand)
// Splits "10px 20px" → {"10px", "20px"}
// ---------------------------------------------------------------------------

struct FourSides {
  int top = 0, right = 0, bottom = 0, left = 0;
};

static std::vector<std::string> split_css_values(const std::string& value) {
  std::vector<std::string> parts;
  size_t p = 0;
  while (p < value.size()) {
    while (p < value.size() && std::isspace(static_cast<unsigned char>(value[p])))
      ++p;
    size_t start = p;
    while (p < value.size() && !std::isspace(static_cast<unsigned char>(value[p])))
      ++p;
    if (p > start)
      parts.push_back(value.substr(start, p - start));
  }
  return parts;
}

static FourSides parse_shorthand_sides(const std::vector<std::string>& parts, uint16_t glyph_width,
                                       uint16_t ref_width) {
  auto to_px = [&](const std::string& v) -> int {
    auto len = parse_css_length(v, glyph_width, ref_width);
    return len.value_or(0);
  };
  FourSides s;
  if (parts.size() == 1) {
    s.top = s.right = s.bottom = s.left = to_px(parts[0]);
  } else if (parts.size() == 2) {
    s.top = s.bottom = to_px(parts[0]);
    s.left = s.right = to_px(parts[1]);
  } else if (parts.size() == 3) {
    s.top = to_px(parts[0]);
    s.left = s.right = to_px(parts[1]);
    s.bottom = to_px(parts[2]);
  } else if (parts.size() >= 4) {
    s.top = to_px(parts[0]);
    s.right = to_px(parts[1]);
    s.bottom = to_px(parts[2]);
    s.left = to_px(parts[3]);
  }
  return s;
}

// ---------------------------------------------------------------------------
// CssRule
// ---------------------------------------------------------------------------

CssRule CssRule::parse(const char* decl, size_t length, const CssConfig& config) {
  CssRule rule;
  size_t pos = 0;
  while (pos < length) {
    const void* semi = std::memchr(decl + pos, ';', length - pos);
    const size_t end = semi ? static_cast<size_t>(static_cast<const char*>(semi) - decl) : length;
    apply(rule, decl + pos, end - pos, config);
    pos = end + 1;
  }
  finish(rule, config);
  return rule;
}

void CssRule::apply(CssRule& rule, const char* decl, size_t length, const CssConfig& config) {
  std::string s(decl, length);
  // Lowercase
  for (auto& c : s)
    c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));

  const size_t semi = s.size();
  const size_t colon = s.find(':');
  if (colon == std::string::npos)
    return;
  // Extract key and value
  size_t key_start = 0;
  while (key_start < colon && std::isspace(static_cast<unsigned char>(s[key_start])))
    ++key_start;
  size_t key_end = colon;
  while (key_end > key_start && std::isspace(static_cast<unsigned char>(s[key_end - 1])))
    --key_end;

  size_t val_start = colon + 1;
  while (val_start < semi && std::isspace(static_cast<unsigned char>(s[val_start])))
    ++val_start;
  size_t val_end = semi;
  while (val_end > val_start && std::isspace(static_cast<unsigned char>(s[val_end - 1])))
    --val_end;

  std::string key = s.substr(key_start, key_end - key_start);
  std::string value = s.substr(val_start, val_end - val_start);

  if (key == "text-align") {
    if (value == "start" || value == "left")
      rule.set_alignment(Alignment::Start);
    else if (value == "end" || value == "right")
      rule.set_alignment(Alignment::End);
    else if (value == "center")
      rule.set_alignment(Alignment::Center);
    else if (value == "justify")
      rule.set_alignment(Alignment::Justify);
  } else if (key == "font-style") {
    if (value == "normal")
      rule.set_italic(false);
    else if (value == "italic")
      rule.set_italic(true);
  } else if (key == "font-weight") {
    if (value == "normal")
      rule.set_bold(false);
    else if (value == "bold")
      rule.set_bold(true);
  } else if (key == "text-indent") {
    auto len = parse_css_length(value, config.glyph_width, config.content_width);
    if (len.has_value())
      rule.set_indent(static_cast<int16_t>(*len));
  } else if (key == "margin-left") {
    auto len = parse_css_length(value, config.glyph_width, config.content_width);
    if (len.has_value() && *len > 0)
      rule.set_margin_left(static_cast<uint16_t>(*len));
  } else if (key == "margin-right") {
    auto len = parse_css_length(value, config.glyph_width, config.content_width);
    if (len.has_value() && *len > 0)
      rule.set_margin_right(static_cast<uint16_t>(*len));
  } else if (key == "margin-top") {
    auto len = parse_css_length(value, config.glyph_width, config.content_width);
    if (len.has_value())
      rule.set_margin_top(static_cast<uint16_t>(std::max(0, *len)));
  } else if (key == "margin-bottom") {
    auto len = parse_css_length(value, config.glyph_width, config.content_width);
    if (len.has_value())
      rule.set_margin_bottom(static_cast<uint16_t>(std::max(0, *len)));
  } else if (key == "margin") {
    auto parts = split_css_values(value);
    auto s = parse_shorthand_sides(parts, config.glyph_width, config.content_width);
    if (s.left > 0)
      rule.set_margin_left(static_cast<uint16_t>(s.left));
    if (s.right > 0)
      rule.set_margin_right(static_cast<uint16_t>(s.right));
    if (!parts.empty()) {
      rule.set_margin_top(static_cast<uint16_t>(std::max(0, s.top)));
      rule.set_margin_bottom(static_cast<uint16_t>(std::max(0, s.bottom)));
    }
  } else if (key == "padding-left") {
    auto len = parse_css_length(value, config.glyph_width, config.content_width);
    if (len.has_value()) {
      uint16_t val = *len > 0 ? static_cast<uint16_t>(*len) : 0;
      rule.set_margin_left(rule.has_margin_left_ ? rule.margin_left + val : val);
    }
  } else if (key == "padding-right") {
    auto len = parse_css_length(value, config.glyph_width, config.content_width);
    if (len.has_value() && *len > 0) {
      uint16_t val = static_cast<uint16_t>(*len);
      rule.set_margin_right(rule.margin_right_opt().value_or(0) + val);
    }
  } else if (key == "padding-top") {
    auto len = parse_css_length(value, config.glyph_width, config.content_width);
    if (len.has_value() && *len >= 0) {
      uint16_t val = static_cast<uint16_t>(std::max(0, *len));
      rule.set_margin_top(std::max(rule.margin_top_opt().value_or(0), val));
    }
  } else if (key == "padding-bottom") {
    auto len = parse_css_length(value, config.glyph_width, config.content_width);
    if (len.has_value() && *len >= 0) {
      uint16_t val = static_cast<uint16_t>(std::max(0, *len));
      rule.set_margin_bottom(std::max(rule.margin_bottom_opt().value_or(0), val));
    }
  } else if (key == "padding") {
    auto parts = split_css_values(value);
    auto s = parse_shorthand_sides(parts, config.glyph_width, config.content_width);
    if (!parts.empty()) {
      uint16_t lv = s.left > 0 ? static_cast<uint16_t>(s.left) : 0;
      rule.set_margin_left(rule.has_margin_left_ ? rule.margin_left + lv : lv);
    }
    if (s.right > 0)
      rule.set_margin_right(rule.margin_right_opt().value_or(0) + static_cast<uint16_t>(s.right));
    if (s.top > 0)
      rule.set_margin_top(std::max(rule.margin_top_opt().value_or(0), static_cast<uint16_t>(s.top)));
    if (s.bottom > 0)
      rule.set_margin_bottom(std::max(rule.margin_bottom_opt().value_or(0), static_cast<uint16_t>(s.bottom)));
  } else if (key == "float") {
    if (value == "left" || value == "right")
      rule.set_is_float(true);
    else if (value == "none")
      rule.set_is_float(false);
  } else if (key == "display") {
    if (value == "none")
      rule.set_is_hidden(true);
  } else if (key == "border-top-style") {
    if (value != "none" && value != "hidden")
      rule.set_border_top(true);
    else
      rule.set_border_top(false);
  } else if (key == "border-top") {
    // e.g. "1px solid black" — any non-none value means a visible border
    if (value == "none" || value == "0" || value == "hidden")
      rule.set_border_top(false);
    else if (value.find("solid") != std::string::npos || value.find("dashed") != std::string::npos ||
             value.find("dotted") != std::string::npos || value.find("double") != std::string::npos)
      rule.set_border_top(true);
  } else if (key == "width") {
    if (!value.empty() && value.back() == '%') {
      char* end = nullptr;
      float v = std::strtof(value.c_str(), &end);
      if (end != value.c_str()) {
        int pct = static_cast<int>(v + 0.5f);
        if (pct < 0) pct = 0;
        if (pct > 100) pct = 100;
        rule.set_width_pct(static_cast<uint8_t>(pct));
      }
    }
  } else if (key == "page-break-before") {
    if (value == "always" || value == "left" || value == "right")
      rule.set_page_break_before(true);
    else if (value == "auto" || value == "avoid")
      rule.set_page_break_before(false);
  } else if (key == "page-break-after") {
    if (value == "always" || value == "left" || value == "right")
      rule.set_page_break_after(true);
    else if (value == "auto" || value == "avoid")
      rule.set_page_break_after(false);
  } else if (key == "text-transform") {
    if (value == "uppercase")
      rule.set_text_transform(TextTransform::Uppercase);
    else if (value == "lowercase")
      rule.set_text_transform(TextTransform::Lowercase);
    else if (value == "capitalize")
      rule.set_text_transform(TextTransform::Capitalize);
    else if (value == "none")
      rule.set_text_transform(TextTransform::None);
  } else if (key == "font-variant") {
    if (value == "small-caps") {
      rule.set_font_variant_small_caps(true);
      // Approximate as uppercase when no explicit text-transform is set
      if (!rule.has_text_transform_)
        rule.set_text_transform(TextTransform::Uppercase);
    }
  } else if (key == "vertical-align") {
    // CSS super/sub -> font_size_pct: 75% + VerticalAlign
    if (value == "super") {
      if (!rule.has_font_size_pct_)
        rule.set_font_size_pct(75);
      rule.set_vertical_align(VerticalAlign::Super);
    } else if (value == "sub") {
      if (!rule.has_font_size_pct_)
        rule.set_font_size_pct(75);
      rule.set_vertical_align(VerticalAlign::Sub);
    } else if (value == "top" || value == "bottom") {
      if (!rule.has_font_size_pct_)
        rule.set_font_size_pct(75);
    }
  } else if (key == "line-height") {
    // Parse line-height as percentage of our natural y_advance.
    // Our fonts have y_advance ≈ 1.5× em-size, so CSS line-height: 1.5 = 100% (use y_advance as-is).
    // CSS line-height: 1.2 (browser normal) = 80% (slightly tighter than y_advance).
    // Common values: "normal", "1.2", "1.5", "140%", "1.4em"
    static constexpr float kNormFactor = 1.5f;
    char* end = nullptr;
    if (value == "normal" || value == "inherit") {
      rule.set_line_height_pct(100);
    } else if (value.size() > 1 && value.back() == '%') {
      float pct = std::strtof(value.c_str(), &end);
      if (end != value.c_str()) {
        uint8_t val = static_cast<uint8_t>(std::clamp(pct / kNormFactor, 70.0f, 200.0f));
        rule.set_line_height_pct(val);
      }
    } else if (value.size() > 2 && value.substr(value.size() - 2) == "em") {
      float em = std::strtof(value.c_str(), &end);
      if (end != value.c_str()) {
        uint8_t val = static_cast<uint8_t>(std::clamp(em * 100.0f / kNormFactor, 70.0f, 200.0f));
        rule.set_line_height_pct(val);
      }
    } else {
      // Unitless number (e.g. "1.5")
      float num = std::strtof(value.c_str(), &end);
      if (end != value.c_str()) {
        uint8_t val = static_cast<uint8_t>(std::clamp(num * 100.0f / kNormFactor, 70.0f, 200.0f));
        rule.set_line_height_pct(val);
      }
    }
  } else if (key == "list-style-type" || key == "list-style") {
    if (value == "none")
      rule.set_list_style_none(true);
  } else if (key == "font-size") {
    if (value == "small" || value == "x-small" || value == "xx-small" || value == "smaller")
      rule.set_font_size_pct(80);
    else if (value == "large" || value == "larger")
      rule.set_font_size_pct(120);
    else if (value == "x-large")
      rule.set_font_size_pct(140);
    else if (value == "xx-large")
      rule.set_font_size_pct(160);
    else if (value == "medium" || value == "normal")
      rule.set_font_size_pct(100);
    else {
      // Try parsing numeric values: percentages (90%) and em (0.9em)
      char* end = nullptr;
      if (value.size() > 1 && value.back() == '%') {
        float pct = std::strtof(value.c_str(), &end);
        if (end != value.c_str()) {
          rule.set_font_size_pct(static_cast<uint8_t>(std::clamp(pct, 30.0f, 250.0f)));
        }
      } else if (value.size() > 2 && value.substr(value.size() - 2) == "em") {
        float em = std::strtof(value.c_str(), &end);
        if (end != value.c_str()) {
          rule.set_font_size_pct(static_cast<uint8_t>(std::clamp(em * 100.0f, 30.0f, 250.0f)));
        }
      } else if (value.size() > 3 && value.substr(value.size() - 3) == "rem") {
        float rem = std::strtof(value.c_str(), &end);
        if (end != value.c_str()) {
          rule.set_font_size_pct(static_cast<uint8_t>(std::clamp(rem * 100.0f, 30.0f, 250.0f)));
        }
      } else if (value.size() > 2 && value.substr(value.size() - 2) == "pt") {
        float pt = std::strtof(value.c_str(), &end);
        if (end != value.c_str()) {
          float ratio = pt / 12.0f;
          rule.set_font_size_pct(static_cast<uint8_t>(std::clamp(ratio * 100.0f, 30.0f, 250.0f)));
        }
      } else if (value.size() > 2 && value.substr(value.size() - 2) == "px") {
        float px = std::strtof(value.c_str(), &end);
        if (end != value.c_str()) {
          float ratio = px / 24.0f;
          rule.set_font_size_pct(static_cast<uint8_t>(std::clamp(ratio * 100.0f, 30.0f, 250.0f)));
        }
      }
    }
  }
}

void CssRule::finish(CssRule& rule, const CssConfig& config) {
  // If both margins are set in the same rule, clamp their total to
  // max_margin_pct% of content_width, scaling proportionally.
  if (rule.has_margin_left_ && rule.has_margin_right_) {
    uint16_t total = rule.margin_left + rule.margin_right;
    uint16_t max_total = config.max_margin_pct * config.content_width / 100;
    if (total > max_total && total > 0) {
      float scale = static_cast<float>(max_total) / total;
      rule.set_margin_left(static_cast<uint16_t>(rule.margin_left * scale));
      rule.set_margin_right(static_cast<uint16_t>(rule.margin_right * scale));
    }
  }
}

CssRule CssRule::operator+(const CssRule& rhs) const {
  CssRule result;

  if (rhs.has_alignment_)
    result.set_alignment(rhs.alignment);
  else if (has_alignment_)
    result.set_alignment(alignment);
  if (rhs.has_italic_)
    result.set_italic(rhs.italic);
  else if (has_italic_)
    result.set_italic(italic);
  if (rhs.has_bold_)
    result.set_bold(rhs.bold);
  else if (has_bold_)
    result.set_bold(bold);
  if (rhs.has_indent_)
    result.set_indent(rhs.indent);
  else if (has_indent_)
    result.set_indent(indent);
  if (rhs.has_font_size_pct_)
    result.set_font_size_pct(rhs.font_size_pct);
  else if (has_font_size_pct_)
    result.set_font_size_pct(font_size_pct);
  if (rhs.has_margin_left_)
    result.set_margin_left(rhs.margin_left);
  else if (has_margin_left_)
    result.set_margin_left(margin_left);
  if (rhs.has_margin_right_)
    result.set_margin_right(rhs.margin_right);
  else if (has_margin_right_)
    result.set_margin_right(margin_right);
  if (rhs.has_margin_top_)
    result.set_margin_top(rhs.margin_top);
  else if (has_margin_top_)
    result.set_margin_top(margin_top);
  if (rhs.has_margin_bottom_)
    result.set_margin_bottom(rhs.margin_bottom);
  else if (has_margin_bottom_)
    result.set_margin_bottom(margin_bottom);
  if (rhs.has_is_float_)
    result.set_is_float(rhs.is_float);
  else if (has_is_float_)
    result.set_is_float(is_float);
  if (rhs.has_is_hidden_)
    result.set_is_hidden(rhs.is_hidden);
  else if (has_is_hidden_)
    result.set_is_hidden(is_hidden);
  if (rhs.has_page_break_before_)
    result.set_page_break_before(rhs.page_break_before);
  else if (has_page_break_before_)
    result.set_page_break_before(page_break_before);
  if (rhs.has_page_break_after_)
    result.set_page_break_after(rhs.page_break_after);
  else if (has_page_break_after_)
    result.set_page_break_after(page_break_after);
  if (rhs.has_text_transform_)
    result.set_text_transform(rhs.text_transform);
  else if (has_text_transform_)
    result.set_text_transform(text_transform);
  if (rhs.has_vertical_align_)
    result.set_vertical_align(rhs.vertical_align);
  else if (has_vertical_align_)
    result.set_vertical_align(vertical_align);
  if (rhs.has_line_height_pct_)
    result.set_line_height_pct(rhs.line_height_pct);
  else if (has_line_height_pct_)
    result.set_line_height_pct(line_height_pct);
  if (rhs.has_list_style_none_)
    result.set_list_style_none(rhs.list_style_none);
  else if (has_list_style_none_)
    result.set_list_style_none(list_style_none);
  if (rhs.has_font_variant_small_caps_)
    result.set_font_variant_small_caps(rhs.font_variant_small_caps);
  else if (has_font_variant_small_caps_)
    result.set_font_variant_small_caps(font_variant_small_caps);
  if (rhs.has_border_top_)
    result.set_border_top(rhs.border_top);
  else if (has_border_top_)
    result.set_border_top(border_top);
  if (rhs.has_width_pct_)
    result.set_width_pct(rhs.width_pct);
  else if (has_width_pct_)
    result.set_width_pct(width_pct);

  return result;
}

// ---------------------------------------------------------------------------
// CssStylesheet
// ---------------------------------------------------------------------------

namespace {

constexpr size_t kMaxSelectorClasses = 8;

// A parsed simple selector, as views into the sheet text (no allocation).
struct SelectorView {
  std::string_view element;
  std::string_view id;
  std::string_view classes[kMaxSelectorClasses];
  uint8_t class_count = 0;

  size_t name_bytes() const {
    size_t n = element.size() + id.size();
    for (uint8_t i = 0; i < class_count; ++i)
      n += 1 + classes[i].size();
    return n;
  }
};

// Parses a compound selector such as "p", ".cls", "p.a.b", "div#id.cls".
// Returns false for empty or complex selectors (spaces, >, +, ~, :, [) and for
// ones that don't fit the compact form (names over 255 bytes, too many classes).
bool parse_selector(const char* s, size_t len, SelectorView& out) {
  out = {};
  while (len > 0 && std::isspace(static_cast<unsigned char>(s[0]))) {
    ++s;
    --len;
  }
  while (len > 0 && std::isspace(static_cast<unsigned char>(s[len - 1])))
    --len;
  if (len == 0)
    return false;

  for (size_t i = 0; i < len; ++i) {
    char c = s[i];
    if (std::isspace(static_cast<unsigned char>(c)) || c == '>' || c == '+' || c == '~' || c == ':' || c == '[')
      return false;
  }

  char kind = 'e';
  size_t seg = 0;
  for (size_t i = 0; i <= len; ++i) {
    if (i < len && s[i] != '.' && s[i] != '#')
      continue;
    std::string_view name(s + seg, i - seg);
    if (!name.empty()) {
      if (name.size() > 255)
        return false;
      if (kind == 'e') {
        out.element = name;
      } else if (kind == '#') {
        out.id = name;
      } else {
        if (out.class_count == kMaxSelectorClasses)
          return false;
        out.classes[out.class_count++] = name;
      }
    }
    if (i < len)
      kind = s[i];
    seg = i + 1;
  }
  return !out.element.empty() || !out.id.empty() || out.class_count != 0;
}

// Does the whitespace-separated class list contain `target`?
bool class_list_contains(std::string_view cls, std::string_view target) {
  size_t p = 0;
  while (p < cls.size()) {
    while (p < cls.size() && std::isspace(static_cast<unsigned char>(cls[p])))
      ++p;
    size_t start = p;
    while (p < cls.size() && !std::isspace(static_cast<unsigned char>(cls[p])))
      ++p;
    if (cls.substr(start, p - start) == target)
      return true;
  }
  return false;
}

// Group header: the rule's properties, then its selector count.
constexpr size_t kGroupHeader = sizeof(CssRule) + sizeof(uint16_t);

// Walks one stored selector at p; returns the byte after it. `matched` tells
// whether it applies to the element, `specificity` is its cascade weight.
const uint8_t* match_selector(const uint8_t* p, std::string_view element, std::string_view id, std::string_view cls,
                              bool& matched, uint32_t& specificity) {
  const uint8_t element_len = p[0];
  const uint8_t id_len = p[1];
  const uint8_t class_count = p[2];
  p += 3;
  specificity = (static_cast<uint32_t>(id_len != 0) << 16) | (static_cast<uint32_t>(class_count) << 8) |
                static_cast<uint32_t>(element_len != 0);
  const char* names = reinterpret_cast<const char*>(p);
  matched = (element_len == 0 || element == std::string_view(names, element_len)) &&
            (id_len == 0 || id == std::string_view(names + element_len, id_len));
  p += element_len + id_len;
  for (uint8_t i = 0; i < class_count; ++i) {
    const size_t len = *p++;
    if (matched && !class_list_contains(cls, std::string_view(reinterpret_cast<const char*>(p), len)))
      matched = false;
    p += len;
  }
  return p;
}

}  // namespace

bool CssNameSet::contains(std::string_view name) const {
  const uint32_t h = hash(name.data(), name.size());
  return std::binary_search(hashes, hashes + count, h);
}

void CssStylesheet::use_external(uint8_t* data, size_t capacity) {
  owned_ = {};
  external_ = true;
  ext_ = data;
  cap_ = capacity;
  size_ = 0;
  selector_count_ = 0;
  overflow_ = false;
}

bool CssStylesheet::append(const void* src, size_t n) {
  if (overflow_)
    return false;
  if (external_) {
    if (n > cap_ - size_) {
      overflow_ = true;
      return false;
    }
  } else if (size_ + n > owned_.capacity()) {
    const size_t want = std::max({owned_.capacity() * 2, size_ + n, size_t(256)});
#ifdef ESP_PLATFORM
    if (esp_get_free_heap_size() < kMinFreeHeap + want) {
      overflow_ = true;
      return false;
    }
#endif
    owned_.reserve(want);
  }
  if (!external_)
    owned_.resize(size_ + n);
  std::memcpy(data() + size_, src, n);
  size_ += n;
  return true;
}

void CssStylesheet::extend_from_sheet(const char* css, size_t length) {
  std::vector<char> scratch(Parser::kScratchSize);
  Parser parser(*this, scratch.data());
  parser.feed(css, length);
  parser.finish();
}

// ---------------------------------------------------------------------------
// CssStylesheet::Parser — a character-at-a-time state machine, equivalent to
// parsing the whole text at once: comments are removed first, then
//   prelude '{' declarations '}'   stores the rule for each supported selector
//                                  of the prelude, unless the block nests '{'
//                                  or sets no property we use;
//   '@' ... ';'  or  '@' ... {...} is skipped (with anything before the '@').
// Selectors are split on ',' and declarations on ';' as they arrive, so only
// the current selector and declaration are ever buffered.
// ---------------------------------------------------------------------------

CssStylesheet::Parser::Parser(CssStylesheet& sheet, char* scratch, const CssNameSet* filter)
    : sheet_(sheet), sel_(scratch), decl_(scratch + kSelectorCap), filter_(filter) {
  begin_group();
}

void CssStylesheet::Parser::feed(const char* data, size_t length) {
  for (size_t i = 0; i < length; ++i) {
    const char c = data[i];
    if (in_comment_) {
      if (star_ && c == '/')
        in_comment_ = false;
      star_ = !in_comment_ ? false : c == '*';
      continue;
    }
    if (slash_) {
      slash_ = false;
      if (c == '*') {
        in_comment_ = true;
        star_ = false;
        continue;
      }
      put('/');
    }
    if (c == '/') {
      slash_ = true;
      continue;
    }
    put(c);
  }
}

void CssStylesheet::Parser::finish() {
  if (slash_) {
    slash_ = false;
    put('/');
  }
  // An unterminated rule is dropped.
  sheet_.size_ = group_;
  group_selectors_ = 0;
}

// Opens a group for the next rule: its header is reserved now, so the
// selectors can follow it, and filled in when the rule closes.
void CssStylesheet::Parser::begin_group() {
  static const uint8_t kZero[kGroupHeader] = {};
  group_ = sheet_.size_;
  group_selectors_ = 0;
  sel_len_ = 0;
  sel_space_ = false;
  sel_bad_ = false;
  sheet_.append(kZero, sizeof(kZero));
}

void CssStylesheet::Parser::end_selector() {
  if (!sel_bad_ && sel_len_ > 0 && group_selectors_ < UINT16_MAX) {
    SelectorView v;
    bool keep = parse_selector(sel_, sel_len_, v);
    if (keep && filter_) {
      keep = (v.element.empty() || filter_->contains(v.element)) && (v.id.empty() || filter_->contains(v.id));
      for (uint8_t i = 0; keep && i < v.class_count; ++i)
        keep = filter_->contains(v.classes[i]);
    }
    if (keep) {
      const uint8_t head[3] = {static_cast<uint8_t>(v.element.size()), static_cast<uint8_t>(v.id.size()),
                               v.class_count};
      // Checked up front so a selector is stored whole or not at all.
      if (sheet_.external_ && v.name_bytes() + sizeof(head) > sheet_.cap_ - sheet_.size_) {
        sheet_.overflow_ = true;
      } else {
        sheet_.append(head, sizeof(head));
        sheet_.append(v.element.data(), v.element.size());
        sheet_.append(v.id.data(), v.id.size());
        for (uint8_t i = 0; i < v.class_count; ++i) {
          const uint8_t len = static_cast<uint8_t>(v.classes[i].size());
          sheet_.append(&len, 1);
          sheet_.append(v.classes[i].data(), len);
        }
        ++group_selectors_;
      }
    }
  }
  sel_len_ = 0;
  sel_space_ = false;
  sel_bad_ = false;
}

void CssStylesheet::Parser::end_declaration() {
  if (!decl_bad_ && decl_len_ > 0)
    CssRule::apply(rule_, decl_, decl_len_, sheet_.config_);
  decl_len_ = 0;
  decl_bad_ = false;
}

void CssStylesheet::Parser::put(char c) {
  switch (mode_) {
    case Mode::Prelude:
      if (c == '@') {
        sheet_.size_ = group_;  // drops the selectors seen so far
        group_selectors_ = 0;
        mode_ = Mode::AtRule;
      } else if (c == '{') {
        end_selector();
        mode_ = Mode::Body;
        depth_ = 1;
        nested_ = false;
        rule_ = CssRule();
        decl_len_ = 0;
        decl_bad_ = false;
      } else if (c == ',') {
        end_selector();
      } else if (std::isspace(static_cast<unsigned char>(c))) {
        if (sel_len_ > 0)
          sel_space_ = true;
      } else {
        if (sel_space_)
          sel_bad_ = true;  // whitespace inside: a descendant selector
        if (sel_len_ < kSelectorCap)
          sel_[sel_len_++] = c;
        else
          sel_bad_ = true;  // longer than any storable selector
      }
      break;

    case Mode::AtRule:
      if (c == ';') {
        mode_ = Mode::Prelude;
        begin_group();
      } else if (c == '{') {
        mode_ = Mode::AtBlock;
        depth_ = 1;
      }
      break;

    case Mode::AtBlock:
      if (c == '{') {
        ++depth_;
      } else if (c == '}' && --depth_ == 0) {
        mode_ = Mode::Prelude;
        begin_group();
      }
      break;

    case Mode::Body:
      if (c == '{') {
        ++depth_;
        nested_ = true;
      } else if (c == '}') {
        if (--depth_ > 0)
          break;
        if (!nested_) {
          end_declaration();
          CssRule::finish(rule_, sheet_.config_);
        }
        if (!nested_ && rule_.has_any() && group_selectors_ > 0 && !sheet_.overflow_) {
          std::memcpy(sheet_.data() + group_, &rule_, sizeof(CssRule));
          std::memcpy(sheet_.data() + group_ + sizeof(CssRule), &group_selectors_, sizeof(uint16_t));
          sheet_.selector_count_ += group_selectors_;
        } else {
          sheet_.size_ = group_;
        }
        mode_ = Mode::Prelude;
        begin_group();
      } else if (!nested_) {
        if (c == ';')
          end_declaration();
        else if (decl_len_ < kDeclarationCap)
          decl_[decl_len_++] = c;
        else
          decl_bad_ = true;  // no declaration we use is this long
      }
      break;
  }
}

// ---------------------------------------------------------------------------
// get() overloads: scan all rules, match selectors, sort, merge.
// ---------------------------------------------------------------------------

CssRule CssStylesheet::get(const char* element, const char* id, const char* cls) const {
  if (size_ == 0)
    return {};

  size_t el_len = element ? std::strlen(element) : 0;
  size_t id_len = id ? std::strlen(id) : 0;
  size_t cls_len = cls ? std::strlen(cls) : 0;
  return get(element ? element : "", el_len, id, id_len, cls, cls_len);
}

CssRule CssStylesheet::get(const char* element, size_t element_len, const char* id, size_t id_len, const char* cls,
                           size_t cls_len) const {
  if (size_ == 0)
    return {};
  const std::string_view element_sv(element ? element : "", element ? element_len : 0);
  const std::string_view id_sv(id ? id : "", id ? id_len : 0);
  const std::string_view cls_sv(cls ? cls : "", cls ? cls_len : 0);

  struct Match {
    uint32_t specificity;
    size_t index;
    const uint8_t* rule;
  };
  Match inline_buf[8];
  size_t match_count = 0;
  bool used_heap = false;
  std::vector<Match> heap_matches;

  const uint8_t* p = data();
  const uint8_t* const end = p + size_;
  size_t index = 0;
  while (p < end) {
    const uint8_t* rule = p;
    uint16_t selectors;
    std::memcpy(&selectors, p + sizeof(CssRule), sizeof(selectors));
    p += kGroupHeader;
    for (uint16_t s = 0; s < selectors; ++s, ++index) {
      bool matched;
      uint32_t specificity;
      p = match_selector(p, element_sv, id_sv, cls_sv, matched, specificity);
      if (!matched)
        continue;
      Match m{specificity, index, rule};
      if (!used_heap && match_count < 8) {
        inline_buf[match_count++] = m;
      } else {
        if (!used_heap) {
          heap_matches.assign(inline_buf, inline_buf + match_count);
          used_heap = true;
        }
        heap_matches.push_back(m);
        match_count = heap_matches.size();
      }
    }
  }

  if (match_count == 0)
    return {};

  Match* matches = used_heap ? heap_matches.data() : inline_buf;
  std::sort(matches, matches + match_count, [](const Match& a, const Match& b) {
    if (a.specificity != b.specificity)
      return a.specificity < b.specificity;
    return a.index < b.index;
  });

  CssRule result;
  for (size_t i = 0; i < match_count; ++i) {
    CssRule rule;
    std::memcpy(static_cast<void*>(&rule), matches[i].rule, sizeof(CssRule));
    result = result + rule;
  }
  return result;
}

}  // namespace microreader
