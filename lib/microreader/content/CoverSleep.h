#pragma once

// Book cover as the sleep screen.
//
// The expensive part — inflating, decoding, scaling and dithering a cover —
// is done exactly once per book and stored as a screen-ready framebuffer dump
// (a "cover cache"). Every later sleep is a single sequential read of that
// file straight into the display buffer, with no decode and no allocation.
//
// The cache is built at the end of a book's first EPUB→MRB conversion, where
// a progress bar is already running and both display buffers are free. Books
// converted before this feature existed have no cache, so the same builder is
// also callable at sleep time as a fallback.
//
// Neither the builder nor the loader allocates: the render target is one of
// the DrawBuffer's two internal framebuffers and the decoder's ~33KB work
// buffer is the other.

#include <cstdint>
#include <string>

namespace microreader {

class DrawBuffer;

// Value stored in Application::sleep_image_path() to select "cover of the book
// currently being read". Shares the "<scheme>:" shape of the other sentinels
// ("embedded:N", "bmp:<path>") but takes no argument — the book is whichever
// one the reader is on, or the last one opened.
inline constexpr const char* kCoverSleepPath = "cover:";

// Per-book cache directory: <data_dir>/cache/<epub stem>.
// Must agree with ReaderScreen's own cache dir or the cover would be written
// somewhere the reader never cleans up.
std::string book_cache_dir_for(const char* data_dir, const char* epub_path);

// Cover cache file inside a per-book cache directory. The panel geometry is
// part of the name: a cache rendered for one model is meaningless on another,
// and the device model can be switched at runtime.
std::string cover_cache_path(const std::string& book_cache_dir, int panel_w, int panel_h);

// Scale src_w × src_h to fit inside max_w × max_h with the aspect ratio intact,
// enlarging as well as shrinking. The cover is never cropped: when its ratio
// doesn't match the panel's, the leftover space becomes white bars.
void cover_fit_size(uint16_t src_w, uint16_t src_h, int max_w, int max_h, uint16_t& out_w, uint16_t& out_h);

// ZIP local-header offset of a book's cover image, read from its MRB cache.
// Image references are recorded in document order during conversion, so entry
// 0 is the first image in the book — the cover for virtually every EPUB.
// Returns false when the MRB is missing or holds no images.
bool cover_offset_from_mrb(const char* mrb_path, uint32_t& out_offset);

// Decode the image at `zip_local_offset` in `epub_path` and render it into
// `buf`, scaled to fit the screen with its aspect ratio intact (never cropped
// — a cover that doesn't match the panel ratio gets white bars) and centred.
// The result is written to `cache_path`.
//
// CLOBBERS BOTH display buffers and leaves nothing on the panel; callers must
// repaint. The rotation transform is saved and restored, and the panel's own
// rotation is never touched.
bool build_cover_cache(const char* epub_path, uint32_t zip_local_offset, const char* cache_path, DrawBuffer& buf);

// Load a cache written by build_cover_cache() and put it on the panel as the
// sleep screen, leaving the display powered down. The caller must already have
// set the panel rotation it wants. Returns false when the cache is missing or
// was rendered for different panel geometry.
bool show_cover_sleep(const char* cache_path, DrawBuffer& buf);

}  // namespace microreader
