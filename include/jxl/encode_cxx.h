// Copyright (c) the JPEG XL Project Authors. All rights reserved.
//
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file.

/// @addtogroup libjxl_cpp
///@{
///
/// @file encode_cxx.h
/// @brief C++ header-only helper for @ref encode.h.
///
/// There's no binary library associated with the header since this is a header
/// only library.

#ifndef JXL_ENCODE_CXX_H_
#define JXL_ENCODE_CXX_H_

#include <jxl/encode.h>

#include <memory>

#ifndef __cplusplus
#error "This a C++ only header. Use jxl/encode.h from C sources."
#endif

/// Struct to call jxl_encoder_destroy from the jxl_encoder_ptr unique_ptr.
struct jxl_encoder_destroy_struct {
  /// Calls @ref jxl_encoder_destroy() on the passed encoder.
  void operator()(jxl_encoder* encoder) { jxl_encoder_destroy(encoder); }
};

/// std::unique_ptr<> type that calls jxl_encoder_destroy() when releasing the
/// encoder.
///
/// Use this helper type from C++ sources to ensure the encoder is destroyed and
/// their internal resources released.
typedef std::unique_ptr<jxl_encoder, jxl_encoder_destroy_struct> jxl_encoder_ptr;

/// Creates an instance of jxl_encoder into a jxl_encoder_ptr and initializes it.
///
/// This function returns a unique_ptr that will call jxl_encoder_destroy() when
/// releasing the pointer. See @ref jxl_encoder_create for details on the
/// instance creation.
///
/// @param ctx library context (required; not owned).
/// @return a @c NULL jxl_encoder_ptr if the instance can not be allocated or
///         initialized
/// @return initialized jxl_encoder_ptr instance otherwise.
static inline jxl_encoder_ptr jxl_encoder_make(jxl_context* ctx) {
  return jxl_encoder_ptr(jxl_encoder_create(ctx));
}

#endif  // JXL_ENCODE_CXX_H_

/// @}
