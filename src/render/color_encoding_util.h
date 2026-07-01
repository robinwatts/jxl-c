// SPDX-License-Identifier: MIT OR Apache-2.0
#ifndef JXL_RENDER_COLOR_ENCODING_UTIL_H_
#define JXL_RENDER_COLOR_ENCODING_UTIL_H_

#include "image/image_internal.h"
#include "jxl_oxide/jxl_types.h"

#include "jxl_oxide/jxl_status.h"

void jxl_color_encoding_default_srgb(jxl_color_encoding *out);

jxl_status_t jxl_colour_encoding_parsed_to_public(const jxl_colour_encoding_parsed *in,
                                                jxl_color_encoding *out);

/* True when XYB→enum fast path (D65 + sRGB primaries, no custom WP/primaries). */
int jxl_colour_encoding_is_d65_srgb_fast_path(const jxl_colour_encoding_parsed *enc);

/* Rust ColorEncodingWithProfile::is_equivalent for enum encodings (no ICC). */
int jxl_colour_encoding_parsed_equivalent(const jxl_colour_encoding_parsed *a,
                                        const jxl_colour_encoding_parsed *b);

jxl_status_t jxl_color_encoding_to_parsed(const jxl_color_encoding *in,
                                          jxl_colour_encoding_parsed *out);

#endif /* JXL_RENDER_COLOR_ENCODING_UTIL_H_ */
