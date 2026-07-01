// SPDX-License-Identifier: MIT OR Apache-2.0
#ifndef JXL_VARDCT_DEQUANT_H_
#define JXL_VARDCT_DEQUANT_H_

#include "allocator.h"
#include "bitstream/bitstream.h"
#include "modular/ma.h"
#include "vardct/dct_select.h"
#include "vardct/error.h"

#include <stddef.h>
#include "jxl_oxide/jxl_types.h"

typedef struct jxl_context jxl_context;

#define JXL_DEQUANT_MATRIX_COUNT 17

typedef enum {
    JXL_DEQUANT_ENC_DEFAULT = 0,
    JXL_DEQUANT_ENC_HORNUSS = 1,
    JXL_DEQUANT_ENC_DCT2 = 2,
    JXL_DEQUANT_ENC_DCT4 = 3,
    JXL_DEQUANT_ENC_DCT4X8 = 4,
    JXL_DEQUANT_ENC_AFV = 5,
    JXL_DEQUANT_ENC_DCT = 6,
    JXL_DEQUANT_ENC_RAW = 7,
} jxl_dequant_encoding;

typedef struct {
    jxl_transform_type dct_select;
    jxl_dequant_encoding encoding;
    float hornuss[3][3];
    float dct2[3][6];
    float dct4_params[3][2];
    float dct4x8_params[3][1];
    float afv_params[3][9];
    float *dct_bands[3];
    size_t dct_band_lens[3];
    float *dct4x4_bands[3];
    size_t dct4x4_band_lens[3];
    float raw_denominator;
} jxl_dequant_matrix_params;

typedef struct {
    jxl_dequant_matrix_params matrices[JXL_DEQUANT_MATRIX_COUNT];
    jxl_context *ctx;
    int has_jpeg_matrices;
    int32_t jpeg_matrices[3][64];
} jxl_dequant_matrix_set;

typedef struct {
    jxl_context *ctx;
    uint32_t bit_depth;
    uint32_t stream_index;
    const jxl_ma_config *global_ma;
    int capture_jpeg_matrices;
    jxl_dequant_matrix_set *out_set;
} jxl_dequant_matrix_set_params;

void jxl_dequant_matrix_params_init(jxl_dequant_matrix_params *p);
void jxl_dequant_matrix_params_free(jxl_allocator_state *alloc, jxl_dequant_matrix_params *p);
void jxl_dequant_matrix_set_init(jxl_dequant_matrix_set *set);
void jxl_dequant_matrix_set_free(jxl_dequant_matrix_set *set);

jxl_vardct_status_t jxl_dequant_matrix_params_default(jxl_allocator_state *alloc,
                                                     jxl_transform_type t,
                                                     jxl_dequant_matrix_params *out);

jxl_vardct_status_t jxl_dequant_matrix_params_parse(jxl_allocator_state *alloc, jxl_bs *bs,
                                                    jxl_transform_type dct_select,
                                                    const jxl_dequant_matrix_set_params *params,
                                                    jxl_dequant_matrix_params *out);

jxl_vardct_status_t jxl_dequant_matrix_set_parse(jxl_context *ctx, jxl_allocator_state *alloc,
                                                 jxl_bs *bs,
                                                 const jxl_dequant_matrix_set_params *params,
                                                 jxl_dequant_matrix_set *out);

uint32_t jxl_dequant_matrix_set_stream_index(uint32_t num_lf_groups);

const int32_t *jxl_dequant_matrix_set_jpeg_quant(const jxl_dequant_matrix_set *set, size_t channel);

#endif /* JXL_VARDCT_DEQUANT_H_ */
