// SPDX-License-Identifier: MIT OR Apache-2.0
#include "allocator.h"
#include "jxl_oxide/jxl_status.h"
#include "render/cms_lcms.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>

#if defined(JXL_OXIDE_C_HAVE_LCMS2)
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
    jxl_allocator_state state;
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


    jxl_allocator_init(&state, &vtable);

    r = 0.25f;
    g = 0.5f;
    b = 0.75f;

    jxl_status_t status = jxl_cms_transform_linear_srgb_to_icc(
        &state, &r, &g, &b, 1, k_jxl_linear_srgb_icc, sizeof(k_jxl_linear_srgb_icc));
    assert(status == JXL_OK);

    assert(counts.alloc_calls + counts.calloc_calls > 0);
    assert(counts.free_calls > 0);
}
#endif

int main(void) {
#if defined(JXL_OXIDE_C_HAVE_LCMS2)
    test_lcms_routes_through_custom_allocator();
    printf("test_cms_alloc: ok\n");
#else
    printf("test_cms_alloc: skipped (no LCMS2)\n");
#endif
    return 0;
}
