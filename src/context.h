// SPDX-License-Identifier: MIT OR Apache-2.0
#ifndef JXL_OXIDE_CONTEXT_INTERNAL_H_
#define JXL_OXIDE_CONTEXT_INTERNAL_H_

#include "allocator.h"
#include "jxl_oxide/jxl_context.h"
#include "jxl_oxide/jxl_status.h"
#include "jxl_oxide/jxl_types.h"
#include "render/simd/features.h"
#include "vardct/dequant.h"
#include "vardct/hf_pass.h"

typedef struct jxl_debug_flags {
    int debug_hf_trace;
    int debug_hf_coeff;
    int debug_tokens;
    unsigned debug_tokens_limit;
    int debug_tokens_pg_active;
    unsigned debug_tokens_pg_pass;
    unsigned debug_tokens_pg_group;
    int debug_pixel;
    unsigned debug_pixel_y;
    unsigned debug_pixel_x;
    unsigned debug_pixel_ch;
    int debug_bits;
    int debug_pg_fail;
    int debug_inverse;
    int debug_fb;
    int debug_lf;
    int debug_prereq;
    int debug_ma_walk;
    int debug_vardct_pg;
    int dump_fast_rle;
    int skip_restoration;
    int skip_patches;
    int skip_splines;
    int skip_lf_frame;
    int skip_all_lf_pg;
    int epf_assert_row;
    int epf_test_force_scalar;
    int force_wide_buffers;
    int only_lf_pg_active;
    unsigned only_lf_pg;
    int skip_lf_pg_active;
    unsigned skip_lf_pg;
} jxl_debug_flags;

void jxl_debug_flags_read(jxl_debug_flags *out);
const jxl_debug_flags *jxl_context_debug(const jxl_context *ctx);

#ifdef NDEBUG
#define JXL_DEBUG_FLAG(ctx, flag) (0)
#else
#define JXL_DEBUG_FLAG(ctx, flag) (jxl_context_debug(ctx)->flag)
#endif

typedef struct {
    float *data;
    size_t len;
} jxl_context_dequant_buf;

typedef struct {
    jxl_context_dequant_buf weights[JXL_DEQUANT_MATRIX_COUNT][3];
    jxl_context_dequant_buf weights_tr[JXL_DEQUANT_MATRIX_COUNT][3];
} jxl_context_dequant;

typedef struct {
    int initialized;
    jxl_cpu_features features;
} jxl_context_cpu_features_cache;

typedef struct {
    int initialized;
    jxl_coeff_order natural_8x8[64];
    jxl_coeff_order natural_16x16[256];
    jxl_coeff_order natural_32x32[1024];
    jxl_coeff_order natural_16x8[128];
    jxl_coeff_order natural_32x8[256];
    jxl_coeff_order natural_32x16[512];
    jxl_coeff_order natural_64x64[4096];
    jxl_coeff_order natural_64x32[2048];
    jxl_coeff_order natural_128x128[16384];
} jxl_context_hf_orders;

typedef struct jxl_context {
    jxl_allocator_state alloc;
    const jxl_cms *cms;
    jxl_debug_flags debug;
    jxl_context_cpu_features_cache cpu_features;
    jxl_context_dequant dequant;
    jxl_context_hf_orders hf_orders;
} jxl_context;

void jxl_context_init_inplace(jxl_context *ctx, const jxl_context_options *opts);
void jxl_context_fini_inplace(jxl_context *ctx);
void jxl_context_dequant_free(jxl_context *ctx);

jxl_allocator_state *jxl_context_alloc_state(jxl_context *ctx);
const jxl_allocator_state *jxl_context_alloc_state_const(const jxl_context *ctx);

#endif /* JXL_OXIDE_CONTEXT_INTERNAL_H_ */
