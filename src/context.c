// SPDX-License-Identifier: MIT OR Apache-2.0
#define _CRT_SECURE_NO_WARNINGS // Shut up, MSVC

#include "context.h"

#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifndef NDEBUG
static int env_truthy(const char *name) {
    const char *v = getenv(name);
    return v != NULL && v[0] != '\0';
}

static int env_nonzero(const char *name) {
    const char *v = getenv(name);
    return v != NULL && v[0] != '\0' && v[0] != '0';
}

static void env_parse_ulong(const char *name, int *out_active, unsigned *out_value) {
    const char *v = getenv(name);
    char *end;
    unsigned long val;
    if (out_active != NULL) {
        *out_active = 0;
    }
    if (v == NULL || v[0] == '\0') {
        return;
    }
    end = NULL;
    val = strtoul(v, &end, 10);
    if (end == v) {
        return;
    }
    if (out_active != NULL) {
        *out_active = 1;
    }
    if (out_value != NULL) {
        *out_value = (unsigned)val;
    }
}

#endif /* !NDEBUG */

void jxl_debug_flags_read(jxl_debug_flags *out) {
    if (out == NULL) {
        return;
    }

    memset(out, 0, sizeof(*out));
#ifndef NDEBUG
    out->debug_tokens_limit = 64;

    out->debug_hf_trace = env_truthy("JXL_DEBUG_HF_TRACE");
    out->debug_hf_coeff = env_truthy("JXL_DEBUG_HF_COEFF");
    out->debug_tokens = env_truthy("JXL_DEBUG_TOKENS");
    out->debug_bits = env_truthy("JXL_DEBUG_BITS");
    out->debug_pg_fail = env_truthy("JXL_DEBUG_PG_FAIL");
    out->debug_inverse = env_truthy("JXL_DEBUG_INVERSE");
    out->debug_fb = env_truthy("JXL_DEBUG_FB");
    out->debug_lf = env_truthy("JXL_DEBUG_LF");
    out->debug_prereq = env_truthy("JXL_DEBUG_PREREQ");
    out->debug_ma_walk = env_truthy("JXL_DEBUG_MA_WALK");
    out->debug_vardct_pg = env_truthy("JXL_DEBUG_VARDCT_PG");
    out->dump_fast_rle = env_nonzero("JXL_DUMP_FAST_RLE");
    out->skip_restoration = env_truthy("JXL_SKIP_RESTORATION");
    out->skip_patches = env_truthy("JXL_SKIP_PATCHES");
    out->skip_splines = env_truthy("JXL_SKIP_SPLINES");
    out->skip_lf_frame = env_truthy("JXL_SKIP_LF_FRAME");
    out->skip_all_lf_pg = env_nonzero("JXL_SKIP_ALL_LF_PG");
    out->epf_assert_row = env_truthy("JXL_EPF_ASSERT_ROW");
    out->force_wide_buffers = env_nonzero("JXL_FORCE_WIDE_BUFFERS");

    {
        const char *pixel = getenv("JXL_DEBUG_PIXEL");
        if (pixel != NULL && pixel[0] != '\0') {
            unsigned dy = 0;
            unsigned dx = 0;
            unsigned dch = 0;
            if (sscanf(pixel, "%u:%u:%u", &dy, &dx, &dch) == 3) {
                out->debug_pixel = 1;
                out->debug_pixel_y = dy;
                out->debug_pixel_x = dx;
                out->debug_pixel_ch = dch;
            }
        }
    }

    {
        const char *filter = getenv("JXL_DEBUG_TOKENS_PG");
        if (filter != NULL && filter[0] != '\0') {
            unsigned fp = UINT_MAX;
            unsigned fg = UINT_MAX;
            if (sscanf(filter, "%u:%u", &fp, &fg) == 2) {
                out->debug_tokens_pg_active = 1;
                out->debug_tokens_pg_pass = fp;
                out->debug_tokens_pg_group = fg;
            }
        }
    }

    {
        const char *lim = getenv("JXL_DEBUG_TOKENS_LIMIT");
        if (lim != NULL && lim[0] != '\0') {
            out->debug_tokens_limit = (unsigned)strtoul(lim, NULL, 10);
        }
    }

    env_parse_ulong("JXL_ONLY_LF_PG", &out->only_lf_pg_active, &out->only_lf_pg);
    env_parse_ulong("JXL_SKIP_LF_PG", &out->skip_lf_pg_active, &out->skip_lf_pg);
#endif /* !NDEBUG */
}

const jxl_debug_flags *jxl_context_debug(const jxl_context *ctx) {
    return &ctx->debug;
}

void jxl_context_init_inplace(jxl_context *ctx, const jxl_context_options *opts) {
    if (ctx == NULL) {
        return;
    }
    memset(ctx, 0, sizeof(*ctx));
    jxl_allocator_init(&ctx->alloc, opts != NULL ? &opts->alloc : NULL);
    ctx->cms = opts != NULL ? opts->cms : NULL;
    jxl_debug_flags_read(&ctx->debug);
}

void jxl_context_fini_inplace(jxl_context *ctx) {
    if (ctx == NULL) {
        return;
    }
    jxl_context_dequant_free(ctx);
    memset(&ctx->cpu_features, 0, sizeof(ctx->cpu_features));
    memset(&ctx->hf_orders, 0, sizeof(ctx->hf_orders));
}

jxl_status_t jxl_context_create(const jxl_context_options *opts, jxl_context **out) {
    jxl_allocator_state scratch;
    jxl_context *ctx;
    if (out == NULL) {
        return JXL_ERROR_INVALID_INPUT;
    }
    *out = NULL;

    jxl_allocator_init(&scratch, opts != NULL ? &opts->alloc : NULL);

    ctx = jxl_alloc(&scratch, sizeof(*ctx));
    if (ctx == NULL) {
        return JXL_ERROR_OUT_OF_MEMORY;
    }
    jxl_context_init_inplace(ctx, opts);
    ctx->alloc = scratch;
    *out = ctx;
    return JXL_OK;
}

void jxl_context_destroy(jxl_context *ctx) {
    if (ctx == NULL) {
        return;
    }
    jxl_context_fini_inplace(ctx);
    jxl_ctx_free(ctx, ctx);
}

jxl_allocator_state *jxl_context_alloc_state(jxl_context *ctx) {
    return ctx != NULL ? &ctx->alloc : NULL;
}

const jxl_allocator_state *jxl_context_alloc_state_const(const jxl_context *ctx) {
    return ctx != NULL ? &ctx->alloc : NULL;
}
