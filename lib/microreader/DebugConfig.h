#pragma once

// Project-wide compile-time diagnostics switch.
//
// MR_ETA_DEBUG is the existing user-facing debug switch.  Keep the detailed
// persistent log tied to it by default so a release build can remove both the
// overlay and all diagnostic-log call sites with one setting.  The separate
// alias allows a developer to override only the log from the build flags when
// that is useful.
#ifndef MR_ETA_DEBUG
#define MR_ETA_DEBUG 1
#endif

#ifndef MR_DIAGNOSTIC_LOG
#define MR_DIAGNOSTIC_LOG MR_ETA_DEBUG
#endif
