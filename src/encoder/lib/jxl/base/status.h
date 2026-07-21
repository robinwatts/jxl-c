// Copyright (c) the JPEG XL Project Authors. All rights reserved.
//
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file.

#ifndef LIB_JXL_BASE_STATUS_H_
#define LIB_JXL_BASE_STATUS_H_

// Error handling: jxl_status return type + helper macros.

#include <stdarg.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

#include "lib/jxl/base/common.h"
#include "lib/jxl/base/compiler_specific.h"


// The Verbose level for the library
#ifndef JXL_DEBUG_V_LEVEL
#define JXL_DEBUG_V_LEVEL 0
#endif  // JXL_DEBUG_V_LEVEL

#ifdef USE_ANDROID_LOGGER
#include <android/log.h>
#define LIBJXL_ANDROID_LOG_TAG ("libjxl")
static inline void android_vprintf(const char* format, va_list args) {
  char* message = NULL;
  int res = vasprintf(&message, format, args);
  if (res != -1) {
    __android_log_write(ANDROID_LOG_DEBUG, LIBJXL_ANDROID_LOG_TAG, message);
    free(message);
  }
}
#endif

// Print a debug message on standard error or android logs. You should use the
// JXL_DEBUG macro instead of calling jxl_debug directly. This function returns
// false, so it can be used as a return value in JXL_FAILURE.
JXL_FORMAT(1, 2)
static JXL_NOINLINE bool jxl_debug(const char* format, ...) {
  va_list args;
  va_start(args, format);
#ifdef USE_ANDROID_LOGGER
  android_vprintf(format, args);
#else
  vfprintf(stderr, format, args);
#endif
  va_end(args);
  return false;
}

// Print a debug message on standard error if "enabled" is true. "enabled" is
// normally a macro that evaluates to 0 or 1 at compile time, so the jxl_debug
// function is never called and optimized out in release builds. Note that the
// arguments are compiled but not evaluated when enabled is false. The format
// string must be a explicit string in the call, for example:
//   JXL_DEBUG(JXL_DEBUG_MYMODULE, "my module message: %d", some_var);
// Add a header at the top of your module's .cc or .h file (depending on whether
// you have JXL_DEBUG calls from the .h as well) like this:
//   #ifndef JXL_DEBUG_MYMODULE
//   #define JXL_DEBUG_MYMODULE 0
//   #endif JXL_DEBUG_MYMODULE
#define JXL_DEBUG_TMP(format, ...) \
  jxl_debug(("%s:%d: " format "\n"), __FILE__, __LINE__, ##__VA_ARGS__)

#define JXL_DEBUG(enabled, format, ...)     \
  do {                                      \
    if (enabled) {                          \
      JXL_DEBUG_TMP(format, ##__VA_ARGS__); \
    }                                       \
  } while (0)

// JXL_DEBUG version that prints the debug message if the global verbose level
// defined at compile time by JXL_DEBUG_V_LEVEL is greater or equal than the
// passed level.
#if JXL_DEBUG_V_LEVEL > 0
#define JXL_DEBUG_V(level, format, ...) \
  JXL_DEBUG(level <= JXL_DEBUG_V_LEVEL, format, ##__VA_ARGS__)
#else
#define JXL_DEBUG_V(level, format, ...)
#endif

#define JXL_WARNING(format, ...) \
  JXL_DEBUG(JXL_IS_DEBUG_BUILD, format, ##__VA_ARGS__)

#if JXL_IS_DEBUG_BUILD
// Exits the program after printing a stack trace when possible.
JXL_NORETURN static JXL_NOINLINE bool jxl_abort() {
  JXL_PRINT_STACK_TRACE();
  JXL_CRASH();
}
#endif

#if JXL_IS_DEBUG_BUILD
#define JXL_DEBUG_ABORT(format, ...)                                   \
  do {                                                                 \
    if (JXL_DEBUG_ON_ABORT) {                                          \
      jxl_debug(("%s:%d: JXL_DEBUG_ABORT: " format "\n"), __FILE__, \
                   __LINE__, ##__VA_ARGS__);                           \
    }                                                                  \
    jxl_abort();                                                    \
  } while (0);
#else
#define JXL_DEBUG_ABORT(format, ...)
#endif

// Use this for code paths that are unreachable unless the code would change
// to make it reachable, in which case it will print a warning and abort in
// debug builds. In release builds no code is produced for this, so only use
// this if this path is really unreachable.
#if JXL_IS_DEBUG_BUILD
#define JXL_UNREACHABLE(format, ...)                                          \
  (jxl_debug(("%s:%d: JXL_UNREACHABLE: " format "\n"), __FILE__, __LINE__, \
                ##__VA_ARGS__),                                               \
   jxl_abort(), JXL_FAILURE(format, ##__VA_ARGS__))
#else  // JXL_IS_DEBUG_BUILD
#define JXL_UNREACHABLE(format, ...) \
  JXL_FAILURE("internal: " format, ##__VA_ARGS__)
#endif

// Only runs in debug builds (builds where NDEBUG is not
// defined). This is useful for slower asserts that we want to run more rarely
// than usual. These will run on asan, msan and other debug builds, but not in
// opt or release.
#if JXL_IS_DEBUG_BUILD
#define JXL_DASSERT(condition)                                      \
  do {                                                              \
    if (!(condition)) {                                             \
      JXL_DEBUG(JXL_DEBUG_ON_ABORT, "JXL_DASSERT: %s", #condition); \
      jxl_abort();                                               \
    }                                                               \
  } while (0)
#else
#define JXL_DASSERT(condition)
#endif

// A jxl_status value from a jxl_status_code or jxl_status which prints a debug message
// when enabled.
#define JXL_STATUS(status, format, ...)                                   \
  jxl_status_message(jxl_status_from_code(status), "%s:%d: " format "\n", \
                       __FILE__, __LINE__, ##__VA_ARGS__)

// Notify of an error but discard the resulting jxl_status value. This is only
// useful for debug builds or when building with JXL_CRASH_ON_ERROR.
#define JXL_NOTIFY_ERROR(format, ...)                                      \
  (void)JXL_STATUS(kGenericError, "JXL_ERROR: " format, \
                   ##__VA_ARGS__)

// An error jxl_status with a message. The JXL_STATUS() macro will return a jxl_status
// object with a kGenericError/kUnsupported/kNotEnoughBytes code, but the comma
// operator helps with clang-tidy inference and potentially with optimizations.
#define JXL_FAILURE(format, ...)                                              \
  ((void)JXL_STATUS(kGenericError, "JXL_FAILURE: " format, \
                    ##__VA_ARGS__),                                           \
   jxl_status_from_code(kGenericError))
#define JXL_UNSUPPORTED(format, ...)                            \
  ((void)JXL_STATUS(kUnsupported,            \
                    "JXL_UNSUPPORTED: " format, ##__VA_ARGS__), \
   jxl_status_from_code(kUnsupported))
#define JXL_NOT_ENOUGH_BYTES(format, ...)                            \
  ((void)JXL_STATUS(kNotEnoughBytes,              \
                    "JXL_NOT_ENOUGH_BYTES: " format, ##__VA_ARGS__), \
   jxl_status_from_code(kNotEnoughBytes))

// Always evaluates the status exactly once, so can be used for non-debug calls.
// Returns from the current context if the passed jxl_status expression is an error
// (fatal or non-fatal). The return value is the passed jxl_status.
#define JXL_RETURN_IF_ERROR(status)                                       \
  do {                                                                    \
    jxl_status jxl_return_if_error_status = (status);                  \
    if (!jxl_status_ok(jxl_return_if_error_status)) {                                \
      (void)jxl_status_message(                                         \
          jxl_return_if_error_status,                                     \
          "%s:%d: JXL_RETURN_IF_ERROR code=%d: %s\n", __FILE__, __LINE__, \
          (int)(jxl_status_get_code(jxl_return_if_error_status)), #status);  \
      return jxl_return_if_error_status;                                  \
    }                                                                     \
  } while (0)

// As above, but without calling jxl_status_message. Intended for bundles (see
// fields.h), which have numerous call sites (-> relevant for code size) and do
// not want to generate excessive messages when decoding partial headers.
#define JXL_QUIET_RETURN_IF_ERROR(status)                \
  do {                                                   \
    jxl_status jxl_return_if_error_status = (status); \
    if (!jxl_status_ok(jxl_return_if_error_status)) {               \
      return jxl_return_if_error_status;                 \
    }                                                    \
  } while (0)

#if JXL_IS_DEBUG_BUILD
// jxl_debug: fatal check.
#define JXL_ENSURE(condition)                     \
  do {                                            \
    if (!(condition)) {                           \
      jxl_debug("JXL_ENSURE: %s", #condition); \
      jxl_abort();                             \
    }                                             \
  } while (0)
#else
// Release: non-fatal check of condition. If false, just return an error.
#define JXL_ENSURE(condition)                           \
  do {                                                  \
    if (!(condition)) {                                 \
      return JXL_FAILURE("JXL_ENSURE: %s", #condition); \
    }                                                   \
  } while (0)
#endif

typedef enum jxl_status_code {
  // Non-fatal errors (negative values).
  kNotEnoughBytes = -1,

  // The only non-error status code.
  kOk = 0,

  // Fatal-errors (positive values)
  kGenericError = 1,
  kUnsupported = 2,
} jxl_status_code;

// jxl_status that raises compiler warnings if not used after being returned from a
// function. In case of error, the status can carry an extra error code which is
// split between fatal and non-fatal error codes. Use jxl_status_ok() instead of treating
// jxl_status as a bool. Aggregate-friendly: set code_ via jxl_status_from_code / helpers.
typedef struct jxl_status {
  jxl_status_code code_;
} jxl_status;

static inline bool jxl_status_ok(jxl_status s) { return s.code_ == kOk; }

static inline jxl_status_code jxl_status_get_code(jxl_status s) { return s.code_; }

static inline bool jxl_status_is_fatal_error(jxl_status s) {
  return (int32_t)(s.code_) > 0;
}

static inline jxl_status jxl_status_from_code(jxl_status_code code) {
  jxl_status s;
  s.code_ = code;
  return s;
}

static inline jxl_status jxl_ok_status() { return jxl_status_from_code(kOk); }

static inline jxl_status jxl_error_status() {
  return jxl_status_from_code(kGenericError);
}

/* Convert a bool success flag into jxl_status (true → Ok, false → generic error). */
static inline jxl_status jxl_status_from_bool(bool ok) {
  return ok ? jxl_ok_status() : jxl_error_status();
}

/* Helper to create a jxl_status and print the debug message or abort when needed. */
static inline JXL_FORMAT(2, 3) jxl_status jxl_status_message(jxl_status status,
                                                    const char* format, ...) {
  // This block will be optimized out when JXL_IS_DEBUG_BUILD is disabled.
  if ((JXL_IS_DEBUG_BUILD && jxl_status_is_fatal_error(status)) ||
      (JXL_DEBUG_ON_ALL_ERROR && !jxl_status_ok(status))) {
    va_list args;
    va_start(args, format);
#ifdef USE_ANDROID_LOGGER
    android_vprintf(format, args);
#else
    vfprintf(stderr, format, args);
#endif
    va_end(args);
  }
#if JXL_CRASH_ON_ERROR
  // JXL_CRASH_ON_ERROR means to jxl_abort() only on non-fatal errors.
  if (jxl_status_is_fatal_error(status)) {
    jxl_abort();
  }
#endif  // JXL_CRASH_ON_ERROR
  return status;
}


#endif  // LIB_JXL_BASE_STATUS_H_
