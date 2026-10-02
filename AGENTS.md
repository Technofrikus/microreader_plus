# Microreader2 — Project Summary

E-ink ebook reader targeting **ESP32-C3 + SSD1677 e-paper** (800×480 physical, rotated 90° → **480×800 portrait**) with a **desktop SDL2 emulator** for rapid development.

## Architecture

```
lib/microreader/          ← shared core (platform-agnostic C++17)
  content/                ← EPUB parsing & layout pipeline
    mrb/                  ← MRB binary format (MrbFormat, MrbReader, MrbWriter, MrbConverter)
  display/                ← Canvas, DisplayQueue, Font, Display interface
  screens/                ← IScreen implementations (UI screens)
platforms/desktop/        ← SDL2 desktop emulator
platforms/esp32/          ← real hardware (ESP-IDF + PlatformIO)
test/                     ← Google Test suite (unit + integration)
tools/                    ← Python scripts (LUT editor, upload, etc.)
  calibre-plugin/         ← Calibre device plugin (build.py → microreader.zip)
```

**Core** defines abstract interfaces; each platform provides concrete implementations:

| Interface      | Desktop (SDL2)              | ESP32 (real hardware)     |
|----------------|-----------------------------|---------------------------|
| `IDisplay`     | `DesktopEmulatorDisplay`    | `EInkDisplay`             |
| `IRuntime`     | `DesktopRuntime`            | `Esp32Runtime`            |
| `IInputSource` | `DesktopInputSource`        | `Esp32InputSource` (ADC)  |

## Key distinction: Desktop vs ESP32

- **`platforms/desktop/`** = **emulator/simulator**. `display.h` has a per-pixel float `sim_` buffer that simulates e-ink particle physics (exponential approach). This is where display simulation tweaks go.
- **`platforms/esp32/`** = **real device**. `epd.h` drives the actual SSD1677 panel over SPI. No simulation needed.
- **`lib/microreader/`** = **shared application logic**. `Application`, `Canvas`, `DisplayQueue`, `Loop`, screens — runs identically on both platforms. Do NOT modify core files for desktop-only display concerns.

> **⚠️ AGENT RULE — Do NOT build or verify the desktop simulator.** We do **not** use the SDL2 desktop emulator for development or validation. Agents must **never** run the Desktop CMake build (`cmake -S platforms/desktop ...`) or rely on the simulator to check their work. All development, compilation, and testing happens **directly on the ESP32 device** (or QEMU for hardware-free runs). Skip the Desktop build commands entirely.

## EPUB content pipeline

Located in `lib/microreader/content/`. This is the chain that turns an `.epub` file into rendered pages:

```
EPUB file (ZIP on SD card)
  → ZipReader        (parse central directory, lazy extraction)
  → Epub::open()     (streaming OPF/NCX parse via XmlReader, builds spine + TOC)
  → Epub::parse_chapter()  (extract XHTML → parse into Chapter)
  → layout_page()    (Chapter + Font → PageContent with positioned words)
  → ReaderScreen     (PageContent → paint lambda → DisplayQueue)
```

### Key content files

| File | Purpose |
|------|---------|
| `ZipReader.h/.cpp` | ZIP central directory reader. `ZipEntry` stores `std::string_view name` + offsets (no data); all names live in a single contiguous `name_blob_` vector inside `ZipReader` (two-pass: count total name bytes, then bulk-allocate). `ZipEntryInput` streams decompression with ~33KB constant memory (32KB LZ dict + 1KB input buffer). `extract()` decompresses fully into `std::vector<uint8_t>`. |
| `MrbReader.h/.cpp` | Reads `.mrb` (pre-processed binary) files. Loads chapter table + image refs into RAM on `open()`, but the paragraph index is **not loaded into RAM** — each entry (8 bytes) is seeked on demand in `load_paragraph()` to avoid ~136KB allocation for large books. `MrbChapterSource` wraps `MrbReader` as an `IParagraphSource` with lazy paragraph caching per chapter. |
| `XmlReader.h/.cpp` | Streaming XML SAX parser with configurable buffer (typically 4–8KB). Emits `StartElement`, `EndElement`, `Text`, `CData` events. Pluggable `IXmlInput` for data source. |
| `EpubParser.h/.cpp` | `Epub` class: `open()` streams OPF via `ZipEntryInput` → `XmlReader` (never loads full OPF into RAM). Builds compact manifest (FNV-1a hashes, ~6 bytes/entry). `parse_chapter()` extracts full XHTML into memory then parses to `Chapter`. Also resolves image dimensions by extracting image files. |
| `ContentModel.h` | Core data structures: `Run` (styled text span), `TextParagraph` (runs + alignment/indent), `Paragraph` (Text/Image/Hr/PageBreak), `Chapter` (title + paragraphs), `SpineItem`, `TocEntry`, `EpubMetadata`. |
| `TextLayout.h/.cpp` | `IFont` interface for font metrics; `FixedFont` for testing. `layout_paragraph()` → word-wrap runs into `LayoutLine`s. `layout_page()` → fill one page from chapter position → `PageContent` with positioned `PageTextItem`/`PageImageItem`/`PageHrItem`. |
| `hyphenation/Hyphenation.h/.cpp` | Liang hyphenation (TeX patterns compiled to tries by Typst `hypher`, one `Liang/hyph-*.trie.h` per language). `find_hyphen_break()` classifies each code point of a token as letter / digit / dash / other: Liang runs **only on maximal letter runs**, so quotes („ “ ” ‘ ’ « »), apostrophes, full stops and dashes never count toward the letter minimums (prevents "ethic-\|s.”"). Minimums are per language in code points: English left 2 / right 3 (TeX), German and others 2/2 (Duden). Explicit dashes (`-` U+2010 – —) are preferred break points when ≥1 letter/digit precedes and ≥2 follow; U+2011 (non-breaking hyphen) is not a break. |
| `CssParser.h/.cpp` | Minimal CSS parser for EPUB stylesheets. `CssStylesheet` cascades by specificity. Only simple selectors (element, `#id`, `.class`, compounds) are stored, as one flat byte sequence of groups — `[CssRule][u16 n]` then n × `[el_len][id_len][class_count][names]`, so a comma list stores its properties once. Storage is the sheet's own heap vector (tests, in-memory parsing) or a fixed caller region via `use_external()` (the conversion's CSS arena), where a full region sets `overflow()` instead of allocating. `CssStylesheet::Parser` is a streaming, char-at-a-time state machine (`feed()` any chunk size, `finish()`), equivalent to parsing the whole text: comments stripped, `@`-rules and nested blocks skipped, selectors split on `,` and declarations on `;` as they arrive — only the current selector (2560 B) and declaration (1536 B) are buffered, in a caller scratch (`kScratchSize`). `CssRule::apply()`/`finish()` are `parse()` split per declaration (padding adds to margin, small-caps checks text-transform, the margin clamp needs the whole rule). An optional `CssNameSet` (sorted FNV-1a hashes) drops selectors naming anything a chapter doesn't use. |
| `ImageDecoder.h/.cpp` | JPEG/PNG detection and dimension reading. `ImageSizeStream` is a streaming header parser (~44 bytes state) used for lazy image size resolution. `images_enabled` runtime flag (default `true`). |
| `Book.h/.cpp` | High-level wrapper: owns `StdioZipFile` + `Epub`. Methods: `open(path)`, `load_chapter(index, Chapter&)`, `decode_image()`. |
| `MrbFormat.h` | MRB binary format constants and on-disk structs: `MrbHeader` (32 bytes), `MrbChapterEntry` (16 bytes), `MrbImageRef` (8 bytes: `local_header_offset(u32)` + `width(u16)` + `height(u16)`), paragraph record layout. Version 3. Shared between `MrbReader` and `MrbWriter`. |
| `MrbWriter.h/.cpp` | Writes `.mrb` binary files. `BufferedFileWriter` batches into 4KB buffer. Paragraphs are doubly-linked (prev/next offsets). Uses **deferred paragraph writing**: each paragraph is serialized into `pending_para_` buffer, then flushed when the *next* paragraph arrives (so `next_offset` can be filled in without seeking). This eliminates all backward seeks in `end_chapter()`. |
| `MrbConverter.h/.cpp` | Orchestrates EPUB→MRB conversion: iterates spine chapters, streams paragraphs via `ParagraphSink` callback from `EpubParser` → `MrbWriter`. Exposes `convert_epub_to_mrb()` and `convert_epub_to_mrb_streaming()`. |
| `HtmlExporter.h/.cpp` | Exports a `Book` to a self-contained HTML file using the `TextLayout` engine to paginate content exactly as the reader would. Used by `HtmlExportTest`. Options control page size, padding, chapter limit, and debug output. |
| `CoverSleep.h/.cpp` | Book cover as the sleep screen. `build_cover_cache()` decodes the cover straight from the EPUB ZIP (via `read_local_entry` + `decode_image_from_entry` with a row sink) into one of `DrawBuffer`'s two framebuffers, using the other as the decoder work buffer — **no heap allocation**. The result is dumped as a screen-ready framebuffer plane to `<data_dir>/cache/<stem>/cover_<panel_w>x<panel_h>.fb`; `show_cover_sleep()` reloads it with a single sequential read and one `full_refresh(Full, turnOffScreen=true)`. Built at the end of a book's first EPUB→MRB conversion when cover mode is selected (`ReaderScreen::start`), lazily at sleep time otherwise (`Application::show_cover_sleep_`). The cover is fitted, never cropped (`cover_fit_size`). Cover location: the EPUB's declared cover (`Epub::cover_entry_index()`) when the book is open, else MRB image 0 (images are recorded in document order). |
| `SleepImageList.h/.cpp` | Scans the SD card's `sleep/` folder (`/sdcard/sleep`, `sd/sleep` on desktop) for `.mgr`/`.bmp` files. Returns file names plus a `bmp` flag rather than full paths — a large sleep folder held as full path strings is a lot of heap for a menu — with `value()` rebuilding the string `Application::sleep_image_path()` persists. Sorted case-insensitively so the picker and the auto-cycle agree on an order; the persisted cycle index would otherwise mean something different after every reboot. |
| `BmpSleepConverter.h/.cpp` | Converts BMP files to MGR2 format (2bpp, 4-level grayscale) for sleep screen images. Supports 1bpp, 2bpp, 4bpp, 8bpp (indexed), 16bpp (RGB565/BGR555), 24bpp, and 32bpp (BI_RGB/BI_BITFIELDS) formats. Two output modes: (1) **COVER mode** (both out_w/out_h > 0) scales the image to fill the target then crops the minimal excess — used with the device's native resolution (X3: 792×528, X4: 800×480) so no white borders appear; (2) **auto-size mode** (out_w=0 or out_h=0) outputs the source dimensions after rotation. Also has a 1bpp two-plane output mode (`.1b.mgr`: BW plane then RED plane, each 1-bit packed) — the format is decided by the file NAME, not a header byte. |

### Sleep image upload pipeline (1bpp fast path)

`DrawBuffer::show_mgr2_sleep_()` renders `.1b.mgr` sleep images via a dual-plane grayscale refresh. The 1bpp path has three upload strategies, tried in order:

1. **Direct upload** (memory-backed + native resolution only): when the source is `mem_`-backed (embedded asset / mmap) AND `src_stride == config_.stride`, no x/y offset, and `panel_offset_x == 0`, the source buffer is handed straight to `display_.write_ram_bw()`/`write_ram_red()` — no `fill(false)`, no per-pixel copy, no framebuffer round-trip. ~50ms per plane (pure SPI cost) vs ~241ms with the old per-pixel loop. **File-backed sources must NOT use this path** — `get_plane_row()` returns a pointer into a 256-byte `row_buf_` containing one row, and the display reads the full ~52KB plane from it, reading past the buffer into garbage.
2. **Byte-aligned memcpy**: when `x_offset % 8 == 0`, per-row `memcpy` replaces the bit-extract loop. ~50x faster than per-pixel OR on ESP32-C3.
3. **Per-pixel fallback**: the original bit-extract loop, used only when the source isn't byte-aligned with the framebuffer.

On X3, `EInkDisplay::write_ram_bw()`/`write_ram_red()` use `x3SendMirroredPlaneChunked_()` instead of `x3SendMirroredPlane_()`: it pre-mirrors the plane into a lazily-allocated ~52KB buffer (`x3_mirror_buf_`) and sends it in a few large SPI transactions (4092-byte chunks) instead of one per row, cutting per-row transaction setup overhead (~5-10ms over 528 rows). Falls back to per-row sends if the mirror buffer can't be allocated. **CS must stay LOW for the entire plane write** — the X3 controller's RAM address auto-increment does not persist across CS toggles, so toggling CS per chunk makes every chunk overwrite address 0.

### Memory constraints (critical for ESP32-C3)

The ESP32-C3 has **~320KB total RAM**, of which **~155KB is free after SD card init**. The EPUB pipeline must fit within this budget:

| Component | Typical Cost | Notes |
|-----------|-------------|-------|
| `Book::open()` metadata | 30–45KB | ZIP entries × ~60 bytes each (struct + string_view into contiguous name blob — 2 allocations total) + spine + TOC + stylesheet. War and Peace (377 entries) verified on device: 126KB free after open. |
| `MrbReader::open()` | 3–5KB | Chapter table (368 × 8 = ~3KB) + image refs + metadata/TOC. Paragraph index is NOT loaded into RAM — seeked on demand. |
| `parse_chapter()` XHTML extract | up to 218KB | Full chapter XHTML decompressed into `std::vector<uint8_t>` + 33KB work buffer |
| `parse_chapter()` image dim resolve | up to 200KB | Extracts each image file to read JPEG/PNG headers |
| `Chapter` in memory | up to 250KB+ | 268 paragraphs × 684 runs × `std::string` text (177KB text alone for largest chapter) |
| `layout_page()` | ~5–10KB | One page at a time, returns `PageContent` with word pointers into `Chapter` |

**Known crash**: ohler.epub ("Der totale Rausch") has 487 spine items mapping to only ~10 XHTML files, the largest being 218KB. The current `parse_chapter()` approach of extracting the full XHTML + resolving image dimensions exceeds available heap. **This needs to be fixed** — likely by pre-processing EPUBs into a compact on-device format or by streaming the XHTML parsing.

**`convert_epub_to_mrb_streaming()`** in `MrbConverter.h` uses ~37KB of working memory per chapter (vs. full XHTML extract) and is safe for ESP32's limited RAM. Optional `work_buf`/`xml_buf` parameters let callers pass pre-allocated split buffers to avoid heap fragmentation from a single large allocation.

### ⚠️ CRITICAL: Image sizes are NOT resolved during MRB conversion

The MRB conversion step does **NOT** load, decode, or read the dimensions of any images. It records the **local file header offset** (`local_header_offset`) for each image reference, allowing direct access to the image data in the EPUB ZIP at render time without parsing the central directory. Image dimensions stored in `MrbImageRef.width/height` come **only** from explicit HTML `width`/`height` attributes in the EPUB's XHTML — most EPUBs do **not** have these, so `width=0, height=0` is the common case.

**Do NOT try to add image size resolution to the conversion step.** Image dimensions are resolved lazily at display time by `ReaderScreen::resolve_image_size_()`, which decodes images directly from the EPUB using `ZipReader::read_local_entry()` + `decode_image_from_entry()`. This reads ~30 bytes from the local file header (no central directory needed), saving ~35KB+ vs the old `open_zip_only()` approach that parsed the full central directory and caused OOM on books like ohler.epub (487 entries).

### CSS arena (stylesheets during conversion)

`Epub::parse_chapter_streaming()` splits its `xml_buf` (a framebuffer, `Epub::kChapterBufSize` = `DrawBuffer::kBufSize` = 52,768 B, static_assert in `MrbConverter.cpp`) into `[XML reader 16 KB][CSS parser scratch 4 KB][CssCache arena 32,288 B]`. Before the body parse, `load_chapter_css()` (EpubParser.cpp) scans the head for `<link rel="stylesheet">` (keyed by ZIP entry index) and `<style>`, then streams each CSS file from the ZIP in 16 KB chunks through `CssStylesheet::Parser` into the arena — neither the CSS text nor the parsed rules use heap, so styling is identical on every device and in every memory state (Bluetooth on or off). `CssCache` entries live in fixed slots (an `order_` index gives arena order), so the `CssStylesheet*` returned by `find()`/`load()` stay valid while later loads evict other sheets — `remove()` only moves arena bytes and rebases sheets; never make entries move. The head scan skips oversized tags/comments (`skip_element()` on `BufferTooSmall`). Sheets stay in the arena across chapters; to make room, sheets the current chapter doesn't use are dropped (LRU) and the rest moved down (`CssCache::remove()` rebases the views). `<style>` blocks are a per-chapter sheet (`kInlineKey`).

**Fallback:** if a chapter's sheets don't fit even alone, the chapter is scanned once for every element name, id and class it uses (`collect_names()`, hashes stored at the arena's top), and the sheets are reloaded keeping only rules whose names all occur — rules that can't match anything in the chapter — so the chapter is still styled exactly. Such sheets are per-chapter and the key is remembered (`mark_too_big()`) so later chapters go straight to filtering. Only if even that doesn't fit (never seen) are the rules that fit kept, `Epub::css_incomplete_chapters()` counts it and the converter logs `styling incomplete`.

Measured on 519 real EPUBs (desktop driver, same code): peak arena use 24 KB, 517 books under 16 KB, no fallback needed; 514 MRBs byte-identical to the previous heap-based parser. The other 5 were old bugs, now fixed: `<style>` wrapped as `/*<![CDATA[*/ … /*]]>*/` arrives as three XML text events and the old parser parsed each piece separately (lost the rules — Kehlmann *Ruhm*, Schirach *Collini*, Morris), and the old cache evicted a sheet of the *first* styled chapter while loading its second sheet (generation 0 disabled protection — Forrest *Coldbloods* title page). `big_css.epub` (fixture, 110 KB sheet) tests the fallback.

The converter calls `Epub::release_css()` before and after, because the arena lives in a framebuffer the screens draw into between conversions. Callers of `parse_chapter_streaming()` must not touch `xml_buf` between chapters. With a smaller `xml_buf` (tests, desktop) the arena and scratch come from the heap — same size, same results.

## Screens (IScreen implementations)

All inherit from `IScreen` (`lib/microreader/screens/IScreen.h`): `name()`, `start()`, `stop()`, `update()`.

| Screen | File | Description |
|--------|------|-------------|
| `MainMenu` | `MainMenu.h/.cpp` | Main book list/browser screen. Displays books and subfolders in a hierarchical folder tree structure on the SD card with left-aligned items. Prefixes folders with `/`. Uses `BookIndex` for title/author metadata formatting. |
| `ReaderScreen` | `ReaderScreen.h/.cpp` | EPUB page viewer. 2×-scaled 8×8 bitmap font (16×16 glyphs). Next/prev page, chapter transitions. Three-slot status bar (left/middle/right) via `StatusInfo` in `ReaderOptionsScreen.h` — options include book/chapter %, ETA, battery, and paragraph counts (`ParaChapter`/`ParaBook`/`ParaChapterTotal`/`ParaBookTotal`, computed from `page_pos_.paragraph` + per-chapter paragraph counts — cheap, no layout needed). |
| `SleepImageScreen` | `SleepImageScreen.h/.cpp` | Sleep-image picker, pushed from the Settings row (which stays one line however many images are on the card). Lists `Auto (cycle)`, `Book Cover`, then every image `list_sleep_images()` finds; the one in use is bulleted and `set_selected()` starts the cursor there. `on_long_select` previews an entry through `Application::preview_sleep_image()` — the same code path a real sleep takes — then calls `suppress_redraw()` so the list is not painted back over it; any button exits via `reset_after_scratch()` + `full_refresh()` (which also takes an X3 out of grayscale mode). Item storage is released in `stop()`. |
| `BouncingBallDemo` | `BouncingBallDemo.h/.cpp` | Bouncing ball + random shapes. |


### Screen navigation flow

```
Application
  └─ ScreenManager (push/pop stack, max depth 8)
       └─ MainMenu (always at bottom)
            ├─ "Select Book" → push BookSelectScreen
            │     └─ user picks → push ReaderScreen (via Application chain)
            └─ "Bouncing Ball" → push BouncingBallDemo

Button0 = back (screen returns false → pop)
Button1 = select
Button2 = down / next page
Button3 = up / prev page
Up (side, Vol+) = long-press (800 ms) toggles portrait/landscape rotation (hotkey)
Down (side, Vol-) = short press = prev page in reader
```

## Core systems

- **DisplayQueue**: dual-buffer (ground_truth + target) phase-based animation. Commands progress over N phases before committing.
- **Refresh modes** (`DrawBuffer.h`): `refresh()` uses the fast/partial waveform. `full_refresh()` defaults to `RefreshMode::Half` (the slower, cleaner half-refresh waveform); callers can explicitly request `RefreshMode::Full` when needed.
- **Canvas**: z-ordered scene graph with damage-rect redraw. Elements: `CanvasRect`, `CanvasCircle`, `CanvasText`.
- **Proportional font system** (`BitmapFont.h`, `BitmapFontFormat.h`, `DrawBuffer.h`):
  - **MBF format**: Custom binary font format. One file per pixel size, each containing up to 4 styles (Regular/Bold/Italic/BoldItalic). Generated from TTF via `tools/generate_font.py`.
  - **BitmapFont**: Reads MBF data via `init(data, size)`. Zero-heap: all pointers reference the input buffer (works with mmap). Implements `IFont`.
  - **FontManager**: Manages the loaded fonts, parsing the `FNTS` bundle inside the file system and holding up to `kMaxFontSizes` (8) scale variations dynamically.
  - **DrawBuffer::draw_text_proportional()**: Renders text using `IFont*`.
  - **Font sizes**: The typical bundle uses exactly 4 sizes: 20px, 24px, 28px, 32px (configurable via `--bundle-sizes` in `generate_font.py`), which are mapped sequentially into `ReaderSettings::font_size_idx`.
  - **FNTS bundle**: Multi-font bundle format for ESP32 flash partition. Format: `[FNTS:4][num:1][version:1(=1)][reserved:2][font_name:32][num×size:4][data...]`. Font name is null-terminated, zero-padded to 32 bytes. Uploaded as a single blob via `serial_cmd.py --upload-font`.
  - **ESP32 storage**: Fonts live in the SPIFFS-style font partition (3.375 MB), mmapped via `font_partition.h`. The font bundles (zlib-compressed FNTS) are embedded directly in the firmware image as DROM data (see asset blob below). On first book open (or after a firmware update with a new font CRC), `FontPartition::provision_embedded()` stream-decompresses the requested bundle into the partition (~15s one-time cost). The partition header stores the manifest CRC32 to skip re-provisioning on subsequent boots. After regenerating fonts with `generate_font.py`, just rebuild — `tools/generate_assets.py` is invoked as a PIO pre-build script and picks up the new `.bin` files automatically.
  - **Asset blob (DROM-embedded in firmware)**: `tools/build_assets.py` packages `bookerly.bin`, `alegreya.bin`, and the three sleep `.mgr` images into `assets.bin` (manifest = `[ASTS:4][version:4=1][count:4][total:4]` + N×44-byte entries `{name[32], offset, length, crc32}`). The PIO pre-build script `tools/generate_assets.py` generates both `platforms/esp32/assets.bin` and `platforms/esp32/assets_embedded.S` (an assembly file with `.incbin` that embeds the binary as `_binary_assets_bin_start`/`_binary_assets_bin_end` symbols in the `.rodata` section). `assets_embedded.S` is listed in `SRCS` in `CMakeLists.txt` so it compiles into the firmware as DROM data. `asset_blob.cpp` reads directly from those symbols — no partition reads, no mmap. Runtime access: `asset_blob::g_assets.init()` in `app_main`, then `map(name, &size, &handle)` / `unmap(handle)` / `crc(name)` return direct DROM pointers. `FontManager::ensure_ready()` and `DrawBuffer::show_sleep_image_embedded()` map their asset on demand and unmap immediately after use. **Tradeoff**: assets (~2 MB) consume DROM MMU pages, but firmware.bin is a valid self-contained IDF binary — web flashers (ESP Web Tools etc.) work correctly. Flash usage ~52%, RAM usage ~36%.
  - **Partition header**: 12 bytes: `[FONT:4][decompressed_size:4][embedded_crc32:4]` followed by raw FNTS data.
  - **Desktop**: Loads individual `.mbf` files from `resources/fonts/` (`font-0.mbf` up to `font-3.mbf`).
  - **Generation**: `python tools/generate_font.py "resources/fonts/Bookerly.ttf" -o resources/fonts/font-normal.mbf --with-styles --bold "resources/fonts/Bookerly Bold.ttf" --italic "resources/fonts/Bookerly Italic.ttf" --bold-italic "resources/fonts/Bookerly Bold Italic.ttf" --bundle --bundle-sizes 20 24 28 32 --font-name Bookerly` generates 4 sizes + bundle.
- **Input**: `ButtonState` carries `current` + `pressed_latch`. Auto-repeat at hardware layer (5ms sample on ESP32). Screens use `is_pressed()`.
- **Bluetooth LE HID remote** (`platforms/esp32/ble_hid.{h,cpp}`, `lib/microreader/Bluetooth.h`, `HidReport.h`, `screens/BluetoothScreen.*`): NimBLE central with one bonded remote, used as a page turner. See "Bluetooth remote" below.
- **Loop**: `run_loop()` polls input → app.update() → queue.tick() → wait_next_frame().
- **Logging** (`HeapLog.h`): `MR_LOGI(tag, fmt, ...)` maps to `ESP_LOGI` on device and `printf("[tag] fmt\n")` on desktop. `HEAP_LOG(tag)` logs free heap + largest block (ESP32-only, no-op on desktop). No `ILogger` abstraction — use these macros directly.

## Build commands

### Desktop
```bash
cd microreader2
cmake -S platforms/desktop -B build/desktop-debug -DCMAKE_BUILD_TYPE=Debug "-DCMAKE_POLICY_VERSION_MINIMUM:STRING=3.5"
cmake --build build/desktop-debug --config Debug
```

### ESP32 (PlatformIO)
```bash
cd microreader2
# Build only:
$env:USERPROFILE\.platformio\penv\Scripts\pio.exe run
# Build + flash:
$env:USERPROFILE\.platformio\penv\Scripts\pio.exe run -t upload
# Monitor serial:
$env:USERPROFILE\.platformio\penv\Scripts\pio.exe device monitor --baud 115200
```
- Board: ESP32-C3-DevKitM-1, 16MB flash, COM4
- Upload baud: 921600, monitor baud: 115200
- Main task stack: 16KB
- ESP32 sources are **explicitly listed** in `platforms/esp32/CMakeLists.txt` (NOT auto-discovered by PIO LDF). When adding new `.cpp` files, you must add them to the source list.

### Tests

Three levels of testing, from fastest to most thorough:

#### 1. Unit tests (fastest — run after every change)

```bash
cd microreader2/test
cmake -B build2 -DCMAKE_BUILD_TYPE=Debug -DCMAKE_POLICY_VERSION_MINIMUM:STRING=3.5
cmake --build build2 --config Debug
.\build2\Debug\unit_tests.exe
```

~589 tests, runs in <1 second. Covers ZipReader, XmlReader, CssParser, EpubParser, ImageDecoder, TextLayout, and Input (ButtonState::consume / detect_long_press) with synthetic fixtures only. **Always run this first.**

VS Code task shortcut: **Run Unit Tests** (builds + runs automatically).

#### 2. Integration tests (includes real EPUB books)

```bash
.\build2\Debug\microreader_tests.exe
```

Includes everything from unit tests plus RealBookTest, BulkBookTest, DebugOhlerTest, HtmlExportTest. Requires real `.epub` files in `test/books/`. Run a subset with `--gtest_filter`:

> **⚠️ Real EPUB books are gitignored and NOT present in this repo** (`.gitignore` excludes `/resources/books`, `/sd/books/*.epub`, `/data/books`, etc.). Integration tests that need them — `HtmlExportTest`, `DebugLayoutTest`, `HrBackwardTest`, `DitherComparisonTest`, `BulkBookTest`, and `RealBookTest.StressTest_AllBooks` / `RegressionTest.SnowCrash` — will **SKIP** (not fail) when the books are missing. This is **expected and pre-existing**, not a regression — do not treat skipped tests as failures. To actually run them, drop real `.epub` files into `test/books/small/`, `sd/books/`, `microreader/resources/books/`, or `TrustyReader/sd/` (all gitignored).

```bash
.\build2\Debug\microreader_tests.exe --gtest_filter="HtmlExport.*"
.\build2\Debug\microreader_tests.exe --gtest_filter="DebugOhler.*"
```

VS Code task shortcuts: **Run All Tests**, **Run HTML Export Tests**.

#### 3. On-device tests (real hardware or QEMU)

Tests book opening, page navigation, and heap stability on actual ESP32 firmware.

**Real device (COM4):**
```bash
# Build + flash firmware
& "$env:USERPROFILE\.platformio\penv\Scripts\pio.exe" run -t upload

# Run book tests (opens each book, navigates pages, checks for crashes)
python tools/test_books.py --port COM4 --pages 20 --heap
```

**QEMU (no hardware needed):**
```bash
# Terminal 1: start QEMU with books pre-loaded
python tools/run_qemu.py --with-books

# Terminal 2: run tests against QEMU
python tools/test_books.py --port socket://localhost:4444 --pages 20 --delay 0.1
```

`test_books.py` options:
- `--pages N` — pages to navigate per book (default: 20)
- `--heap` — print heap stats per book (before/after open/close)
- `--filter STR` — only test books matching STR
- `--clean` — delete all `.mrb` files first (forces fresh conversion)
- `-v` — verbose per-page output

#### Recommended workflow

1. **Edit code** → run **unit tests** (task or `unit_tests.exe`)
2. If content/layout changes → run **integration tests** (`microreader_tests.exe`)
3. If touching memory, images, or ESP32-specific code → **flash + test_books.py** on device
4. For CI-like coverage without hardware → **QEMU + test_books.py**

Two test binaries:
- `unit_tests`: ZipReader, XmlReader, CssParser, EpubParser, ImageDecoder, TextLayout tests
- `microreader_tests`: above + RealBookTest, BulkBookTest, DebugOhlerTest, HtmlExportTest

Test fixtures in `test/fixtures/` (synthetic EPUBs). Real books in `test/books/`.

## ESP32 hardware

- **MCU**: ESP32-C3 (RISC-V single core, 160MHz)
- **RAM**: ~320KB total, ~155KB free after SD init
- **Flash**: 16MB, dual OTA partitions (6.4MB each)
- **Display**: SSD1677 e-ink, 800×480 physical, rotated → 480×800 portrait, 1-bit packed (100-byte row stride)
- **SD card**: FAT32 via SPI (shares bus with display)
- **Buttons**: ADC-based with hardware auto-repeat
- **Rotate display (long-press SELECT in the Reader)**: Holding the **Select (Button1)** button for `ReaderScreen::kLongSelectMs` (500 ms) while reading toggles portrait↔landscape. Implemented in `ReaderScreen::update()`: the Select press is **deferred to release** so a long hold claims the button for rotation, while a short tap opens the reader options menu (or, if a nav-history back-stack is present, clears it). Toggling calls `set_rotate_display()` (persists to `settings`) and re-renders the current page in the new orientation. Rotation can also be toggled from the **Settings** screen and the **Reader Options** screen (`Display: Portrait/Landscape`). A two-button *simultaneous* chord (e.g. Up+Down) is **NOT possible** on this hardware — the side buttons share one ADC resistor-ladder channel and only one is reported per sample (see `platforms/esp32/input.h`).
- **MainMenu folder navigation (long-press UP)**: The MainMenu uses a long-press of Up (`up_uses_long_press_`) to go up one folder level. The decision is *deferred to release*: short tap → move selection up one; medium/long hold → go up one folder. This is independent of rotation now that rotation lives on the Select button, so there is no longer a race between the two. Do **not** re-add an immediate (while-held) `on_long_up()` fire in `ListMenuScreen`, or it will fire on every hold instead of waiting for release.
- **ETA debug overlay (`MR_ETA_DEBUG`)**: `ReaderScreen` has a compile-time-gated debug overlay (`-DMR_ETA_DEBUG=1` build flag, or flip the `#define` in `ReaderScreen.h`). When enabled, it draws a bar along the **top edge** (mirroring the bottom status bar) showing the raw ETA internals **not already on the status bar**: ms/char (Q16) + `has_valid_eta_`, current page elapsed time (seconds, 2 decimals) + char count, and chapter position/total chars. Numbers use thousands separators. The three metrics are joined with ` | ` separators and **centered** on a single line. It reserves `kEtaDebugTopReserve` (20px) of top padding so book text is pushed below the bar, and insets the text `kEtaDebugSidePad` (5px) from the edges (and 4px down) to clear the bezel above the screen. Defaults to **OFF** so the shipping firmware stays clean and small — the overlay code is `#if`-compiled out entirely.

## Bluetooth remote (BLE HID)

Page turning with a BLE keyboard / page turner / media remote. The ESP32-C3 is **BLE-only** (no Bluetooth Classic).

- **Layering**: `lib/microreader/Bluetooth.h` defines `IBluetooth` (platform-agnostic). `platforms/esp32/ble_hid.cpp` implements it; `main.cpp` calls `app.set_bluetooth(&ble_hid::instance())`. Desktop/tests pass nothing, so `app->bluetooth()` is `nullptr` and the Settings row is hidden. `BluetoothScreen` (a `ListMenuScreen`) is built on all platforms.
- **Input path**: HID notifications are decoded on the NimBLE host task by `hid::report_actions()` (`HidReport.h`, unit-tested in `test/unit/HidReportTest.cpp`) and queued as `Button` indices. `Esp32InputSource::poll_buttons()` drains them via `ble_hid::take_presses()`, like `g_serial_buttons`. Next/Prev are delivered as **`Button::Down` / `Button::Up`**, so they turn pages in the Reader (respecting Reader Controls inversion) and move the selection in menus; Select → `Button1`, Back → `Button0`. Injected presses are edge-only (never set `current`), so hold gestures are device-button only. Presses set `pressed_latch`, so they reset the auto-sleep timer.
- **Decoder**: no HID report-descriptor parsing; reports are recognised by shape, and each report characteristic has its own stateful `hid::ReportDecoder`. Key reports: 8-byte with `data[1]==0` = keyboard (usages in bytes 2–7); 2- or 4-byte = consumer-control usages (16-bit LE); 3-byte = mouse buttons (left → Select, right → Back; both tested clickers send their long presses this way, but in opposite directions — remotes named in `kSwappedLongPress` in `ble_hid.cpp` (currently `TP-1`) get the buttons swapped, decided by the bonded name in `on_ready_()`). **Touch clickers** ("TikTok" remotes) pose as a touchscreen and draw a swipe per press; `parse_touch()` accepts two single-contact digitizer shapes, both with flags ≤ 0x07 in byte 0 (bit 0 = tip): 8-byte (contact id ≠ 0 in byte 1, X/Y 16-bit LE at 2–5) and 4-byte (X/Y 12-bit packed in bytes 1–3). A touch is classified once, **as soon as it has travelled `kSwipeMin` (64 units)** — not at the lift: a clicker's ~10 frames arrive a few per connection event, so waiting for the lift added ~0.4 s. Vertical: up → Next, down → Prev. **Horizontal swipes are ignored** — both tested clickers send them for a double click. Lifting before `kSwipeMin` is a tap → Select (LY-03's extra button). The touch check runs before the consumer check because both can be 4 bytes. Swipes are logged at INFO (`swipe dx= dy=`), and so is every non-zero key report (handle, action, hex bytes; `(unmapped)` when it decodes to nothing) — that's how to add a new remote. Some clickers auto-repeat a held button as press/release pairs (~200 ms apart); a Select/Back pressed again within `kRepeatGapMs` (800 ms — TP-1's first repeat comes ~600 ms after the press) of last being held is dropped, so a long press doesn't pop several screens. Page turns are not debounced.
- **GATT setup chain**: HID service `0x1812` → characteristics (Report `0x2A4D` / Boot Keyboard Input `0x2A22` with NOTIFY) → descriptors → CCCD writes → `ble_gap_update_params` to 150–200 ms, latency 4 (the reader is the central, so only the interval saves its power; latency only helps the remote). A shorter interval (30–45 ms, also as a burst mode after input) was tried with touch clickers and didn't help: most of their ~1 s press-to-page delay is the remote itself (it waits to rule out a double click, which it sends as a horizontal swipe) plus the ~390 ms e-ink refresh. Remote-initiated parameter requests (`BLE_GAP_EVENT_CONN_UPDATE_REQ` / `L2CAP_UPDATE_REQ`) are clamped by `clamp_params()` — many keyboards ask for 7.5–15 ms, which would multiply the reader's radio wake-ups. **Descriptor discovery must run per characteristic** (`val_handle`..next `def_handle - 1`): NimBLE reports every descriptor against the start handle it was given, so a single service-wide `ble_gattc_disc_all_dscs` cannot match CCCDs to reports (this bug silently subscribed nothing). The ready log prints `N of M reports subscribed`. Setup runs at a 7.5–15 ms interval (`kSetupItvlMin/Max`); a remote's counter-request during setup is clamped to that floor.
- **Reconnect handle cache**: after the first full setup, the subscribed report value handles are saved in NVS (`ble_hdl` blob, `HandleCache` = peer addr + handles). On reconnect to that peer, `on_encrypted_()` skips discovery **and the CCCD writes** — it trusts the bonded remote to keep its subscriptions (spec behaviour for bonded clients) — so the remote is ready ~75 ms after connecting instead of ~1.65 s (measured on X3 with TP-1). The cache is cleared on Forget, on pairing a new remote, on `REPEAT_PAIRING`, and when a notification arrives on an unknown handle while Connected (next connect then rediscovers). A remote that forgets its subscriptions would stay silent until re-paired. The ready log prints `remote ready in N ms (cached handles)`.
- **State / persistence**: NVS namespace `microreader`, keys `ble_on` (u8), `ble_name` (str) and `ble_hdl` (blob, handle cache). Bonds are stored by NimBLE (`CONFIG_BT_NIMBLE_NVS_PERSIST`). Pairing is manual (Bluetooth screen → Pair New Remote → select); Just Works only (`BLE_HS_IO_NO_INPUT_OUTPUT`). Pairing a new remote deletes the other bonds.
- **Threading**: the NimBLE host task owns all connection state. `IBluetooth` commands (`forget_remote`, `start_pairing`, `stop_pairing`, `pair`, `on_user_activity`) only post a `ble_npl_event` to NimBLE's default event queue; the handler runs on the host task. Only `set_enabled()` (stack start/stop) runs on the UI task. `BluetoothScreen` therefore doesn't redraw right after a command: it lifts its rebuild throttle and redraws when `revision()` changes.
- **Lifecycle**: `ble_hid::boot()` runs after `app.start()` — i.e. after the first frame is on the glass — so its ~90–190 ms controller/PHY init doesn't delay the visible boot (the X3 blocks during a refresh, so it can't overlap it). It starts the stack only if `ble_on`. Measured on X3: largest free block after BT start went 86 → 90 KB with this ordering. Switching it on at runtime can fail on a fragmented heap: the setting stays on and the state is `RestartNeeded` (it starts at the next boot). `ble_hid::shutdown()` runs before `sd_deinit()`/deep sleep. **Don't toggle the stack on/off for memory reasons** (e.g. around conversions): deinit leaves the heap fragmented (largest block 110 → 74 KB in testing).
- **Radio budget**: reconnect scans at 10% duty (20 ms / 200 ms) for 30 s, then ~5% (30 ms / 640 ms) for 150 s, then pauses in `Standby`. `Application::update()` calls `IBluetooth::on_user_activity()` on every button press, which restarts the search from `Standby`; waking from deep sleep restarts it too (boot). Pairing scan is active and stops after 60 s.
- **Memory (X3, book open)**: baseline 135.9 KB free / 110.6 KB largest; BT built in but off 122.3 / 110.6 KB; BT on 82.0 / 69.6 KB. The running stack costs ~40 KB. Sapiens and a 22 MB EPUB converted fine with BT on (min free ~37 KB).
- **sdkconfig**: see the Bluetooth block in `sdkconfig.defaults`. `CONFIG_BT_CTRL_RUN_IN_FLASH_ONLY=y` moves ~16 KB of controller code out of IRAM (C3 IRAM and DRAM share SRAM, so IRAM code is heap lost even while BT is off). Trade-off: the link may hiccup during internal-flash erase (font provisioning, OTA). `SPI_FLASH_AUTO_SUSPEND` would avoid that, but it conflicts with `SPI_FLASH_ROM_IMPL`. `CONFIG_ESP_WIFI_ENABLED` cannot be disabled (no Kconfig prompt), and WiFi code isn't linked anyway. When changing BT options, delete the matching lines from `sdkconfig.esp32c3` (tracked) so the defaults apply.

## Known issues & gotchas

- **Terminal `cd` parameter is REQUIRED**: When invoking the `terminal` tool, you MUST pass the `cd` parameter with an absolute path (e.g. `cd: /Users/tf/Nextcloud/gitfolder/microreader-plus/test`). Do NOT put `cd` inside the command string, and do NOT omit `cd` — the tool rejects input without it ("tool input was not fully received").
- **ESP32 CMake source list**: New `.cpp` files MUST be added to `platforms/esp32/CMakeLists.txt` explicitly. PIO's LDF auto-discovery does not work for this project structure.
- **CMake exit code 1**: PIO/CMake may return exit code 1 from miniz deprecation warnings on stderr. This is not an actual error — build succeeds.
- **Desktop CMake policy warning**: Use `-DCMAKE_POLICY_VERSION_MINIMUM:STRING=3.5` to silence.
- **COM4 port busy**: Kill any running monitor terminal before uploading firmware.
- **ohler.epub OOM**: 487 spine items, XHTML files up to 218KB, causes heap exhaustion on ESP32 during `parse_chapter()`. See "Memory constraints" section.
- **Heap fragmentation on ESP32**: After `Book::open()`, the largest contiguous block can be much smaller than total free heap (e.g. 59KB largest with 133KB total free). Large single allocations (>50KB) should be split into smaller ones.
- **SD SPI max 20 MHz**: The SD card on the prototype board fails at 40 MHz (`ESP_ERR_INVALID_RESPONSE` during `sdmmc_enable_hs_mode_and_check`). Keep `max_freq_khz = 20000`.
- **Boot speed**: `CONFIG_BOOTLOADER_SKIP_VALIDATE_IN_DEEP_SLEEP=y` skips the bootloader's SHA-256 image check (~470 ms for the ~2.9 MB app, mostly the DROM segment) on deep-sleep wake; cold power-on still verifies. Bootloader options only reach devices flashed over USB / web flasher — OTA and SD updates don't replace the bootloader. Image-check time scales with image size, so keep rodata lean: header-defined data arrays must be `inline constexpr`, not `static constexpr` (the UI fonts were duplicated per TU, ~220 KB).
- **Boot path (X3, book open, measured 2026-09-29)**: app start → NVS 40 ms → `epd.begin` 50 ms → SD mount 60 ms → data-dir check 18 ms (`ensure_dir()`: stat first, mkdir only when missing) → font 30 ms → reader open + first page render ~170 ms → plane upload 60 ms → X3 FULL refresh 480 ms. `epd.begin()` sends the X3 `POWER_ON` early (`x3_power_on_pending_`; `sendCommand()` completes it before the next command) so the panel's ~130 ms power-up overlaps SD/font/book loading. Deep-sleep wake also calls `esp_deep_sleep_disable_rom_logging()` (app-side, no bootloader needed) to drop the ROM banner on the next wake. Total: first page at ~1.5 s after a deep-sleep wake with a USB-flashed bootloader.
- **FATFS codepage**: fixed at 437 (`sdkconfig.defaults`). `CONFIG_FATFS_CODEPAGE_DYNAMIC` links ~480 KB of CJK tables; with LFN + UTF-8 API the codepage only affects legacy 8.3 short names, so long filenames in any language still work.
- **Directory listing**: use `dirent::d_type` (ESP-IDF's FAT `readdir()` fills it) instead of `stat()` per entry — on FAT each `stat()` searches the directory again. See `entry_type()` in `MainMenu.cpp`; `BookIndex` does the same.
- **Conversion memory (Bluetooth on)**: converting runs with only ~35–80 KB free heap, and exceptions are disabled, so any `bad_alloc` aborts (crash dumps: `CONFIG_ESP_COREDUMP_ENABLE_TO_FLASH`, partition `coredump`; decode addresses with `riscv32-esp-elf-addr2line -e .pio/build/esp32c3/firmware.elf`). Rules learned from two real crashes: never allocate a large block at the *end* of a conversion (heap is lowest there) — `MrbWriter::finish()` streams spine names via a callback; pre-size growing tables (`MrbWriter::reserve_chapters()`); stylesheets never touch the heap — see "CSS arena" above; on an X3, free the display driver's ~52 KB full-frame mirror buffer (`DrawBuffer::release_display_memory()`) before converting — `sync_bw_ram()`/any full-plane upload allocates it and keeps it. Convert All and the book index did; opening a single unconverted book (`ReaderScreen::start`) did not, which left 60 KB free before `Book::open` with Bluetooth on (Piketty, *Kapital und Ideologie*, 256 files: aborted in the TOC pool; now converts with ≥32 KB free, MRB byte-identical to the desktop) and also made the first page after a conversion abort in `make_image_size_query`/the JPEG decoder. `ZipReader::open()` reads the central directory through the caller's work buffer (whole, or in windows when larger — no heap block) and preflights the entry-table growth. The book index uses `Book::open_metadata()`: a name filter keeps only `container.xml` + the `.opf` in the ZIP table and the OPF is read only up to `</metadata>` (Morris: 239 → 2 entries), so indexing needs almost no heap and never skips a book for memory. `build_index()` also calls `release_display_memory()` like Convert All. `BodyParser` keeps a run capacity of `kDefaultRunReserve` (64) between paragraphs — it used to reserve 512 runs (~28 KB, taken from the largest free block minus 6 KB), which starved the text strings on large books (Piketty, *Kapital und Ideologie*: 750 ZIP entries, 250 KB chapter files) and aborted. Profiling a conversion on the Mac: link a small driver against `test/build2/libmicroreader_content.a` with a counting `operator new` that prints allocations above a threshold with `backtrace()`.
- **Stale PIO build after bootloader/RTC sdkconfig changes**: PIO doesn't regenerate `.pio/build/esp32c3/memory.ld` or rebuild the bootloader subproject when such options change (symptom: `region 'rtc_reserved_seg' overflowed`). Delete `.pio/build/esp32c3/memory.ld`, `.pio/build/esp32c3/bootloader/` and `bootloader.bin`, then rebuild.
- **Device MRB differs from the desktop's → check the SD card first**: the conversion is deterministic (same code and buffer sizes on desktop and device), so a device `.mrb` that differs from one converted on the Mac from the same EPUB means the device read different bytes. Seen 2026-09-30 on the X3's card: one 1 KB block (file sectors 4432–4433 of Diamond's *Kollaps*) returned dozens of flipped bits on most reads, at 20 and at 10 MHz, silently (no read error, the ZIP entry just decompresses to garbage from there on). Check: download the EPUB twice with the ACK-paced `CMND T` read and `cmp` against the original. Nothing verifies the ZIP entries' CRC32 during conversion, so such corruption ends up in the MRB.
- **Chapter CRC check during conversion**: `ZipEntry` carries the ZIP's `crc32` (`has_crc` is false for a local header with a data descriptor). CRC checking is **opt-in** on `ZipEntryInput` (`enable_crc_check()` after `open()`, before the first `read()`); by default `read()` computes no CRC. `Epub::parse_chapter_streaming()` first runs `ZipReader::verify_crc()` (enables it; decompress + `mz_crc32`, nothing kept) on the chapter *before* the stylesheets are loaded or any paragraph reaches the sink, retries once on a mismatch, and returns `EpubError::CrcMismatch` on the second (other zip failures -> `EpubError::ZipError`). The body parse also enables the check and calls `ZipEntryInput::finish()` (drains the rest and compares); a mismatch there is not retried, since output is already written. `convert_epub_to_mrb_streaming()` returns false and `conversion_read_error()` is true; `ReaderScreen` then shows "SD card read error" instead of "Failed to open book". Cost: each chapter is decompressed twice. Only chapter XHTML is checked — CSS files and images are not.
- **`new (std::nothrow)` must not abort**: exceptions are disabled, so the stock nothrow `new` ends in `__cxa_throw` → `abort()` when the heap can't satisfy it. `platforms/esp32/nothrow_new.cpp` overrides both nothrow operators with `malloc` so they return `nullptr` as the decoders and `make_image_size_query` expect. Without it, opening a book right after one that left the heap fragmented (largest block ~11 KB) aborted on the 33 KB work-buffer allocation (seen in `test_books.py` after End Times Fascism / Acemoglu).
- **Do NOT add `setvbuf` to `StdioZipFile`**: Adding a read buffer to the EPUB input `FILE*` causes wasteful 4 KB read-ahead after every seek, making SEEK 3× slower and DECOMP ~12% slower. The `ZipEntryInput` already manages its own efficient input buffering.

## Device testing

To verify changes on real hardware:
1. Upload test books via `python tools/serial_cmd.py --port COM4 --upload <path>.epub`
2. Build + flash: `& "$env:USERPROFILE\.platformio\penv\Scripts\pio.exe" run -t upload`
3. Monitor serial: `& "$env:USERPROFILE\.platformio\penv\Scripts\pio.exe" device monitor --baud 115200`
4. Use `python tools/serial_cmd.py --port COM4` for interactive control (see below).

### Serial command protocol (`tools/serial_cmd.py`)

Serial communication is handled by `serial_communication.h`. It runs a FreeRTOS `serial_receiver_task` that multiplexes three magic-prefixed protocols: LUT frames (`0xDEADBEEF`), EPUB uploads (`EPUB`), and control commands (`CMND`).

Device commands (sent via `serial_cmd.py` — see interactive commands below):

| Command | Description |
|---------|-------------|
| Button press | Inject button press by index |
| Open book | Push ReaderScreen for a book path |
| Status | Query heap info (`STATUS:free=N,largest=N`) |
| List books | List `.epub` files on SD card (`path|title|author|size|mtime`) |
| Download file | Download file from device (`T` + path) |
| Delete file | Delete file on device (`R` + path) |
| Clear cache | Delete all `.mrb` files (`CLEARED:N`) |
| Clear sleep | Delete all sleep images (`CLEARED_SLEEP:N`) |
| Clear SD fonts | Delete all SD fonts (`CLEARED_SDFONTS:N`) |
| Bench | Run EPUB conversion benchmark for a book |
| Image bench | Run image size-read benchmark for a book |
| Invalidate font | Zero font partition CRC, force re-provisioning |
| Render bench | Render benchmark on current page |
| Waveform bench | Time every X3 LUT set (`V`); X3-only, flashes the screen |
| Set model | Set device model (X3/X4) and reboot |
| Flash bench | Flash erase+write benchmark |

Interactive commands in `serial_cmd.py`:
```
> status        # heap info
> books         # list SD card books
> btn 1         # press button 1 (select)
> select        # alias for btn 1
> back          # alias for btn 0
> down          # alias for btn 2
> up            # alias for btn 3
> open alice.epub  # open book (auto-prepends /sdcard/books/)
> upload alice.epub  # upload EPUB file to the device
> rm /sdcard/books/alice.epub  # delete a file
> clear         # delete all .mrb files on the device
> clear-sleep   # delete all sleep images
> clear-fonts   # delete all SD fonts
> bench alice.epub   # run EPUB conversion benchmark
> imgsize alice.epub # run image size-read benchmark
> test [filter] [--clean] [-v]  # open each book and watch for BOOK_OK/BOOK_FAIL
> wfbench        # time every X3 waveform (LUT set), prints a summary table
```

### Waveform benchmark (X3)

`EInkDisplay::bench_waveforms()` (`platforms/esp32/epd.h`, X3-only) walks every
`X3LutSet` and measures the **real** panel busy time for a full-screen
white→black and black→white inversion, plus the SPI cost of one plane upload.
Full-screen inversions are used on purpose: they exercise the WB/BW LUT entries
(worst case, and the transition a ghost-clearing flash cycle depends on), whereas
an unchanged-pixel refresh only hits the slow WW/BB "top-up" entries.

Between measurements the panel is driven back to a settled white with the FULL
waveform so residue from the previous set cannot skew the next one.

```bash
python tools/serial_cmd.py --port COM4 --wfbench [--capture wf.log]
```

Output: one `WFBENCH:<name>|<frames>|<w2b>|<b2w>|<avg>|<spi>|<ms_per_frame>` line
per set, terminated by `=== WF BENCH DONE`, then a host-side summary table and an
estimated cost for a 3-pass flash cycle (black → white → page).

The bench clobbers both panel RAMs, so `main.cpp` calls
`buf.reset_after_scratch(true)` afterwards to force a clean repaint.

**Frame counts** (from the LUT TP/RP bytes — `A+B+C+D` frames per phase, `RP` =
repeats, X3 counts `RP=1` as one pass): FAST2 7, GRAY 7, ULTRAFAST 18, FAST 18,
TURBO 19, FULL 26, IMG 50. Note `FAST`/`FAST2`/`ULTRAFAST` are **defined but not
wired into any call path** — they exist only as tuning material. `ULTRAFAST` is
not actually faster than `FAST` (both 18 frames).

### Calibre Plugin (`tools/calibre-plugin/`)

Calibre device plugin for sending/deleting EPUBs directly from Calibre's library. Bundles `pyserial 3.5` because Calibre's embedded Python doesn't include it.

Install: **Calibre → Preferences → Plugins → Load plugin from file → `tools/calibre-plugin/microreader.zip`**

Build: `cd tools/calibre-plugin && python build.py`
Install + build: `python build.py --install`

The plugin communicates via the same `CMND` serial protocol. It uses:
- `CMND L` → list books (expects `path|title|author|size|mtime` format)
- `CMND R` + path → delete file
- `CMND T` + path → download file (device→Calibre)
- `EPUB` protocol → upload file (Calibre→device)

Non-interactive CLI flags:
```bash
python tools/serial_cmd.py --upload <path>.epub   # upload then exit
python tools/serial_cmd.py --bench ohler.epub [--capture bench.log] [--timeout 300]
python tools/serial_cmd.py --bench-all [--timeout 180]  # bench every .epub on device
python tools/serial_cmd.py --imgbench-all [--timeout 120]
```

Capture mode (non-interactive, saves all output to file):
```bash
python tools/serial_cmd.py --capture bench.log --reset --timeout 300
# --reset toggles DTR/RTS to reboot the device before capturing
# stops early when "=== DONE:" appears (override with --done-marker)
```

Useful heap logging pattern:
```cpp
#include "esp_heap_caps.h"
ESP_LOGI("test", "Free heap: %lu largest=%lu",
         (unsigned long)esp_get_free_heap_size(),
         (unsigned long)heap_caps_get_largest_free_block(MALLOC_CAP_8BIT));
```

## Git remotes & fork topology

This repo is **two fork-hops** downstream of the original. There are three repos in the chain:

```
CidVonHighwind/microreader     ← "the original"   (upstream)
        │ fork
pablohc/microreader_plus       ← "plus"          (the fork this repo is based on)
        │ fork
Technofrikus/microreader_plus  ← "this repo"     (origin)
```

### Configured remotes (all three point at the same local repo — do NOT clone separately)

- `origin` — [Technofrikus/microreader_plus](https://github.com/Technofrikus/microreader_plus) (your fork; **push here**)
- `pablohc` — [pablohc/microreader_plus](https://github.com/pablohc/microreader_plus) (the "plus" fork; has fixes not yet in the original)
- `upstream` — [CidVonHighwind/microreader](https://github.com/CidVonHighwind/microreader) (the original project)

> **Why one repo, not three clones:** all three are remotes in this single checkout. Cloning separately means three working trees you can't diff/merge across. Everything is done from here with `git fetch` + `cherry-pick`/`merge`.

### Divergence (measured 2026-07-28 — re-check with the commands below)

**Upstream (`CidVonHighwind/microreader`) is fully merged into this repo.** A prior sync (the "Browser Manager from Upstream" commits on `main`) already brought in the entire upstream history, including `upstream/rework` and all tags through `v2.1-dev.02`. As of this measurement:

| Comparison | Commits in other NOT in HEAD |
|---|---|
| `upstream/main` (original) | **0** |
| `upstream/rework` | **0** |
| tag `v2.1-dev.02` | **0** |
| `pablohc/main` (plus) | 0 |

So there is **nothing left to pull from upstream** — the older "37 commits behind" note is stale and has been removed.

**Where new features actually come from now:** this repo is forked from `pablohc/microreader_plus`, and `pablohc` carries its own feature branches on top of upstream. On 2026-07-28 these were merged into `main` via the `feat/sync-pablohc-features` branch:

- `pablohc/feat/battery-fix` — deep-sleep current reduction (unmount SD + deep-sleep display) + battery voltage logging
- `pablohc/fix/phantom-button-clear` — clear phantom button presses after loading-box transitions
- `pablohc/feat/upstream-sync` — web file-manager page, Calibre plugin, font-generator page, upload/sleep fixes

The **Calibre plugin** (`tools/calibre-plugin/`) was additionally synced directly from `upstream/main` because our fork's copy had diverged and its `download()` used the old non-ACK protocol, which hangs against the firmware's ACK-paced `T` (read) command. Upstream's plugin sends a `0x06` ACK per 2KB chunk, matching the firmware.

> **Pull from `pablohc` for the "plus" extras**, and pull from `upstream` only if a future upstream commit lands that we don't have (the divergence commands below will show it).

### Workflow for pulling features down

1. **Always work on a feature branch, never directly on `main`:**
   ```bash
   git fetch upstream pablohc
   git checkout -b feat/<name>
   ```
2. **Grab a specific feature (fastest, most surgical) — `cherry-pick`:**
   ```bash
   git cherry-pick <commit>            # single commit
   git cherry-pick <start>..<end>      # whole topic branch
   ```
   Use this when you only want specific features and want to avoid unrelated conflicts.
3. **Grab everything new from a remote — `merge`:**
   ```bash
   git merge pablohc/feat/<name>   # or upstream/main if it has new commits
   ```
   Expect conflicts in files both sides changed (e.g. `ReaderScreen`, display code, this `AGENTS.md`). Resolve, build, test.
4. **Validate before declaring success** (see [Tests](#tests) below):
   ```bash
   cd test && cmake -B build2 -DCMAKE_BUILD_TYPE=Debug -DCMAKE_POLICY_VERSION_MINIMUM:STRING=3.5
   cmake --build build2 --config Debug && ./build2/Debug/unit_tests
   ```

### Useful commands

```bash
# Re-measure divergence (should report 0 for upstream/main):
#   this vs original:   git rev-list --left-right --count HEAD...upstream/main
#   this vs plus:       git rev-list --left-right --count HEAD...pablohc/main
#   plus vs original:   git rev-list --left-right --count pablohc/main...upstream/main
# List commits in original NOT in this repo:
#   git log --oneline HEAD..upstream/main
# List commits in a pablohc branch NOT in this repo:
#   git log --oneline HEAD..pablohc/feat/<name>
# Sync only the calibre plugin from upstream:
#   git checkout upstream/main -- tools/calibre-plugin/
```

> **Gotcha:** the working tree often has untracked/staged `docs/` files (font-generator wasm etc.). Commit or `git stash` them before a `merge` so they don't tangle with incoming changes.


- **Keep this file up to date after every change.** When you add, rename, or restructure files, interfaces, systems, or tools — update the relevant section immediately. This file is the single source of truth for new chat sessions.
- **Record useful discoveries.** When you learn something non-obvious (hardware quirks, tricky build steps, constraints, or gotchas), add it to the relevant section so future sessions benefit.
- **Remove stale information.** When a known issue is fixed or a design is superseded, remove or update the entry rather than leaving outdated history.
