# microreader+ (TF Edition)

Minimal EPUB reader tuned for speed for **Xteink X4 and X3**. **Not recommended for locked devices** — fork of [CidVonHighwind/microreader](https://github.com/CidVonHighwind/microreader) and Microreader+

## Changes vs upstream (microreader)

| # | Change | Commit / PR |
|---|--------|-------------|
| 24 | Replaced the sleep-image cycle/back controls with a dedicated list screen supporting live full-screen preview | [b19b5c4](https://github.com/Technofrikus/microreader_plus/commit/b19b5c4) |
| 23 | Improved sleep-image downscaling quality with box filtering instead of nearest-neighbor, keeping small cover text legible | [87e1a9d](https://github.com/Technofrikus/microreader_plus/commit/87e1a9d) |
| 22 | Added a **Book Cover** sleep-image mode that shows the current book's cover as the sleep screen, built once per book and cached for instant loads | [a98140f](https://github.com/Technofrikus/microreader_plus/commit/a98140f) |
| 21 | Show the current folder name in the "up one level" row while browsing library folders | [b8cea9d](https://github.com/Technofrikus/microreader_plus/commit/b8cea9d) |
| 20 | Sped up EPUB conversion, font provisioning, sleep-image conversion, and book indexing with model-aware progress refresh cadence (X3 25% / X4 5%) | [8126ac6](https://github.com/Technofrikus/microreader_plus/commit/8126ac6), [4cda910](https://github.com/Technofrikus/microreader_plus/commit/4cda910) |
| 19 | Added a bundled light-themed default sleep image for X3 and X4 | [83051e0](https://github.com/Technofrikus/microreader_plus/commit/83051e0) |
| 18 | Added an **Estimated Time Read** option for a configurable reader status-bar slot | — |
| 17 | Improved EPUB line breaking so punctuation-only runs remain with the preceding word | [bfdef4f](https://github.com/Technofrikus/microreader_plus/commit/bfdef4f) |
| 16 | Added optional persistent SD-card diagnostic ring logs for boot, reader, input, display, and shutdown events | [513c284](https://github.com/Technofrikus/microreader_plus/commit/513c284) |
| 15 | Refined book and chapter ETA estimation with separate short- and long-term reading pace, persistent calibration, and responsive estimates near completion | [9b3bc9b](https://github.com/Technofrikus/microreader_plus/commit/9b3bc9b) |
| 14 | Added versioned GitHub Releases for firmware and the Calibre plugin | — |
| 13 | Added X3 fuel-gauge voltage logging and improved startup diagnostics | — |
| 12 | Added model-native X3/X4 sleep assets and optimized X3 transfers, reducing sleep and boot overhead | — |
| 11 | Added a fast native 1-bit/two-plane sleep-image path with direct and byte-aligned uploads | — |
| 9 | Added a configurable three-slot status bar with chapter/book progress, ETA, battery, and paragraph counters | — |
| 8 | Rotate the screen (portrait ↔ landscape) by long-pressing the Select button while reading | — |
| 7 | Added long-hold select to cycle text selection settings backward | [ba0c7e6](https://github.com/CidVonHighwind/microreader-plus/commit/ba0c7e6) |
| 6 | Added folder-based book organization with long-press UP to navigate folders | [b9cdbcc](https://github.com/CidVonHighwind/microreader-plus/commit/b9cdbcc) |
| 5 | Added **ETA estimation in the reader screen** (chapter and book), including adaptive reading-speed estimation | [2505b52](https://github.com/CidVonHighwind/microreader-plus/commit/2505b52) |
| 4 | Added device-native resolution with cover-fit (scale + crop) for sleep BMPs | [6590639](https://github.com/CidVonHighwind/microreader-plus/commit/6590639) |
| 3 | Added 'Rebuild Sleep Images' menu entry with progress bar and cancel (sleep images are converted all at once, making shutdown faster) | [c180d13](https://github.com/CidVonHighwind/microreader-plus/commit/c180d13) |
| 2 | Added 2bpp bitmap support (for the awesome images from the lector wallpaper gallery) | [7129706](https://github.com/CidVonHighwind/microreader-plus/commit/7129706) |
| 1 | Removed the "sleeping..." text drawn on the sleep screen in `DrawBuffer::show_mgr2_sleep_()` | — |

## Fork Features

### Special Button Presses
- **Long-press UP (in MainMenu):** Navigate up one folder level.
- **Long-press BACK (in MainMenu):** Delete the selected book.
- **Long-press BACK (in Last Opened list):** Remove the selected book from the "Last Opened" list.
- **Long-press SELECT (in Reader):** Rotate the screen between portrait and landscape (hold ~0.5 s). A short tap instead opens the reader options menu.
- **Long-hold SELECT (in Reader):** Cycle through text selection settings in reverse (backward).

### Content management
- **Delete book** with long-press back
- **Remove from Recents** with long-press back from Last Opened list

### Reader Options
- Antialiasing On/Off
- Configurable status-bar slots for chapter/book progress, ETA, battery, and paragraph counts

### Support for X3
- **Auto-detect** X3 vs X4 at boot via I2C hardware fingerprint
- Grayscale sleep screen images
- Fast page rendering
- BQ27220 I2C battery gauge
- Native 1-bit/two-plane sleep-image uploads with optimized X3 transfers
- SPI at 20 MHz on X3 and X4
- Automatic EPD cleanup before restart to prevent controller corruption

<img width="488" height="695" alt="Screenshot_2026-06-15-08-56-09-48_99c04817c0de5652397fc8b56c3b3817" src="https://github.com/user-attachments/assets/3b57a53e-4c3d-48b5-b600-4c290dddcf38" />


### Firmware Update
Just use the providede .bin file with the Crosspoint Web Falsher https://crosspointreader.com/ and choose custom firmware there.


---


## Hardware

Works on X3 and X4 by XTEink

## Device Management

Books (`.epub`) should go in the books folder.

Fonts (`.mfb`) go in the `fonts/` folder on the SD card.

Simply copy files to the SD card while it is connected to your computer, then reinsert it into the device.

## Sleep Screen

The device displays an image when it enters deep sleep. One image is built into the firmware. You can also add your own by placing BMP files in the `sleep/` folder on the SD card.

Supported BMP variants: 1 bpp monochrome, 2bpp indexed, 4 bpp indexed, 8 bpp indexed, 16 bpp RGB565 / BGR555, 24 bpp BGR, 32 bpp BGRA.

Recommended Source for nice pictures: https://diogo7dias.github.io/lector-xteink-firmware/#flash

The first time an image is shown it is converted and cached; subsequent sleeps load the cache directly. The cache is cleared by **Settings → Clear Cache**.
You can *convert all sleep images* in the menu so shutting down is faster every time.

### Book cover

**Settings → Sleep Image → Book Cover** shows the cover of the book you are reading (or the last one you opened) instead of a fixed image. The cover keeps its proportions and is never cropped, so a cover whose shape doesn't match the screen gets white bars rather than losing its title.

Each book's cover is prepared once — at the end of its first conversion when this mode is already selected, otherwise on the first sleep after you switch to it — and stored alongside that book's other cache files, so every later sleep just loads it. Books with no usable cover fall back to the previously selected sleep image.

### Adding sleep images

```powershell
python tools/serial_cmd.py --port COM4 --upload-sleep "path/to/my_image.bmp"
```

**Desktop emulator:** copy any `.bmp` file into `sd/sleep/`.

### Selecting a sleep image

**Settings → Sleep Image** opens a list of everything available:

- **Auto (cycle)** — a different image from `sleep/` each time the device sleeps.
- **Book Cover** — the cover of the book you are reading (see above).
- **\<filename\>** — pins the device to that one image.

The image in use is marked with a bullet and the cursor starts on it, so you never have to scroll back to where you already were. **Hold select** on any entry to see it full-screen exactly as a sleep would show it; any button returns to the list.

Images are listed alphabetically, ignoring case — the same order **Auto** cycles through.

## Calibre Plugin

Send and delete EPUB books directly from Calibre's library.

### Install

1. In Calibre: **Preferences → Plugins → Load plugin from file**
2. Select `tools/calibre-plugin/microreader.zip`
3. Restart Calibre
4. Connect the device via USB

The device is detected automatically. Books on the device show checkmarks; you can send and delete from the Device menu.

Requires Calibre 5+ and the device connected over USB.

### Build

#### Tips for merging upstream changes

This fork tracks the upstream [microreader](https://github.com/CidVonHighwind/microreader) repo. To keep merges easy:

- **Keep changes localized.** Each fork feature lives in its own files or clearly marked sections — avoid editing shared `lib/microreader/` code unless the change is intended for both.
- **Use descriptive commits.** Each commit should describe a single logical change so `git log --oneline upstream/...` is readable.
- **Frequent rebases onto upstream.** Run `git fetch upstream && git rebase upstream/main` regularly; resolve conflicts while the change is still fresh in your mind.
- **Prefer additions over modifications.** When adding a feature, add new files/functions rather than editing existing shared code. This makes `git diff upstream/...` clean and conflict-free.
- **Track what's merged upstream.** The "Features merged upstream" section below this list is manually kept up to date — update it whenever an upstream PR is accepted so you know what to drop or rebase.

```powershell
cd tools/calibre-plugin
python build.py             # packages __init__.py + serial/ into microreader.zip
python build.py --install   # build and copy into Calibre's plugins folder
```

The plugin bundles `pyserial` because Calibre's embedded Python doesn't include it.

### Debug

```powershell
& tools/calibre-plugin/launch-debug.ps1   # Windows only: calibre-debug -g
```

All `print()` calls in `__init__.py` appear in the terminal, prefixed with `[Microreader]`.

---

## Project Structure

```
lib/microreader/       shared core (platform-agnostic C++20)
  content/             EPUB parsing, layout, MRB binary format
  display/             Canvas, DisplayQueue, Font interfaces
  screens/             UI screen implementations
platforms/desktop/     SDL2 emulator
platforms/esp32/       ESP-IDF + PlatformIO firmware
test/                  Google Test suite
tools/                 Python scripts
resources/             Fonts, sleep images
```

## Building

### Desktop (emulator)

```powershell
cmake -S platforms/desktop -B build/desktop-debug -DCMAKE_BUILD_TYPE=Debug "-DCMAKE_POLICY_VERSION_MINIMUM:STRING=3.5"
cmake --build build/desktop-debug --config Debug
.\build\desktop-debug\Debug\microreader_desktop.exe
```

### ESP32 (PlatformIO)

```powershell
# Build + flash
$env:USERPROFILE\.platformio\penv\Scripts\pio.exe run -t upload

# Serial monitor
$env:USERPROFILE\.platformio\penv\Scripts\pio.exe device monitor --baud 115200
```

COM4, upload baud 921600.

### Tests

```powershell
cd test
cmake -B build2 -DCMAKE_BUILD_TYPE=Debug -DCMAKE_POLICY_VERSION_MINIMUM:STRING=3.5
cmake --build build2 --config Debug

.\build2\Debug\unit_tests.exe          # fast (~375 tests, <1s)
.\build2\Debug\microreader_tests.exe   # includes real EPUB integration tests
```

## Font Generation

Reader fonts are FNTS bundles (`.mfb`), generated from TTF/OTF sources via `tools/generate_font.py`.

Two kinds:
- **Built-in** (`resources/fonts/`) — embedded in the firmware asset blob. Require a firmware rebuild to update.
- **SD card** (`resources/sd fonts/`) — loaded from `/sdcard/fonts/` at runtime. No firmware rebuild needed; just copy or upload.

The generation command is the same for both:

```powershell
python tools/generate_font.py "resources/sd fonts/ttf/Cartisse-Regular.ttf" `
  -o "resources/sd fonts/Cartisse.mfb" --with-styles `
  --bold "resources/sd fonts/ttf/Cartisse-Bold.ttf" `
  --italic "resources/sd fonts/ttf/Cartisse-Italic.ttf" `
  --bold-italic "resources/sd fonts/ttf/Cartisse-BoldItalic.ttf" `
  --bundle --bundle-sizes 20 22 24 26 28 30 32 --font-name Cartisse

# Regenerate all SD fonts
$ttf = "resources/sd fonts/ttf"; $out = "resources/sd fonts"
foreach ($f in @("Bitter","Cartisse","NV_Bitter","NV_Charis","NV_Cooper","NV_Garamond","NV_Jost","NV_Palatium","Readerly")) {
    python tools/generate_font.py "$ttf/$f-Regular.ttf" -o "$out/$f.mfb" --with-styles `
      --bold "$ttf/$f-Bold.ttf" --italic "$ttf/$f-Italic.ttf" --bold-italic "$ttf/$f-BoldItalic.ttf" `
      --bundle --bundle-sizes 20 22 24 26 28 30 32 --font-name $f
}

# Font preview (generates tools/font_overview.html)
python tools/font_overview.py

# UI fonts (bitmap, bw-only)
python tools/generate_font.py resources/fonts/terminus/Terminus-Bold.ttf 14 --header lib/microreader/display/ui_font_small.h --bw-only --ranges ui
python tools/generate_font.py resources/fonts/terminus/Terminus-Bold.ttf 18 --header lib/microreader/display/ui_font_medium.h --bw-only --ranges ui
python tools/generate_font.py resources/fonts/terminus/Terminus-Bold.ttf 24 --header lib/microreader/display/ui_font_large.h --bw-only --ranges ui

python tools/generate_font.py resources/fonts/terminus/Terminus-Bold.ttf 32 --header lib/microreader/display/ui_font_header.h --bw-only --ranges ui
```

> **Font partition limit**: SD card fonts must fit within 3.375 MB. The font data + 4 KB header must not exceed `0x360000` bytes.


## Hyphenation

The reader uses the [Liang hyphenation algorithm](https://tug.org/docs/liang/) — the same algorithm used by TeX. Language-specific TeX pattern files are compiled into compact binary tries by [Typst hypher](https://github.com/typst/hypher) and embedded as `constexpr` byte arrays. The language is detected automatically from the EPUB's `xml:lang` attribute.

**Supported languages:**

| Code | Language    | Trie size |
|------|-------------|-----------|
| `en` | English     | 26 KB     |
| `de` | German      | 206 KB    |
| `fr` | French      | 7 KB      |
| `es` | Spanish     | 14 KB     |
| `it` | Italian     | 2 KB      |
| `nl` | Dutch       | 64 KB     |
| `pt` | Portuguese  | 1 KB      |
| `pl` | Polish      | 16 KB     |
| `ru` | Russian     | 33 KB     |

Trie data lives in `lib/microreader/content/hyphenation/Liang/hyph-<lang>.trie.h` as `constexpr` byte arrays — no heap allocation, no flash reads at runtime (data is placed in DROM on ESP32).

To add a new language:
1. Download the `.bin` from [typst/hypher/tries](https://github.com/typst/hypher/tree/main/tries) into `tools/hyphenation/`
2. Generate the header: `python tools/generate_trie_header.py tools/hyphenation/<lang>.bin lib/microreader/content/hyphenation/Liang/hyph-<lang>.trie.h <lang>`
3. Add the new enum value to `HyphenationLang` in `Hyphenation.h`
4. Add a `#include` + `case` in `Hyphenation.cpp` (`hyphenate_word`) and an `ieq` check in `detect_language`
