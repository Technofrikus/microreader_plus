#include "Hyphenation.h"

#include <cstdint>
#include <cstring>

#include "Liang/hyph-de.trie.h"
#include "Liang/hyph-en.trie.h"
#include "Liang/hyph-es.trie.h"
#include "Liang/hyph-fr.trie.h"
#include "Liang/hyph-it.trie.h"
#include "Liang/hyph-nl.trie.h"
#include "Liang/hyph-pl.trie.h"
#include "Liang/hyph-pt.trie.h"
#include "Liang/hyph-ru.trie.h"
#include "Liang/liang_hyphenation_patterns.h"

// ---------------------------------------------------------------------------
// Liang hyphenation algorithm using TeX patterns compiled into binary tries
// by Typst hypher: https://github.com/typst/hypher
// ---------------------------------------------------------------------------

static constexpr size_t kMaxWordLen = 128;

// Decoded view of a single trie node pulled from the serialized blob.
// Node layout: [header][?levelsInfo][transitions][targets]
// header bits: 7=hasLevels, 6-5=stride(0→1,else1-3), 4-0=childCount(31=overflow→extraByte)
struct TrieState {
  const HyphenationTrieData* trie;
  size_t addr;
  uint8_t stride;
  uint8_t childCount;
  const uint8_t* transitions;
  const uint8_t* targets;
  const uint8_t* levels;
  uint8_t levelsLen;
};

static TrieState decode_trie_node(const HyphenationTrieData& trie, size_t addr) {
  TrieState s = {};
  if (addr >= trie.size)
    return s;
  const uint8_t* base = trie.data + addr;
  size_t rem = trie.size - addr;
  size_t pos = 0;

  const uint8_t hdr = base[pos++];
  const bool hasLevels = (hdr >> 7) != 0;
  uint8_t stride = (hdr >> 5) & 0x03u;
  if (stride == 0)
    stride = 1;
  size_t childCount = hdr & 0x1Fu;
  if (childCount == 31u) {
    if (pos >= rem)
      return s;
    childCount = base[pos++];
  }

  const uint8_t* levels = nullptr;
  uint8_t levelsLen = 0;
  if (hasLevels) {
    if (pos + 1 >= rem)
      return s;
    const uint8_t hi = base[pos++];
    const uint8_t loLen = base[pos++];
    // 12-bit absolute offset into original blob (before the 4-byte root header was stripped).
    // Subtract 4 to get index into trie.data (which starts at blob byte 4).
    const size_t offset = (static_cast<size_t>(hi) << 4) | (loLen >> 4);
    levelsLen = loLen & 0x0Fu;
    if (offset < 4u || offset + levelsLen > trie.size + 4u)
      return s;
    levels = trie.data + offset - 4u;
  }

  if (pos + childCount > rem)
    return s;
  const uint8_t* transitions = base + pos;
  pos += childCount;
  if (pos + static_cast<size_t>(childCount) * stride > rem)
    return s;
  const uint8_t* targets = base + pos;

  s.trie = &trie;
  s.addr = addr;
  s.stride = stride;
  s.childCount = static_cast<uint8_t>(childCount < 255 ? childCount : 255);
  s.transitions = transitions;
  s.targets = targets;
  s.levels = levels;
  s.levelsLen = levelsLen;
  return s;
}

static int32_t decode_delta(const uint8_t* buf, uint8_t stride) {
  if (stride == 1)
    return static_cast<int8_t>(buf[0]);
  if (stride == 2)
    return static_cast<int16_t>((static_cast<uint16_t>(buf[0]) << 8) | buf[1]);
  const int32_t v = (static_cast<int32_t>(buf[0]) << 16) | (static_cast<int32_t>(buf[1]) << 8) | buf[2];
  return v - (1 << 23);
}

static bool trie_step(const TrieState& state, uint8_t ch, TrieState& out) {
  for (size_t i = 0; i < state.childCount; ++i) {
    if (state.transitions[i] != ch)
      continue;
    const uint8_t* dp = state.targets + i * state.stride;
    const int32_t delta = decode_delta(dp, state.stride);
    const int64_t next = static_cast<int64_t>(state.addr) + delta;
    if (next < 0 || static_cast<size_t>(next) >= state.trie->size)
      return false;
    out = decode_trie_node(*state.trie, static_cast<size_t>(next));
    return out.trie != nullptr;
  }
  return false;
}

static int trie_hyphenate(const char* word, size_t word_len, size_t leftmin, size_t rightmin, size_t* out_positions,
                          int max_positions, const HyphenationTrieData& trie) {
  if (!word || word_len == 0)
    return 0;
  if (word_len > kMaxWordLen)
    word_len = kMaxWordLen;

  // Build augmented word ".word." with simple case-folding:
  // ASCII A-Z → a-z; UTF-8 C3+[80-9E] (Latin-1 uppercase supplement) → C3+[A0-BE].
  uint8_t aug[kMaxWordLen + 3];
  aug[0] = '.';
  for (size_t i = 0; i < word_len; ++i) {
    uint8_t c = static_cast<uint8_t>(word[i]);
    if (c >= 0x41u && c <= 0x5Au) {
      c += 0x20u;  // ASCII uppercase
    } else if (i > 0 && static_cast<uint8_t>(word[i - 1]) == 0xC3u) {
      // UTF-8 continuation byte after C3 prefix — lowercase if in uppercase range
      if (c >= 0x80u && c <= 0x9Eu && c != 0x97u)
        c += 0x20u;
    }
    aug[1 + i] = c;
  }
  const int aug_len = static_cast<int>(word_len) + 2;
  aug[aug_len - 1] = '.';

  // Score array: one byte per augmented position.
  uint8_t scores[kMaxWordLen + 3];
  std::memset(scores, 0, static_cast<size_t>(aug_len));

  // Walk trie from every starting position in the augmented word.
  const TrieState root = decode_trie_node(trie, trie.rootOffset);
  if (!root.trie)
    return 0;

  for (int start = 0; start < aug_len; ++start) {
    TrieState state = root;
    for (int cursor = start; cursor < aug_len; ++cursor) {
      TrieState next;
      if (!trie_step(state, aug[cursor], next))
        break;
      state = next;

      if (state.levels && state.levelsLen > 0) {
        size_t offset = 0;
        for (uint8_t li = 0; li < state.levelsLen; ++li) {
          const uint8_t packed = state.levels[li];
          offset += packed / 10u;
          const uint8_t level = packed % 10u;
          const size_t splitPos = static_cast<size_t>(start) + offset;
          if (splitPos < static_cast<size_t>(aug_len) && level > scores[splitPos])
            scores[splitPos] = level;
        }
      }
    }
  }

  // Emit positions where score is odd and within leftmin/rightmin bounds.
  // leftmin/rightmin count Unicode code points (not bytes), so "ü" counts as
  // one letter, and a split is never placed inside a multi-byte sequence.
  size_t total_cp = 0;
  for (size_t i = 0; i < word_len; ++i)
    total_cp += (static_cast<uint8_t>(word[i]) & 0xC0u) != 0x80u ? 1 : 0;

  int count = 0;
  size_t cp_before = 1;  // code points in word[0..k) — word[0] is always a lead byte
  for (size_t k = 1; k < word_len; ++k) {
    const bool lead = (static_cast<uint8_t>(word[k]) & 0xC0u) != 0x80u;
    if (!lead)
      continue;
    // Score at augmented position k+1 corresponds to a split after byte k-1 in the word.
    // A split at k means: prefix = word[0..k-1], suffix = word[k..end].
    if ((scores[k + 1] & 1u) && cp_before >= leftmin && (total_cp - cp_before) >= rightmin) {
      if (count < max_positions)
        out_positions[count] = k;
      ++count;
    }
    ++cp_before;
  }
  return count;
}

namespace microreader {
namespace {

// Minimum number of letters that must stay on each side of an algorithmic
// (Liang) break. Values follow the TeX/hyph-utf8 conventions for each
// language: English requires three letters to be carried to the next line
// ("eth-ics", never "ethic-s"); German (Duden) and the others require two
// ("Stra-ße", never "A-bend").
struct HyphenMins {
  uint8_t left;
  uint8_t right;
};

HyphenMins hyphen_mins(HyphenationLang lang) {
  switch (lang) {
    case HyphenationLang::English:
      return {2, 3};
    default:
      return {2, 2};
  }
}

const HyphenationTrieData* trie_for(HyphenationLang lang) {
  switch (lang) {
    case HyphenationLang::English:
      return &en_trie;
    case HyphenationLang::German:
      return &de_trie;
    case HyphenationLang::French:
      return &fr_trie;
    case HyphenationLang::Spanish:
      return &es_trie;
    case HyphenationLang::Italian:
      return &it_trie;
    case HyphenationLang::Dutch:
      return &nl_trie;
    case HyphenationLang::Portuguese:
      return &pt_trie;
    case HyphenationLang::Polish:
      return &pl_trie;
    case HyphenationLang::Russian:
      return &ru_trie;
    default:
      return nullptr;
  }
}

// Shortest run of letters we try to hyphenate algorithmically.
constexpr size_t kMinLettersForLiang = 5;

enum class CharClass : uint8_t {
  Letter,  // part of a word: may be hyphenated
  Digit,   // 0-9: never hyphenated, but counts as word content next to a dash
  Dash,    // explicit break opportunity: - ‐ – —
  Other,   // punctuation, quotes, apostrophes, symbols
};

// Decode one UTF-8 code point starting at s[i] (bounded by len). Returns the
// byte length (>= 1); malformed or truncated sequences decode as one byte.
size_t decode_utf8(const char* s, size_t len, size_t i, uint32_t& cp) {
  const uint8_t b0 = static_cast<uint8_t>(s[i]);
  size_t n = 1;
  if (b0 >= 0xF0u)
    n = 4, cp = b0 & 0x07u;
  else if (b0 >= 0xE0u)
    n = 3, cp = b0 & 0x0Fu;
  else if (b0 >= 0xC0u)
    n = 2, cp = b0 & 0x1Fu;
  else {
    cp = b0;
    return 1;
  }
  if (i + n > len) {
    cp = 0xFFFDu;
    return 1;
  }
  for (size_t k = 1; k < n; ++k) {
    const uint8_t b = static_cast<uint8_t>(s[i + k]);
    if ((b & 0xC0u) != 0x80u) {
      cp = 0xFFFDu;
      return 1;
    }
    cp = (cp << 6) | (b & 0x3Fu);
  }
  return n;
}

CharClass classify(uint32_t cp) {
  if (cp < 0x80u) {
    if ((cp >= 'a' && cp <= 'z') || (cp >= 'A' && cp <= 'Z'))
      return CharClass::Letter;
    if (cp >= '0' && cp <= '9')
      return CharClass::Digit;
    return cp == '-' ? CharClass::Dash : CharClass::Other;
  }
  // U+2010 hyphen, U+2013 en dash, U+2014 em dash. (U+2011 is the
  // NON-BREAKING hyphen and deliberately not a break opportunity.)
  if (cp == 0x2010u || cp == 0x2013u || cp == 0x2014u)
    return CharClass::Dash;
  // Latin-1 punctuation and symbols (« » ¡ ¿ · ° ...) except the letters ª µ º
  // and the soft hyphen, which stays part of the word as before.
  if (cp >= 0x80u && cp <= 0xBFu)
    return (cp == 0xAAu || cp == 0xADu || cp == 0xB5u || cp == 0xBAu) ? CharClass::Letter : CharClass::Other;
  if (cp == 0xD7u || cp == 0xF7u)  // × ÷
    return CharClass::Other;
  // General punctuation: typographic quotes “ ” „ ‘ ’ ‚ ‹ ›, dashes, … , primes.
  if (cp >= 0x2000u && cp <= 0x206Fu)
    return CharClass::Other;
  // Currency, arrows, math and misc symbols, supplemental and CJK punctuation,
  // fullwidth ASCII punctuation.
  if ((cp >= 0x20A0u && cp <= 0x20CFu) || (cp >= 0x2190u && cp <= 0x2BFFu) || (cp >= 0x2E00u && cp <= 0x2E7Fu) ||
      (cp >= 0x3000u && cp <= 0x303Fu) || (cp >= 0xFF01u && cp <= 0xFF0Fu) || (cp >= 0xFF1Au && cp <= 0xFF20u) ||
      cp == 0xFFFDu)
    return CharClass::Other;
  return CharClass::Letter;
}

}  // namespace

// ---------------------------------------------------------------------------
// Public API
// ---------------------------------------------------------------------------

int hyphenate_word(const char* word, size_t len, HyphenationLang lang, size_t* out_positions, int max_positions) {
  const HyphenationTrieData* trie = trie_for(lang);
  if (!trie)
    return 0;
  const HyphenMins mins = hyphen_mins(lang);
  return trie_hyphenate(word, len, mins.left, mins.right, out_positions, max_positions, *trie);
}

size_t find_hyphen_break(const IFont& font, const char* word_ptr, size_t len, FontStyle style, uint8_t size_pct,
                         HyphenationLang lang, uint16_t avail, bool& out_prefix_has_hyphen) {
  out_prefix_has_hyphen = false;
  if (len == 0 || avail == 0)
    return 0;

  // A token is a whitespace-delimited chunk, so it can carry punctuation:
  // “ethics.” — »Schifffahrt« — ethics—and — shouldn’t. Only runs of LETTERS
  // are handed to Liang, and the per-language letter minimum is counted inside
  // that run. Quotes, full stops, apostrophes and dashes never count as
  // letters, so "ethic-|s.”" or "gehab-|t.“" can no longer be produced.
  const size_t scan_len = len < kMaxWordLen ? len : kMaxWordLen;

  struct Cp {
    uint8_t off;
    CharClass cls;
  };
  Cp cps[kMaxWordLen + 1];
  size_t n_cp = 0;
  for (size_t i = 0; i < scan_len;) {
    uint32_t cp = 0;
    const size_t l = decode_utf8(word_ptr, scan_len, i, cp);
    cps[n_cp++] = {static_cast<uint8_t>(i), classify(cp)};
    i += l;
  }
  cps[n_cp] = {static_cast<uint8_t>(scan_len), CharClass::Other};  // sentinel: end offset

  // alnum_before[j] = letters + digits in code points [0, j).
  uint8_t alnum_before[kMaxWordLen + 1];
  alnum_before[0] = 0;
  for (size_t j = 0; j < n_cp; ++j)
    alnum_before[j + 1] =
        alnum_before[j] + ((cps[j].cls == CharClass::Letter || cps[j].cls == CharClass::Digit) ? 1 : 0);
  const uint8_t total_alnum = alnum_before[n_cp];

  // Collect candidate break positions in ascending byte order.
  struct Candidate {
    uint8_t pos;      // byte offset: prefix = word_ptr[0..pos)
    bool at_dash;     // prefix ends with a dash — no synthetic hyphen needed
  };
  static constexpr int kMaxCandidates = 48;
  Candidate cands[kMaxCandidates];
  int n_cands = 0;

  const HyphenationTrieData* trie = trie_for(lang);
  const HyphenMins mins = hyphen_mins(lang);

  for (size_t j = 0; j < n_cp && n_cands < kMaxCandidates;) {
    if (cps[j].cls == CharClass::Dash) {
      // Break after an explicit dash only if real word content sits on both
      // sides: at least one letter/digit before it, two after it, and the next
      // character is not another dash ("--", "——").
      const size_t pos = cps[j + 1].off;
      if (pos < len && alnum_before[j] >= 1 && total_alnum - alnum_before[j + 1] >= 2 &&
          cps[j + 1].cls != CharClass::Dash)
        cands[n_cands++] = {static_cast<uint8_t>(pos), true};
      ++j;
      continue;
    }
    if (cps[j].cls != CharClass::Letter) {
      ++j;
      continue;
    }
    // Maximal run of letters [j, k).
    size_t k = j;
    while (k < n_cp && cps[k].cls == CharClass::Letter)
      ++k;
    if (trie && k - j >= kMinLettersForLiang) {
      const size_t run_start = cps[j].off;
      const size_t run_end = cps[k].off;
      size_t positions[32];
      int n = trie_hyphenate(word_ptr + run_start, run_end - run_start, mins.left, mins.right, positions, 32, *trie);
      if (n > 32)
        n = 32;
      for (int p = 0; p < n && n_cands < kMaxCandidates; ++p) {
        const size_t pos = run_start + positions[p];
        if (pos > 0 && pos < len)
          cands[n_cands++] = {static_cast<uint8_t>(pos), false};
      }
    }
    j = k;
  }
  if (n_cands == 0)
    return 0;

  // Prefix pixel widths, accumulated segment by segment (O(len) in total).
  // Kerning across segment boundaries is not counted; the error is at most
  // ±2px, which is acceptable for a break decision.
  uint16_t prefix_ws[kMaxCandidates];
  {
    size_t prev = 0;
    uint16_t acc = 0;
    for (int i = 0; i < n_cands; ++i) {
      const size_t pos = cands[i].pos;
      if (pos > prev)
        acc += font.word_width(word_ptr + prev, static_cast<uint16_t>(pos - prev), style, size_pct);
      prefix_ws[i] = acc;
      prev = pos;
    }
  }

  // Prefer the right-most explicit dash that fits: breaking where the author
  // already put a hyphen reads better than inserting a new one.
  for (int i = n_cands - 1; i >= 0; --i) {
    if (cands[i].at_dash && prefix_ws[i] <= avail) {
      out_prefix_has_hyphen = true;
      return cands[i].pos;
    }
  }
  // Otherwise the right-most algorithmic break whose prefix plus '-' fits.
  const uint16_t hyphen_w = font.char_width('-', style, size_pct);
  for (int i = n_cands - 1; i >= 0; --i) {
    if (!cands[i].at_dash && prefix_ws[i] + hyphen_w <= avail)
      return cands[i].pos;
  }
  return 0;
}

HyphenationLang detect_language(const std::optional<std::string>& lang_tag) {
  if (!lang_tag)
    return HyphenationLang::None;

  std::string_view sv = *lang_tag;

  auto ieq = [](std::string_view s, const char* expected) {
    size_t i = 0;
    while (i < s.size() && (s[i] == ' ' || s[i] == '\t' || s[i] == '\n' || s[i] == '\r')) {
      ++i;
    }
    for (size_t j = 0; expected[j]; ++j) {
      if (i >= s.size())
        return false;
      char c = s[i++];
      if (c >= 'A' && c <= 'Z')
        c += 32;
      if (c != expected[j])
        return false;
    }
    if (i < s.size()) {
      char c = s[i];
      if (c != '-' && c != ' ' && c != '\t' && c != '\n' && c != '\r')
        return false;
    }
    return true;
  };

  if (ieq(sv, "de") || ieq(sv, "ger") || ieq(sv, "deu"))
    return HyphenationLang::German;
  if (ieq(sv, "en") || ieq(sv, "eng"))
    return HyphenationLang::English;
  if (ieq(sv, "fr") || ieq(sv, "fra"))
    return HyphenationLang::French;
  if (ieq(sv, "es") || ieq(sv, "spa"))
    return HyphenationLang::Spanish;
  if (ieq(sv, "it") || ieq(sv, "ita"))
    return HyphenationLang::Italian;
  if (ieq(sv, "nl") || ieq(sv, "nld"))
    return HyphenationLang::Dutch;
  if (ieq(sv, "pt") || ieq(sv, "por"))
    return HyphenationLang::Portuguese;
  if (ieq(sv, "pl") || ieq(sv, "pol"))
    return HyphenationLang::Polish;
  if (ieq(sv, "ru") || ieq(sv, "rus"))
    return HyphenationLang::Russian;

  return HyphenationLang::None;
}

}  // namespace microreader
