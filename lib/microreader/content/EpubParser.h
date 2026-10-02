#pragma once

#include <algorithm>
#include <string>
#include <vector>

#include "ContentModel.h"
#include "CssParser.h"
#include "ZipReader.h"

namespace microreader {

// Callback type for streaming chapter parsing.
// Each paragraph is emitted as soon as it's parsed — no accumulation.
using ParagraphSink = void (*)(void* ctx, Paragraph&& para);

// The parsed stylesheets of a book, kept in one fixed memory region (the
// arena) rather than on the heap — during a conversion that is the unused part
// of the chapter's XML buffer, so loading CSS never competes with the rest of
// the conversion (or Bluetooth) for heap, and the result is the same on every
// device and in every memory state.
//
// Sheets sit back to back in the arena, oldest first. Each chapter marks the
// ones it uses; to make room, sheets the current chapter does not use are
// dropped (they are simply parsed from the EPUB again when a later chapter
// needs them) and the rest are moved down.
class CssCache {
 public:
  static constexpr size_t kMaxEntries = 16;  // stylesheet links per chapter
  // Arena size. The same everywhere so a book converts identically on any
  // platform; parse_chapter_streaming() takes it from its xml_buf when that is
  // large enough (Epub::kChapterBufSize) and from the heap otherwise. Sized so
  // kChapterBufSize is exactly one display framebuffer (DrawBuffer::kBufSize).
  static constexpr size_t kArenaSize = 32288;
  static constexpr uint32_t kInlineKey = 0xFFFFFFFFu;  // the chapter's <style> blocks

  CssCache() = default;
  CssCache(const CssCache&) = delete;
  CssCache& operator=(const CssCache&) = delete;

  // Uses [base, base + kArenaSize). A different region than before drops all
  // sheets; nullptr switches to a heap arena owned by the cache.
  void set_arena(uint8_t* base);
  // Drops all sheets (they point into the arena, which the caller may reuse).
  void clear();
  // Drops all sheets but remembers which were too big.
  void drop_sheets();

  // Starts a chapter: sheets loaded only for the previous chapter are dropped.
  void begin_chapter();
  // A sheet already in the arena (and marks it used by this chapter), or nullptr.
  const CssStylesheet* find(uint32_t key);

  // Parses a sheet into the free end of the arena: fill(CssStylesheet&) must
  // feed it through a CssStylesheet::Parser and return false on a read error.
  // If it does not fit, sheets this chapter does not use are dropped and it is
  // parsed again. `limit` caps the arena bytes used (the top may be reserved).
  // Returns nullptr if the sheet could not be read or does not fit next to
  // this chapter's other sheets. chapter_only: dropped at the next chapter
  // (inline <style> blocks, sheets filtered for this chapter).
  // keep_partial: on overflow keep the rules that fit instead of failing.
  template <typename Fill>
  const CssStylesheet* load(uint32_t key, const CssConfig& config, bool chapter_only, size_t limit,
                            bool keep_partial, Fill&& fill) {
    for (;;) {
      size_t slot = 0;
      while (slot < kMaxEntries + 1 && entries_[slot].used)
        ++slot;
      if (slot == kMaxEntries + 1) {
        if (!evict_one())
          return nullptr;
        continue;
      }
      const size_t start = end_offset();
      Entry& e = entries_[slot];
      e.key = key;
      e.offset = static_cast<uint32_t>(start);
      e.last_used = gen_;
      e.chapter_only = chapter_only;
      e.sheet = CssStylesheet(config);
      e.sheet.use_external(arena_ + start, limit > start ? limit - start : 0);
      if (!fill(e.sheet))
        return nullptr;
      if (!e.sheet.overflow() || keep_partial) {
        e.used = true;
        order_[count_++] = static_cast<uint8_t>(slot);
        peak_ = std::max(peak_, end_offset());
        return &e.sheet;
      }
      if (!evict_one())
        return nullptr;
    }
  }

  uint8_t* arena() const {
    return arena_;
  }
  size_t used() const {
    return end_offset();
  }
  size_t entry_count() const {
    return count_;
  }
  // Largest arena use so far (diagnostics).
  size_t peak() const {
    return peak_;
  }

  // Sheets that did not fit unfiltered: later chapters linking them go
  // straight to the per-chapter filtered load.
  void mark_too_big(uint32_t key);
  bool too_big(uint32_t key) const;

 private:
  struct Entry {
    uint32_t key = 0;
    uint32_t offset = 0;
    uint32_t last_used = 0;
    bool chapter_only = false;
    bool used = false;
    CssStylesheet sheet;
  };

  // Entries live in fixed slots and never move, so the CssStylesheet pointers
  // handed out by find()/load() stay valid while other sheets are loaded or
  // evicted (remove() only moves arena bytes and rebases sheets). order_ lists
  // the used slots in arena order.
  Entry entries_[kMaxEntries + 1];  // + the inline sheet
  uint8_t order_[kMaxEntries + 1] = {};
  size_t count_ = 0;
  uint8_t* arena_ = nullptr;
  std::vector<uint8_t> owned_arena_;
  uint32_t gen_ = 1;
  size_t peak_ = 0;
  static constexpr size_t kMaxTooBig = 8;
  uint32_t too_big_[kMaxTooBig] = {};
  size_t too_big_count_ = 0;

  size_t end_offset() const {
    if (count_ == 0)
      return 0;
    const Entry& last = entries_[order_[count_ - 1]];
    return last.offset + last.sheet.size_bytes();
  }
  // Drops the least recently used sheet this chapter does not use; false if none.
  bool evict_one();
  // Removes the i-th sheet in arena order and moves the sheets after it down.
  void remove(size_t i);
};

// Callback for element id="" annotations encountered during streaming XHTML parsing.
// Called once per element that has an id attribute.
// para_idx = number of paragraphs already emitted before this element opens.
using IdSink = void (*)(void* ctx, const char* id, size_t id_len, uint32_t para_idx);

enum class EpubError {
  Ok = 0,
  ContainerMissing,
  ContentOpfMissing,
  InvalidData,
  ZipError,
  XmlError,
  CrcMismatch,  // chapter data read back wrong from the SD card
};

// EPUB book: parsed from an EPUB file.
// Stores the ZIP entries, spine, metadata, and global stylesheet.
// Chapters are parsed on-demand via parse_chapter().
class Epub {
 public:
  // xml_buf size for parse_chapter_streaming(): the XML reader's part, the CSS
  // parser's scratch and the stylesheet arena (a display framebuffer).
  static constexpr size_t kChapterXmlSize = 16384;
  static constexpr size_t kChapterBufSize =
      kChapterXmlSize + CssStylesheet::Parser::kScratchSize + CssCache::kArenaSize;

  Epub() = default;

  // Set CSS unit conversion config (call before open()).
  void set_css_config(const CssConfig& config) {
    css_config_ = config;
  }
  const CssConfig& css_config() const {
    return css_config_;
  }

  // Open an EPUB file. work_buf (~45KB) and xml_buf (~4KB) are used during
  // OPF/NCX parsing. Caller must provide both; allocate them before calling.
  // If parse_css_ncx is false, skips extracting CSS and parsing NCX (fast for indexing).
  EpubError open(IZipFile& file, uint8_t* work_buf, uint8_t* xml_buf, bool parse_css_ncx = true);

  // Lightweight open: only parses the ZIP central directory.
  // No OPF/NCX/CSS parsing — only zip().entry() is available afterwards.
  // Sufficient for image decode operations.
  EpubError open_zip_only(IZipFile& file);

  // Reads only the metadata (title, author, language, cover id) — for the book
  // index. Keeps just container.xml and the OPF in the ZIP table and stops
  // reading the OPF after <metadata>, so it needs almost no heap whatever the
  // book's size. Spine, TOC and CSS stay empty.
  EpubError open_metadata(IZipFile& file, uint8_t* work_buf, uint8_t* xml_buf);

  // Release all parsed data (ZIP entries, spine, stylesheet, TOC, metadata).
  void close();

  // Number of chapters (spine items).
  size_t chapter_count() const {
    return spine_.size();
  }

  // Parse a specific chapter by index.
  EpubError parse_chapter(IZipFile& file, size_t index, Chapter& out) const;

  // Stream-parse a chapter: paragraphs are emitted one at a time via sink.
  // Uses ~37KB working memory instead of extracting the full XHTML.
  // xml_buf_size: size of xml_buf (0 = the 16 KB minimum). With at least
  // kChapterBufSize, the stylesheets live in xml_buf too (see CssCache) and
  // stay there from chapter to chapter, so the caller must not touch xml_buf
  // between chapters — or call release_css() first. Otherwise the arena comes
  // from the heap.
  EpubError parse_chapter_streaming(IZipFile& file, size_t index, ParagraphSink sink, void* sink_ctx, uint8_t* work_buf,
                                    uint8_t* xml_buf, IdSink id_sink = nullptr, void* id_sink_ctx = nullptr,
                                    size_t xml_buf_size = 0) const;

  // Access metadata.
  const EpubMetadata& metadata() const {
    return metadata_;
  }

  // Access TOC.
  const TableOfContents& toc() const {
    return toc_;
  }
  TableOfContents& toc() {
    return toc_;
  }

  // ZIP entry index of the cover image declared in the OPF metadata
  // (<meta name="cover" content="..."/> resolved against the manifest), or
  // -1 when the book declares no cover. Only valid after a full open().
  int cover_entry_index() const {
    return cover_idx_;
  }

  // Access the zip reader (for image extraction etc)
  const ZipReader& zip() const {
    return zip_;
  }
  const std::vector<SpineItem>& spine() const {
    return spine_;
  }
  const CssCache& css_cache() const {
    return css_cache_;
  }
  // Forgets the parsed stylesheets (they may point into a caller's buffer).
  void release_css() const {
    css_cache_.clear();
  }
  // Chapters whose stylesheets could not be loaded completely (see
  // parse_chapter_streaming). Should stay 0; logged by the converter.
  size_t css_incomplete_chapters() const {
    return css_incomplete_;
  }

  // Resolve a path relative to a content file's directory.
  // e.g. resolve_path("OEBPS/chapters/", "../images/test.jpg") → "OEBPS/images/test.jpg"
  static std::string resolve_path(const std::string& base_dir, const std::string& href);

  // Find an entry index by path.
  const ZipEntry* find_entry(const std::string& path) const {
    return zip_.find(path);
  }
  int find_entry_index(const std::string& path) const;

 private:
  ZipReader zip_;
  std::string root_dir_;  // e.g. "OEBPS/"
  EpubMetadata metadata_;
  std::vector<SpineItem> spine_;
  TableOfContents toc_;
  CssConfig css_config_;
  mutable CssCache css_cache_;
  mutable size_t css_incomplete_ = 0;
  int cover_idx_ = -1;

  // Internal parsing steps
  EpubError parse_container(IZipFile& file, std::string& rootfile_path, uint8_t* work_buf, size_t work_buf_size,
                            uint8_t* xml_buf, size_t xml_buf_size);
  EpubError parse_opf(IZipFile& file, const std::string& opf_path, uint8_t* work_buf, uint8_t* xml_buf,
                      bool parse_css_ncx, bool metadata_only = false);
};

// Parse XHTML body into paragraphs (used by Epub::parse_chapter, also
// usable standalone for testing).
EpubError parse_xhtml_body(const uint8_t* data, size_t size, const CssStylesheet* inline_css,
                           const CssStylesheet* extern_css, const std::string& base_dir, const ZipReader& zip,
                           std::vector<Paragraph>& out);

}  // namespace microreader
