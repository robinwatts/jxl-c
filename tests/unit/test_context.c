// SPDX-License-Identifier: MIT OR Apache-2.0
#include "allocator.h"
#include "context.h"
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

static void test_create_destroy_default(void) {
    jxl_context *ctx = NULL;
    JXL_TEST_ASSERT_EQ(jxl_context_create(NULL, &ctx), JXL_OK);
    assert(ctx != NULL);
    assert(jxl_context_alloc_state(ctx) == &ctx->alloc);
    assert(jxl_context_alloc_state_const(ctx) == &ctx->alloc);
    jxl_context_destroy(ctx);
}

static void test_ctx_alloc_helpers(void) {
    jxl_context *ctx = NULL;
    JXL_TEST_ASSERT_EQ(jxl_context_create(NULL, &ctx), JXL_OK);

    int *values = jxl_ctx_calloc(ctx, 4, sizeof(int));
    assert(values != NULL);
    assert(values[0] == 0 && values[3] == 0);
    values[0] = 7;

    int *grown = jxl_ctx_realloc(ctx, values, 8 * sizeof(int));
    assert(grown != NULL);
    assert(grown[0] == 7);

    char *copy = jxl_ctx_strdup(ctx, "ctx");
    assert(copy != NULL);
    assert(strcmp(copy, "ctx") == 0);

    jxl_ctx_free(ctx, grown);
    jxl_ctx_free(ctx, copy);
    jxl_context_destroy(ctx);
}

static void test_custom_allocator(void) {
    jxl_context_options opts;
    alloc_counts_t counts = {0};
    opts.alloc.alloc = counting_alloc;
    opts.alloc.free = counting_free;
    opts.alloc.user_data = &counts;
    opts.cms = NULL;


    jxl_context *ctx = NULL;
    JXL_TEST_ASSERT_EQ(jxl_context_create(&opts, &ctx), JXL_OK);
    assert(counts.alloc_calls >= 1);

    void *buf = jxl_ctx_alloc(ctx, 32);
    assert(buf != NULL);
    assert(counts.alloc_calls >= 2);

    jxl_ctx_free(ctx, buf);
    assert(counts.free_calls >= 1);

    size_t allocs_before_destroy = counts.alloc_calls;
    size_t frees_before_destroy = counts.free_calls;
    jxl_context_destroy(ctx);
    assert(counts.alloc_calls == allocs_before_destroy);
    assert(counts.free_calls == frees_before_destroy + 1);
}

int main(void) {
    test_create_destroy_default();
    test_ctx_alloc_helpers();
    test_custom_allocator();
    printf("test_context: ok\n");
    return 0;
}
