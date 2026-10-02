// operator new(std::nothrow) that really returns nullptr on failure.
//
// Exceptions are disabled, so the standard nothrow new (which calls the
// throwing new inside a try/catch) ends in __cxa_throw -> abort() when the heap
// cannot satisfy the request. The decoders and the image-size query allocate
// 33 KB work buffers with `new (std::nothrow)` and handle nullptr, which only
// works if failure is reported instead of fatal. After a book left the heap
// fragmented (largest block 11 KB) opening the next one aborted in
// make_image_size_query.
#include <cstdlib>
#include <new>

void* operator new(std::size_t size, const std::nothrow_t&) noexcept {
  return std::malloc(size ? size : 1);
}

void* operator new[](std::size_t size, const std::nothrow_t&) noexcept {
  return std::malloc(size ? size : 1);
}
