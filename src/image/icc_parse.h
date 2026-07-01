// SPDX-License-Identifier: MIT OR Apache-2.0
#ifndef JXL_OXIDE_ICC_PARSE_H_
#define JXL_OXIDE_ICC_PARSE_H_

#include <stddef.h>
#include "jxl_oxide/jxl_types.h"

/* True when Rust ColorEncodingWithProfile::with_icc would parse to a well-known RGB enum
 * (XYB→linear→display transfer) instead of a raw ICC→ICC CMS transform. */
int jxl_icc_maps_to_srgb_display(const uint8_t *icc, size_t len);
int jxl_icc_maps_to_linear_display(const uint8_t *icc, size_t len);

#include "jxl_oxide/jxl_status.h"

/* Rust icc::parse_icc — maps conformance ICC profiles to enum colour encodings. */
jxl_status_t jxl_icc_parse_color_encoding(const uint8_t *icc, size_t len, jxl_color_encoding *out);

#endif /* JXL_OXIDE_ICC_PARSE_H_ */
