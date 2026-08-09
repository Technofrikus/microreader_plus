# Microreader+ (TF Edition) 2.1.0

## What's new

### Reader and settings

- Added paragraph counters for the status bar: current chapter/book paragraph and totals.
- Added configurable half-refresh behavior with `Never`, `Pages`, and `Always` modes.
- Improved ETA calculation with a more robust adaptive reading-speed estimate.
- Fixed the panel preparation required before grayscale sleep images when half-refresh is enabled.
- Added persistent settings support and aligned the sleep-image directory with the visible SD-card folder.

### Sleep images and display performance

- Added a fast 1-bit, two-plane sleep-image path for the ESP32.
- Added direct native-resolution uploads, byte-aligned copying, and a safe fallback for other layouts.
- Added BMP-to-native two-plane asset conversion during the asset build.
- Added native X3 and X4 default sleep images.
- Reduced sleep-image refresh overhead by removing the obsolete ghost-clear path.
- Increased the X3 SPI clock to 20 MHz and optimized X3 plane transfers.
- Removed the unused FAST2 waveform path.

### Boot, memory, and firmware

- Reduced embedded firmware assets substantially and deferred non-critical startup work.
- Removed redundant X4 display initialization before the first full refresh.
- Reduced startup font and sleep-image provisioning to the assets actually used.
- Improved X3 battery logging through the fuel gauge.
- Added heap diagnostics and expanded sleep-image conversion tests.

### Release and tooling

- Release builds now attach versioned firmware and Calibre plugin artifacts automatically.
- Added versioned release filenames and updated the release workflow for GitHub Releases.
- Extended `serial_cmd.py` with sleep-image upload and management support.

## Downloads

- **Microreader+(TF)-2.1.0.bin** — firmware for the Xteink X4 (ESP32-C3). Flash with the Crosspoint web flasher or:
  `python -m esptool --chip esp32c3 --port COM5 --baud 921600 write_flash 0x0 "Microreader+(TF)-2.1.0.bin"`
- **Microreader-Calibre-Plugin-2.1.0.zip** — Calibre plugin for sending and receiving books. Install via **Preferences → Plugins → Load plugin from file**.
