/* Copyright (c) the JPEG XL Project Authors. All rights reserved.
 *
 * Use of this source code is governed by a BSD-style
 * license that can be found in LICENSE-BSD.
 */

/** @addtogroup jxl_encoder
 * @{
 * @file encode.h
 * @brief JPEG-to-JXL encoder API (jxl_encoder_add_jpeg_frame).
 */

#ifndef JXL_ENCODE_H_
#define JXL_ENCODE_H_

#include <jxl/context.h>
#include <jxl/jxl_export.h>
#include <jxl/status.h>
#include <jxl/types.h>
#include <jxl/version.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Encoder library version.
 *
 * @return the encoder library version as an integer:
 * MAJOR_VERSION * 1000000 + MINOR_VERSION * 1000 + PATCH_VERSION. For example,
 * version 1.2.3 would return 1002003.
 */
JXL_C_EXPORT uint32_t jxl_encoder_version(void);

typedef struct jxl_encoder jxl_encoder;
typedef struct jxl_encoder_frame_settings jxl_encoder_frame_settings;

typedef enum {
  /** Encoding speed: 1 (fastest) .. 10 (slowest). Default: 7. */
  JXL_ENCODER_FRAME_SETTING_EFFORT = 0,

  /** Enable CFL for lossless JPEG recompression. -1 = default, 0 = off, 1 = on.
   */
  JXL_ENCODER_FRAME_SETTING_JPEG_RECON_CFL = 30,

  /** Brotli effort for JPEG recompression and brob boxes. -1 or 0..11. */
  JXL_ENCODER_FRAME_SETTING_BROTLI_EFFORT = 32,

  /** Brotli-compress metadata boxes from JPEG frames. -1 = default, 0/1. */
  JXL_ENCODER_FRAME_SETTING_JPEG_COMPRESS_BOXES = 33,

  /** Keep Exif from JPEG input. -1 = default (keep), 0 = discard, 1 = keep.
   */
  JXL_ENCODER_FRAME_SETTING_JPEG_KEEP_EXIF = 35,

  /** Keep XMP from JPEG input. -1 = default (keep), 0 = discard, 1 = keep. */
  JXL_ENCODER_FRAME_SETTING_JPEG_KEEP_XMP = 36,

  JXL_ENCODER_FRAME_SETTING_FILL_ENUM = 65535,
} jxl_encoder_frame_setting_id;

JXL_C_EXPORT jxl_encoder* jxl_encoder_create(jxl_context* ctx);
JXL_C_EXPORT void jxl_encoder_destroy(jxl_encoder* enc);

/** Last error set by an encoder call that returned a JXL_ERROR_* status. */
JXL_C_EXPORT jxl_status_t jxl_encoder_get_error(jxl_encoder* enc);

/**
 * Encodes output bytes. Call repeatedly until status is not
 * ::JXL_NEED_MORE_OUTPUT. Requires *avail_out >= 32 on each call.
 */
JXL_C_EXPORT jxl_status_t jxl_encoder_process_output(jxl_encoder* enc,
                                                    uint8_t** next_out,
                                                    size_t* avail_out);

/**
 * Adds a JPEG frame for lossless recompression to JPEG XL.
 *
 * Implicitly sets basic info and color encoding from the JPEG. Call
 * @ref jxl_encoder_close_input before the final @ref jxl_encoder_process_output
 * when this is the last frame.
 */
JXL_C_EXPORT jxl_status_t
jxl_encoder_add_jpeg_frame(const jxl_encoder_frame_settings* frame_settings,
                       const uint8_t* buffer, size_t size);

JXL_C_EXPORT void jxl_encoder_close_input(jxl_encoder* enc);

JXL_C_EXPORT jxl_status_t jxl_encoder_use_container(jxl_encoder* enc,
                                                   JXL_BOOL use_container);

JXL_C_EXPORT jxl_status_t
jxl_encoder_store_jpeg_metadata(jxl_encoder* enc, JXL_BOOL store_jpeg_metadata);

JXL_C_EXPORT jxl_status_t jxl_encoder_frame_settings_set_option(
    jxl_encoder_frame_settings* frame_settings, jxl_encoder_frame_setting_id option,
    int64_t value);

JXL_C_EXPORT jxl_encoder_frame_settings* jxl_encoder_frame_settings_create(
    jxl_encoder* enc, const jxl_encoder_frame_settings* source);

#ifdef __cplusplus
}
#endif

#endif /* JXL_ENCODE_H_ */

/** @}*/
