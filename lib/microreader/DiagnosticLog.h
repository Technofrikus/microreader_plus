#pragma once

#include "DebugConfig.h"

namespace microreader::diag {

#if MR_DIAGNOSTIC_LOG

// Start a persistent rolling diagnostic log below data_dir.  Safe to call
// repeatedly; events before a successful init are ignored.
void init(const char* data_dir);

// Append one durable event.  The implementation uses fixed-size stack buffers
// and closes the file after every record so the last completed event survives
// a later hang or reset.
void event(const char* tag, const char* fmt, ...);

#endif

}  // namespace microreader::diag

#if MR_DIAGNOSTIC_LOG
#define MR_DIAG_INIT(data_dir) ::microreader::diag::init(data_dir)
#define MR_DIAG(tag, fmt, ...) ::microreader::diag::event(tag, fmt, ##__VA_ARGS__)
#else
#define MR_DIAG_INIT(data_dir) ((void)0)
#define MR_DIAG(tag, fmt, ...) ((void)0)
#endif
