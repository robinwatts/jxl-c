// SPDX-License-Identifier: MIT OR Apache-2.0
#include "allocator.h"
#include "context.h"
#include "jxl/cms.h"
#include "jxl/status.h"
#include "render/cms_lcms.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#if defined(JXL_C_HAVE_LCMS2)
#include "linear_srgb_icc.inc"

typedef struct {
    size_t alloc_calls;
    size_t free_calls;
    size_t calloc_calls;
    size_t realloc_calls;
} alloc_counts_t;

static void *counting_alloc(void *user_data, size_t size) {
    alloc_counts_t *counts = user_data;
    counts->alloc_calls++;
    return malloc(size);
}

static void counting_free(void *user_data, void *ptr) {
    alloc_counts_t *counts = user_data;
    counts->free_calls++;
    free(ptr);
}

static void *counting_calloc(void *user_data, size_t nmemb, size_t size) {
    alloc_counts_t *counts = user_data;
    counts->calloc_calls++;
    return calloc(nmemb, size);
}

static void *counting_realloc(void *user_data, void *ptr, size_t size) {
    alloc_counts_t *counts = user_data;
    counts->realloc_calls++;
    return realloc(ptr, size);
}

static void test_lcms_routes_through_custom_allocator(void) {
    jxl_context *ctx = NULL;
    jxl_context_options opts;
    float r;
    float g;
    float b;
    jxl_allocator_t vtable;
    alloc_counts_t counts = {0};
    vtable.alloc = counting_alloc;
    vtable.free = counting_free;
    vtable.calloc = counting_calloc;
    vtable.realloc = counting_realloc;
    vtable.user_data = &counts;

    opts.alloc = vtable;
    opts.cms = NULL;
    assert(jxl_context_create(&opts, &ctx) == JXL_OK);

    r = 0.25f;
    g = 0.5f;
    b = 0.75f;

    jxl_status_t status = jxl_cms_transform_linear_srgb_to_icc(
        ctx, &r, &g, &b, 1, k_jxl_linear_srgb_icc, sizeof(k_jxl_linear_srgb_icc));
    assert(status == JXL_OK);

    assert(counts.alloc_calls + counts.calloc_calls > 0);
    assert(counts.free_calls > 0);
    jxl_context_destroy(ctx);
}
#endif

static jxl_status_t counting_transform(void *user_data, const uint8_t *src_icc, size_t src_len,
                                      const uint8_t *dst_icc, size_t dst_len, float **planes,
                                      size_t num_planes, size_t num_pixels) {
    size_t *calls = (size_t *)user_data;
    (void)src_icc;
    (void)src_len;
    (void)dst_icc;
    (void)dst_len;
    (void)planes;
    (void)num_planes;
    (void)num_pixels;
    if (calls != NULL) {
        *calls += 1;
    }
    return JXL_OK;
}

static void test_context_uses_supplied_cms(void) {
    jxl_context *ctx = NULL;
    jxl_context_options opts;
    jxl_cms cms;
    size_t calls = 0;
    float sample = 0.25f;
    float *planes[1];

    memset(&opts, 0, sizeof(opts));
    memset(&cms, 0, sizeof(cms));
    cms.user_data = &calls;
    cms.transform = counting_transform;
    opts.cms = &cms;
    assert(jxl_context_create(&opts, &ctx) == JXL_OK);
    assert(ctx->cms == &cms);
    planes[0] = &sample;
    assert(ctx->cms->transform(ctx->cms->user_data, NULL, 0, NULL, 0, planes, 1, 1) == JXL_OK);
    assert(calls == 1);
    jxl_context_destroy(ctx);

    assert(jxl_context_create(NULL, &ctx) == JXL_OK);
    assert(ctx->cms == &ctx->cms_default);
    assert(ctx->cms->user_data == ctx);
    assert(ctx->cms->transform == jxl_get_default_cms()->transform);
    jxl_context_destroy(ctx);

    memset(&opts, 0, sizeof(opts));
    opts.cms = jxl_get_default_cms();
    assert(jxl_context_create(&opts, &ctx) == JXL_OK);
    assert(ctx->cms == &ctx->cms_default);
    assert(ctx->cms->user_data == ctx);
    jxl_context_destroy(ctx);
}

int main(void) {
    test_context_uses_supplied_cms();
#if defined(JXL_C_HAVE_LCMS2)
    test_lcms_routes_through_custom_allocator();
    printf("test_cms_alloc: ok\n");
#else
    printf("test_cms_alloc: ok (no LCMS2)\n");
#endif
    return 0;
}
