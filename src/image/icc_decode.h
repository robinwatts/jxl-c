// SPDX-License-Identifier: MIT OR Apache-2.0
#ifndef JXL_IMAGE_ICC_DECODE_H_
#define JXL_IMAGE_ICC_DECODE_H_

#include "allocator.h"
#include "bitstream/bitstream.h"

jxl_bs_status_t jxl_icc_skip(jxl_allocator_state *alloc, jxl_bs *bs);
jxl_bs_status_t jxl_icc_decode(jxl_allocator_state *alloc, jxl_bs *bs, uint8_t **out_data,
                               size_t *out_len);

#endif /* JXL_IMAGE_ICC_DECODE_H_ */
