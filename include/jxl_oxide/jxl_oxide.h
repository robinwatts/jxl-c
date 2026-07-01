// SPDX-License-Identifier: MIT OR Apache-2.0
#ifndef JXL_OXIDE_H_
#define JXL_OXIDE_H_

#include "jxl_context.h"
#include "jxl_status.h"
#include "jxl_types.h"

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    unsigned reserved; /* must be 0 for v1 */
} jxl_decoder_options;

/*
 * Decoder borrows ctx for allocation; ctx must outlive dec.
 * jxl_decoder_destroy and jxl_decoder_render require the same ctx passed to create.
 */
jxl_status_t jxl_decoder_create(jxl_context *ctx, const jxl_decoder_options *opts,
                                jxl_decoder **out);
void jxl_decoder_destroy(jxl_context *ctx, jxl_decoder *dec);

jxl_status_t jxl_decoder_feed(jxl_decoder *dec, const uint8_t *data, size_t len);
jxl_status_t jxl_decoder_try_init(jxl_decoder *dec);

const jxl_image_header *jxl_decoder_header(const jxl_decoder *dec);

/*
 * Returns the first Exif aux box, if any. Payload bytes are borrowed from dec
 * until jxl_decoder_destroy. When status is JXL_EXIF_DECODING, feed more input
 * and call again. Invalid Exif box layout returns JXL_ERROR_INVALID_INPUT.
 */
jxl_status_t jxl_decoder_first_exif(const jxl_decoder *dec, jxl_exif_metadata *out);

jxl_status_t jxl_decoder_request_icc(jxl_decoder *dec, const uint8_t *icc, size_t len);
jxl_status_t jxl_decoder_request_color_encoding(jxl_decoder *dec,
                                                jxl_color_encoding enc);

jxl_status_t jxl_decoder_set_crop(jxl_decoder *dec, const jxl_crop *crop);

/* Renders keyframe 0 (same as jxl_decoder_render_keyframe(dec, 0, …)). */
jxl_status_t jxl_decoder_render(jxl_context *ctx, jxl_decoder *dec, jxl_render **out);

/* Renders the given keyframe index (0 .. jxl_decoder_num_keyframes(dec)-1). */
jxl_status_t jxl_decoder_render_keyframe(jxl_context *ctx, jxl_decoder *dec,
                                         uint32_t keyframe_index, jxl_render **out);

uint32_t jxl_decoder_num_keyframes(const jxl_decoder *dec);

uint32_t    jxl_render_width(const jxl_render *r);
uint32_t    jxl_render_height(const jxl_render *r);
uint32_t    jxl_render_num_planes(const jxl_render *r);
uint32_t    jxl_render_keyframe_index(const jxl_render *r);
uint32_t    jxl_render_duration(const jxl_render *r);

typedef enum {
    JXL_RENDER_PLANE_F32 = 0,
    JXL_RENDER_PLANE_I16,
    JXL_RENDER_PLANE_I32,
} jxl_render_plane_kind;

jxl_render_plane_kind jxl_render_get_plane_kind(const jxl_render *r, uint32_t plane);

/*
 * Raw i16 modular samples for plane (no bit-depth normalization). NULL if plane is f32/i32.
 * width/height are the modular grid dimensions (may differ from jxl_render_width/height).
 */
const int16_t *jxl_render_plane_i16(const jxl_render *r, uint32_t plane, uint32_t *width,
                                    uint32_t *height);

/*
 * Normalized f32 samples. Materializes lazily when the plane is still integer storage.
 */
const float *jxl_render_plane(const jxl_render *r, uint32_t plane);
const uint8_t *jxl_render_icc(const jxl_render *r, size_t *len);
void jxl_render_destroy(jxl_context *ctx, jxl_render *r);

const char *jxl_decoder_last_error(const jxl_decoder *dec);

#if defined(JXL_OXIDE_C_ENABLE_JBR) && JXL_OXIDE_C_ENABLE_JBR
typedef enum {
    JXL_JPEG_RECONSTRUCTION_UNAVAILABLE = 0,
    JXL_JPEG_RECONSTRUCTION_DECODING,
    JXL_JPEG_RECONSTRUCTION_NEED_MORE_DATA,
    JXL_JPEG_RECONSTRUCTION_AVAILABLE,
    JXL_JPEG_RECONSTRUCTION_INVALID,
} jxl_jpeg_reconstruction_status;

jxl_jpeg_reconstruction_status jxl_decoder_jpeg_reconstruction_status(const jxl_decoder *dec);

/*
 * Reconstructs the original JPEG bitstream when jbrd data is available.
 * On success, *jpeg_out receives an allocated buffer (free with jxl_context allocator).
 */
jxl_status_t jxl_decoder_reconstruct_jpeg(jxl_decoder *dec, uint8_t **jpeg_out, size_t *jpeg_len);
#endif

#ifdef __cplusplus
}
#endif

#endif /* JXL_OXIDE_H_ */
