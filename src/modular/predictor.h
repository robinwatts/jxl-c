// SPDX-License-Identifier: MIT OR Apache-2.0
#ifndef JXL_MODULAR_PREDICTOR_H_
#define JXL_MODULAR_PREDICTOR_H_

#include "bitstream/bitstream.h"
#include "modular/error.h"

#include "jxl_oxide/jxl_types.h"

typedef struct {
    int default_wp;
    uint32_t wp_p1;
    uint32_t wp_p2;
    uint32_t wp_p3a;
    uint32_t wp_p3b;
    uint32_t wp_p3c;
    uint32_t wp_p3d;
    uint32_t wp_p3e;
    uint32_t wp_w0;
    uint32_t wp_w1;
    uint32_t wp_w2;
    uint32_t wp_w3;
} jxl_wp_header;

typedef enum {
    JXL_PREDICTOR_ZERO = 0,
    JXL_PREDICTOR_WEST,
    JXL_PREDICTOR_NORTH,
    JXL_PREDICTOR_AVG_WEST_AND_NORTH,
    JXL_PREDICTOR_SELECT,
    JXL_PREDICTOR_GRADIENT,
    JXL_PREDICTOR_SELF_CORRECTING,
    JXL_PREDICTOR_NORTH_EAST,
    JXL_PREDICTOR_NORTH_WEST,
    JXL_PREDICTOR_WEST_WEST,
    JXL_PREDICTOR_AVG_WEST_AND_NORTH_WEST,
    JXL_PREDICTOR_AVG_NORTH_AND_NORTH_WEST,
    JXL_PREDICTOR_AVG_NORTH_AND_NORTH_EAST,
    JXL_PREDICTOR_AVG_ALL,
} jxl_predictor;

jxl_modular_status_t jxl_wp_header_parse(jxl_bs *bs, jxl_wp_header *out);
jxl_modular_status_t jxl_predictor_from_u32(uint32_t value, jxl_predictor *out);

#endif /* JXL_MODULAR_PREDICTOR_H_ */
