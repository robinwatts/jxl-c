// SPDX-License-Identifier: MIT OR Apache-2.0
#ifndef JXL_ALLOCATOR_INTERNAL_H_
#define JXL_ALLOCATOR_INTERNAL_H_

#include "jxl/allocator.h"

#include <stddef.h>
#include <stdint.h>

typedef struct jxl_context jxl_context;

typedef struct {
    jxl_allocator_t vtable;
    /* Rotating alignment group for encoder jxl_aligned_memory (JPEG XL). */
    uint32_t next_align_group;
} jxl_allocator_state;

/*
 * Allocation contract
 * -------------------
 * Prefer jxl_alloc / jxl_free on a jxl_context. Alignment is NOT guaranteed
 * for jxl_alloc (see historical notes in allocator.c).
 *
 * jxl_alloc_aligned / jxl_free_aligned: use for buffers indexed as SIMD
 * vector types. alignment must be a power of two >= sizeof(void*).
 * JXL_ALLOC_ALIGN_SIMD128 (16) and JXL_ALLOC_ALIGN_SIMD256 (32) match Rust
 * Vec<__m128i> / Vec<__m256i> alignment. Always pair with jxl_free_aligned.
 *
 * jxl_*_state helpers are for bootstrap (creating a context) and the internal
 * memory_manager bridge. New code should pass jxl_context*.
 */

void jxl_allocator_init(jxl_allocator_state *state, const jxl_allocator_t *user);

void *jxl_alloc_state(jxl_allocator_state *state, size_t size);
void *jxl_alloc_aligned_state(jxl_allocator_state *state, size_t alignment, size_t size);
void *jxl_calloc_state(jxl_allocator_state *state, size_t nmemb, size_t size);
void *jxl_realloc_state(jxl_allocator_state *state, void *ptr, size_t size);
const void *jxl_realloc_const_state(jxl_allocator_state *state, const void *ptr, size_t size);
void jxl_free_state(jxl_allocator_state *state, void *ptr);
void jxl_free_const_state(jxl_allocator_state *state, const void *ptr);
void jxl_free_aligned_state(jxl_allocator_state *state, void *ptr);
char *jxl_strdup_state(jxl_allocator_state *state, const char *src);

#define JXL_ALLOC_ALIGN_SIMD128 16u
#define JXL_ALLOC_ALIGN_SIMD256 32u

void *jxl_alloc(jxl_context *ctx, size_t size);
void *jxl_alloc_aligned(jxl_context *ctx, size_t alignment, size_t size);
void *jxl_calloc(jxl_context *ctx, size_t nmemb, size_t size);
void *jxl_realloc(jxl_context *ctx, void *ptr, size_t size);
void jxl_free(jxl_context *ctx, void *ptr);
void jxl_free_aligned(jxl_context *ctx, void *ptr);
char *jxl_strdup(jxl_context *ctx, const char *src);

jxl_allocator_state *jxl_context_alloc_state(jxl_context *ctx);
const jxl_allocator_state *jxl_context_alloc_state_const(const jxl_context *ctx);

#endif /* JXL_ALLOCATOR_INTERNAL_H_ */
