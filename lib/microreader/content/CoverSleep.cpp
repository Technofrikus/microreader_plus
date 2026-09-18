#include "CoverSleep.h"

#include <algorithm>
#include <cstdio>
#include <cstring>

#include "../HeapLog.h"
#include "../display/DrawBuffer.h"
#include "ImageDecoder.h"
#include "ZipReader.h"
#include "mrb/MrbFormat.h"

namespace microreader {

namespace {

// Cache header. The payload that follows is a raw framebuffer plane, so the
// geometry it was rendered for has to be recorded and checked: loading a
// mismatched plane would shear the image across the panel.
constexpr uint8_t kCoverMagic[4] = {'M', 'R', 'C', 'V'};
constexpr uint8_t kCoverVersion = 1;
constexpr size_t kCoverHeaderSize = 12;

std::string path_stem(const char* path) {
  const char* name = path;
  const char* sep = std::strrchr(name, '/');
  const char* bsep = std::strrchr(name, '\\');
  if (bsep && (!sep || bsep > sep))
    sep = bsep;
  if (sep)
    name = sep + 1;
  const char* dot = std::strrchr(name, '.');
  size_t len = dot ? static_cast<size_t>(dot - name) : std::strlen(name);
  return std::string(name, len);
}


// Read the source dimensions from the image header without decoding it, so the
// aspect-preserving target can be computed before the real decode starts.
bool probe_image_size(IZipFile& file, const ZipEntry& entry, uint8_t* work_buf, size_t work_size, uint16_t& w,
                      uint16_t& h) {
  ZipEntryInput inp;
  if (inp.open(file, entry, work_buf, work_size) != ZipError::Ok)
    return false;
  ImageSizeStream stream;
  uint8_t chunk[256];
  for (;;) {
    size_t n = inp.read(chunk, sizeof(chunk));
    if (n == 0)
      break;
    if (stream.feed(chunk, n))
      break;
  }
  if (!stream.ok())
    return false;
  w = stream.width();
  h = stream.height();
  return w != 0 && h != 0;
}

}  // namespace

// Unlike scaled_size() in ImageDecoder, a cover smaller than the screen is
// enlarged — showing a 300px thumbnail at native size in the middle of the
// panel would look like a bug rather than a sleep screen.
void cover_fit_size(uint16_t src_w, uint16_t src_h, int max_w, int max_h, uint16_t& out_w, uint16_t& out_h) {
  if (src_w == 0 || src_h == 0 || max_w <= 0 || max_h <= 0) {
    out_w = out_h = 0;
    return;
  }
  if (static_cast<uint32_t>(src_w) * static_cast<uint32_t>(max_h) >
      static_cast<uint32_t>(src_h) * static_cast<uint32_t>(max_w)) {
    out_w = static_cast<uint16_t>(max_w);
    out_h = static_cast<uint16_t>(std::max<uint32_t>(
        1, static_cast<uint32_t>(src_h) * static_cast<uint32_t>(max_w) / src_w));
  } else {
    out_h = static_cast<uint16_t>(max_h);
    out_w = static_cast<uint16_t>(std::max<uint32_t>(
        1, static_cast<uint32_t>(src_w) * static_cast<uint32_t>(max_h) / src_h));
  }
}

std::string book_cache_dir_for(const char* data_dir, const char* epub_path) {
  if (!data_dir || !epub_path)
    return {};
  return std::string(data_dir) + "/cache/" + path_stem(epub_path);
}

std::string cover_cache_path(const std::string& book_cache_dir, int panel_w, int panel_h) {
  char tail[32];
  std::snprintf(tail, sizeof(tail), "/cover_%dx%d.fb", panel_w, panel_h);
  return book_cache_dir + tail;
}

bool cover_offset_from_mrb(const char* mrb_path, uint32_t& out_offset) {
  std::FILE* f = std::fopen(mrb_path, "rb");
  if (!f)
    return false;
  MrbHeader hdr{};
  bool ok = std::fread(&hdr, 1, sizeof(hdr), f) == sizeof(hdr) && std::memcmp(hdr.magic, kMrbMagic, 4) == 0 &&
            hdr.version == kMrbVersion && hdr.image_count > 0;
  if (ok) {
    uint8_t entry[8];
    ok = std::fseek(f, static_cast<long>(hdr.image_offset), SEEK_SET) == 0 &&
         std::fread(entry, 1, sizeof(entry), f) == sizeof(entry);
    if (ok) {
      out_offset = mrb_read_u32(entry);
      ok = out_offset != 0;
    }
  }
  std::fclose(f);
  return ok;
}

bool build_cover_cache(const char* epub_path, uint32_t zip_local_offset, const char* cache_path, DrawBuffer& buf) {
  if (!epub_path || !cache_path)
    return false;

  StdioZipFile file;
  if (!file.open(epub_path)) {
    MR_LOGI("cover", "epub not readable: %s", epub_path);
    return false;
  }
  ZipEntry entry;
  if (ZipReader::read_local_entry(file, zip_local_offset, entry) != ZipError::Ok) {
    MR_LOGI("cover", "no zip entry at offset %lu", static_cast<unsigned long>(zip_local_offset));
    return false;
  }

  // One framebuffer is the decoder's work buffer, the other is the canvas.
  // scratch_buf2() is the inactive_() counterpart, matching how ReaderScreen
  // splits them for in-book images.
  uint8_t* work = buf.scratch_buf2();

  // The sleep screen is always portrait, whichever way the reader was held.
  // Only the transform is changed here — the panel's own rotation belongs to
  // the caller, which may be mid-conversion with nothing to show yet.
  const Rotation saved_rotation = buf.rotation();
  buf.set_rotation_transform(Rotation::Deg90);

  uint16_t src_w = 0, src_h = 0;
  bool ok = probe_image_size(file, entry, work, DrawBuffer::kBufSize, src_w, src_h);

  uint16_t out_w = 0, out_h = 0;
  if (ok) {
    cover_fit_size(src_w, src_h, buf.width(), buf.height(), out_w, out_h);
    ok = out_w > 0 && out_h > 0;
  }

  if (ok) {
    const int dest_x = (buf.width() - out_w) / 2;
    const int dest_y = (buf.height() - out_h) / 2;

    // White canvas: a cover whose aspect ratio doesn't match the panel keeps
    // its proportions and gets bars rather than losing its title to a crop.
    buf.fill(true);

    struct BlitCtx {
      DrawBuffer* buf;
      int x, y;
    };
    BlitCtx ctx{&buf, dest_x, dest_y};
    ImageRowSink sink;
    sink.ctx = &ctx;
    sink.emit_row = [](void* c, uint16_t row, const uint8_t* data, uint16_t width) {
      auto* bc = static_cast<BlitCtx*>(c);
      bc->buf->blit_1bit_row(bc->x, bc->y + static_cast<int>(row), data, width);
    };

    // Adam7-interlaced PNGs arrive as scattered pixels rather than rows. That
    // is fine here: the cache is a dump of the finished canvas, so the order
    // pixels land in doesn't matter.
    ImagePixelSink psink;
    psink.ctx = &ctx;
    psink.set_pixel = [](void* c, uint16_t px, uint16_t py, bool white) {
      auto* bc = static_cast<BlitCtx*>(c);
      bc->buf->set_pixel(bc->x + static_cast<int>(px), bc->y + static_cast<int>(py), white);
    };

    DecodedImage dims;  // only width/height are set; no pixel buffer is allocated
    ok = decode_image_from_entry(file, entry, out_w, out_h, dims, work, DrawBuffer::kBufSize,
                                 /*scale_to_fill=*/true, &sink, &psink) == ImageError::Ok;
    MR_LOGI("cover", "decode %ux%u -> %ux%u at (%d,%d) ok=%d", static_cast<unsigned>(src_w),
            static_cast<unsigned>(src_h), static_cast<unsigned>(out_w), static_cast<unsigned>(out_h), dest_x, dest_y,
            static_cast<int>(ok));
  }

  if (ok) {
    const DeviceConfig& cfg = buf.config();
    std::FILE* out = std::fopen(cache_path, "wb");
    if (out) {
      uint8_t hdr[kCoverHeaderSize] = {};
      std::memcpy(hdr, kCoverMagic, 4);
      hdr[4] = kCoverVersion;
      hdr[5] = 0;
      mrb_write_u16(hdr + 6, static_cast<uint16_t>(cfg.panel_width));
      mrb_write_u16(hdr + 8, static_cast<uint16_t>(cfg.physical_height));
      mrb_write_u16(hdr + 10, static_cast<uint16_t>(cfg.stride));
      ok = std::fwrite(hdr, 1, sizeof(hdr), out) == sizeof(hdr) &&
           std::fwrite(buf.render_buf(), 1, cfg.pixel_bytes, out) == cfg.pixel_bytes;
      std::fclose(out);
      if (!ok)
        std::remove(cache_path);
    } else {
      ok = false;
    }
    MR_LOGI("cover", "cache write ok=%d path=%s", static_cast<int>(ok), cache_path);
  }

  buf.set_rotation_transform(saved_rotation);
  return ok;
}

bool show_cover_sleep(const char* cache_path, DrawBuffer& buf) {
  if (!cache_path)
    return false;
  std::FILE* f = std::fopen(cache_path, "rb");
  if (!f)
    return false;

  const DeviceConfig& cfg = buf.config();
  uint8_t hdr[kCoverHeaderSize] = {};
  bool ok = std::fread(hdr, 1, sizeof(hdr), f) == sizeof(hdr) && std::memcmp(hdr, kCoverMagic, 4) == 0 &&
            hdr[4] == kCoverVersion && mrb_read_u16(hdr + 6) == static_cast<uint16_t>(cfg.panel_width) &&
            mrb_read_u16(hdr + 8) == static_cast<uint16_t>(cfg.physical_height) &&
            mrb_read_u16(hdr + 10) == static_cast<uint16_t>(cfg.stride);

  // One sequential read of the whole plane straight into the render buffer —
  // no decode, no allocation, no per-row seeking.
  if (ok)
    ok = std::fread(buf.render_buf(), 1, cfg.pixel_bytes, f) == cfg.pixel_bytes;
  std::fclose(f);
  if (!ok) {
    MR_LOGI("cover", "cache unusable: %s", cache_path);
    return false;
  }

  buf.full_refresh(RefreshMode::Full, /*turnOffScreen=*/true);
  return true;
}

}  // namespace microreader
