// SPDX-License-Identifier: MIT OR Apache-2.0
#include "allocator.h"

#include <assert.h>
#include <emmintrin.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "jxl_oxide/jxl_types.h"

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

static void test_default_allocator(void) {
    int i;
    jxl_allocator_state state;
    jxl_allocator_init(&state, NULL);

    int *buf = jxl_calloc(&state, 4, sizeof(int));
    assert(buf != NULL);
    for (i = 0; i < 4; ++i) {
        assert(buf[i] == 0);
    }
    buf[0] = 42;

    int *grown = jxl_realloc(&state, buf, 8 * sizeof(int));
    assert(grown != NULL);
    assert(grown[0] == 42);

    jxl_free(&state, grown);

    char *copy = jxl_strdup(&state, "jxl");
    assert(copy != NULL);
    assert(strcmp(copy, "jxl") == 0);
    jxl_free(&state, copy);
}

static void test_custom_vtable_counts(void) {
    jxl_allocator_state state;
    jxl_allocator_t vtable;
    alloc_counts_t counts = {0};
    vtable.alloc = counting_alloc;
    vtable.free = counting_free;
    vtable.calloc = counting_calloc;
    vtable.realloc = counting_realloc;
    vtable.user_data = &counts;


    jxl_allocator_init(&state, &vtable);

    void *a = jxl_alloc(&state, 16);
    assert(a != NULL);
    assert(counts.alloc_calls == 1);

    void *z = jxl_calloc(&state, 2, 8);
    assert(z != NULL);
    assert(counts.calloc_calls == 1);

    void *b = jxl_realloc(&state, a, 32);
    assert(b != NULL);
    assert(counts.realloc_calls == 1);

    jxl_free(&state, b);
    jxl_free(&state, z);
    assert(counts.free_calls == 2);
}

static void test_alloc_only_vtable_gets_defaults(void) {
    jxl_allocator_state state;
    jxl_allocator_t vtable;
    alloc_counts_t counts = {0};
    vtable.alloc = counting_alloc;
    vtable.free = counting_free;
    vtable.calloc = NULL;
    vtable.realloc = NULL;
    vtable.user_data = &counts;


    jxl_allocator_init(&state, &vtable);

    uint8_t *buf = jxl_calloc(&state, 1, 4);
    assert(buf != NULL);
    assert(buf[0] == 0 && buf[1] == 0 && buf[2] == 0 && buf[3] == 0);
    assert(counts.alloc_calls == 1);
    assert(counts.calloc_calls == 0);

    uint8_t *grown = jxl_realloc(&state, buf, 8);
    assert(grown != NULL);
    assert(counts.alloc_calls == 2);
    assert(counts.realloc_calls == 0);

    jxl_free(&state, grown);
    assert(counts.free_calls == 2);
}

static void test_calloc_overflow(void) {
    jxl_allocator_state state;
    jxl_allocator_init(&state, NULL);
    assert(jxl_calloc(&state, SIZE_MAX, 2) == NULL);
}

static void test_alloc_aligned_default(void) {
    size_t align;
    jxl_allocator_state state;
    jxl_allocator_init(&state, NULL);

    for (align = 16; align <= 32; align += 16) {
        uint8_t *buf = jxl_alloc_aligned(&state, align, 64);
        assert(buf != NULL);
        assert(((uintptr_t)buf & (align - 1)) == 0);
        buf[0] = 0xab;
        buf[63] = 0xcd;
        jxl_free_aligned(&state, buf);
    }

    assert(jxl_alloc_aligned(&state, 16, 0) == NULL);
    assert(jxl_alloc_aligned(&state, 15, 16) == NULL);
    assert(jxl_alloc_aligned(NULL, 16, 16) == NULL);
}

static void test_alloc_aligned_size_prefix_vtable(void) {
    jxl_allocator_state state;
    jxl_allocator_t vtable;
    alloc_counts_t counts = {0};
    vtable.alloc = counting_alloc;
    vtable.free = counting_free;
    vtable.calloc = NULL;
    vtable.realloc = NULL;
    vtable.user_data = &counts;


    jxl_allocator_init(&state, &vtable);

    __m128i *scratch =
        (__m128i *)jxl_alloc_aligned(&state, JXL_ALLOC_ALIGN_SIMD128, 8 * sizeof(__m128i));
    assert(scratch != NULL);
    assert(((uintptr_t)scratch & (JXL_ALLOC_ALIGN_SIMD128 - 1)) == 0);
    scratch[0] = _mm_set1_epi16(1);
    jxl_free_aligned(&state, scratch);
    assert(counts.free_calls == 1);
}

int main(void) {
    test_default_allocator();
    test_custom_vtable_counts();
    test_alloc_only_vtable_gets_defaults();
    test_calloc_overflow();
    test_alloc_aligned_default();
    test_alloc_aligned_size_prefix_vtable();
    printf("test_allocator: ok\n");
    return 0;
}
