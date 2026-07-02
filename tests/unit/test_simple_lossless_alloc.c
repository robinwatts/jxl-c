// SPDX-License-Identifier: MIT OR Apache-2.0
#include "jxl_oxide/jxl_oxide.h"

#include "allocator.h"
#include "test_helpers.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
    size_t alloc_calls;
    size_t free_calls;
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

static int expect_encode_uses_context_allocator(void) {
    jxl_context_options opts;
    alloc_counts_t counts = {0};
    jxl_context *ctx = NULL;
    uint8_t pixels[4] = {0, 64, 128, 255};
    uint8_t *jxl = NULL;
    size_t jxl_len = 0;
    jxl_simple_lossless_image_desc desc;
    size_t allocs_before;
    size_t frees_before;

    memset(&opts, 0, sizeof(opts));
    opts.alloc.alloc = counting_alloc;
    opts.alloc.free = counting_free;
    opts.alloc.user_data = &counts;
    JXL_TEST_ASSERT_EQ(jxl_context_create(&opts, &ctx), JXL_OK);

    memset(&desc, 0, sizeof(desc));
    desc.width = 2;
    desc.height = 2;
    desc.num_channels = 1;
    desc.bits_per_sample = 8;
    desc.effort = 1;
    allocs_before = counts.alloc_calls;
    frees_before = counts.free_calls;
    JXL_TEST_ASSERT_EQ(jxl_simple_lossless_encode(ctx, &desc, pixels, 2, &jxl, &jxl_len), JXL_OK);
    assert(jxl != NULL);
    assert(jxl_len > 0);
    assert(counts.alloc_calls > allocs_before);
    jxl_ctx_free(ctx, jxl);
    assert(counts.free_calls > frees_before);
    jxl_context_destroy(ctx);
    return 0;
}

int main(void) {
    if (expect_encode_uses_context_allocator() != 0) {
        fprintf(stderr, "test_simple_lossless_alloc failed\n");
        return 1;
    }
    printf("test_simple_lossless_alloc: ok\n");
    return 0;
}
