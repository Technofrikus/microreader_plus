# <sup>micro</sup>reader<sup>+</sup>

Minimal EPUB reader for **Xteink X4 and X3**. **Not recommended for locked devices** — fork of [CidVonHighwind/microreader](https://github.com/CidVonHighwind/microreader)

## Fork Features

### Controls
- Simplified configuration for Menu and Reader with **4 control modes** (Default, Side Inverted, Front Inverted and Inverted).

### Content management
- **Delete book** with long-press back
- **Remove from Recents** with long-press back from Last Opened list

### Reader Options
- Antialiasing On/Off (grayscale rendering, supported on both X4 and X3)

### Support for X3
- **Auto-detect** X3 vs X4 at boot via I2C hardware fingerprint
- Grayscale sleep screen images
- Fast page rendering
- BQ27220 I2C battery gauge
- SPI at 10 MHz (X4 runs at 20 MHz)
- Automatic EPD cleanup before restart to prevent controller corruption

<img width="488" height="695" alt="Screenshot_2026-06-15-08-56-09-48_99c04817c0de5652397fc8b56c3b3817" src="https://github.com/user-attachments/assets/3b57a53e-4c3d-48b5-b600-4c290dddcf38" />


### Firmware Update via SD
based on https://github.com/crosspoint-reader/crosspoint-reader/pull/1786
- ESP32 image validation (magic 0xE9, segment walk, XOR, CRC32)
- Chip/revision guard + SHA256 + battery check
- OTA rollback via ESP_IMG_PENDING_VERIFY
- Force switch via otadata direct write as fallback

### UI
- **Custom Logo**

### System
- **Auto-migration** of settings (bool invert → ControlMode)
- **Button state cleanup** post-transition (prevents ghost presses)
  
---

## Features merged upstream

https://github.com/CidVonHighwind/microreader/pull/25
- Horizontal rule: centered at 1/3 width, 2px thickness
- CSS `width: XX%` parsing for `<hr>`

https://github.com/CidVonHighwind/microreader/pull/26
- Drop caps from `<span style="float: left; font-size: ...">`

https://github.com/CidVonHighwind/microreader/pull/30
- `--upload-dir` for batch EPUB upload via serial

https://github.com/CidVonHighwind/microreader/pull/33
- ReaderOptions: book title as header
- Toast feedback on actions

https://github.com/CidVonHighwind/microreader/pull/40
- Add case-insensitive alphabetical sort
- Add secondary alphabetical sort for unopened books in LastOpened mode
- Add visual separator between opened/unopened books
- Preserve last_open_order during index rebuild

---

## Original README

Minimal EPUB reader for the [Xteink X4](https://xteink.com) device (ESP32-C3 + SSD1677 e-ink display, 480×800 portrait).
Includes a desktop SDL2 emulator for development without hardware.

## Hardware

| | |
|---|---|
| MCU | ESP32-C3 (RISC-V, 160 MHz) |
| Display | 4.26" e-ink 800×480 (SSD1677), rotated → 480×800 portrait |
| Storage | SD card (FAT32, SPI) |
| Flash | 16 MB |
| Input | ADC buttons |

## Device Management

Books (`.epub`) can go anywhere on the SD card — the device scans recursively from the root. Fonts (`.mfb`) go in the `fonts/` folder on the SD card.

Simply copy files to the SD card while it is connected to your computer, then reinsert it into the device.

## Sleep Screen

The device displays an image when it enters deep sleep. Several images are built into the firmware. You can also add your own by placing BMP files in the `.sleep/` folder on the SD card.

Supported BMP variants: 1 bpp monochrome, 4 bpp indexed, 8 bpp indexed, 16 bpp RGB565 / BGR555, 24 bpp BGR, 32 bpp BGRA. Use 800×480 pixels for best quality (landscape) or 480×800 (portrait — automatically rotated 90° CCW). Other sizes are scaled to fit.

The first time an image is shown it is converted and cached; subsequent sleeps load the cache directly. The cache is cleared by **Settings → Clear Cache**.

### Adding sleep images

```powershell
python tools/serial_cmd.py --port COM4 --upload-sleep "path/to/my_image.bmp"
```

**Desktop emulator:** copy any `.bmp` file into `sd/.sleep/`.

### Selecting a sleep image

Open **Settings → Sleep Image**:

- **Auto** — cycles through all images in `.sleep/`, picking a different one each sleep.
- **\<filename\>** — pins the device to that specific image.

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

## Firmware Backup & Restore

```powershell
# Backup running firmware partition
python -m esptool --port COM4 read_flash 0x10000 0x650000 app0_backup.bin

# Restore
python -m esptool --port COM4 write_flash 0x10000 app0_backup.bin

# Switch OTA boot partition
python tools/switch_partition.py app0 --port COM4 --flash
python tools/switch_partition.py app1 --port COM4 --flash
```

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

## QEMU Testing (no hardware needed)

```powershell
# Terminal 1
python tools/run_qemu.py --with-books

# Terminal 2
python tools/test_books.py --port socket://localhost:4444 --pages 20 --delay 0.1
```
