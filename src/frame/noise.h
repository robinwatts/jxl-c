// SPDX-License-Identifier: MIT OR Apache-2.0
#ifndef JXL_FRAME_NOISE_H_
#define JXL_FRAME_NOISE_H_

#include "bitstream/bitstream.h"

typedef struct {
    float lut[8];
} jxl_noise_parameters;

jxl_bs_status_t jxl_noise_parameters_parse(jxl_bs *bs, jxl_noise_parameters *out);

#endif /* JXL_FRAME_NOISE_H_ */
