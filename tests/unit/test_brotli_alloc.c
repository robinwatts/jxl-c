// SPDX-License-Identifier: MIT OR Apache-2.0
#include "allocator.h"
#include "bitstream/container/brotli_decode.h"

#include <assert.h>
#include "test_helpers.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

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

static void test_brotli_routes_through_custom_allocator(void) {
    jxl_allocator_state state;
    size_t out_len;
    jxl_allocator_t vtable;
    alloc_counts_t counts = {0};
    vtable.alloc = counting_alloc;
    vtable.free = counting_free;
    vtable.calloc = counting_calloc;
    vtable.realloc = counting_realloc;
    vtable.user_data = &counts;


    jxl_allocator_init(&state, &vtable);

    jxl_brotli_decoder *dec = jxl_brotli_decoder_create(&state);
    assert(dec != NULL);

    /* Brotli JS decode_test.ts empty-string stream. */
    static const uint8_t k_empty_brotli[] = {1, 11, 0, 42, 3};
    JXL_TEST_ASSERT_EQ(jxl_brotli_decoder_feed(dec, k_empty_brotli, sizeof(k_empty_brotli)), JXL_BS_OK);
    JXL_TEST_ASSERT_EQ(jxl_brotli_decoder_finish(dec), JXL_BS_OK);

    out_len = 0;
    const uint8_t *out = jxl_brotli_decoder_output(dec, &out_len);
    assert(out != NULL && out_len == 0);

    assert(counts.alloc_calls + counts.calloc_calls > 0);

    jxl_brotli_decoder_destroy(&state, dec);
    assert(counts.free_calls > 0);
}

int main(void) {
    test_brotli_routes_through_custom_allocator();
    printf("test_brotli_alloc: ok\n");
    return 0;
}
