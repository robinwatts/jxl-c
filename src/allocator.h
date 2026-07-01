// SPDX-License-Identifier: MIT OR Apache-2.0
#ifndef JXL_OXIDE_ALLOCATOR_H_
#define JXL_OXIDE_ALLOCATOR_H_

#include "jxl_oxide/jxl_types.h"

#include <stddef.h>

typedef struct jxl_context jxl_context;

typedef struct {
    jxl_allocator_t vtable;
} jxl_allocator_state;

/*
 * Allocation contract
 * -------------------
 * jxl_alloc / jxl_free: general heap. Alignment is NOT guaranteed. With the
 * default vtable (realloc != NULL) this is typically malloc alignment (often
 * 16 bytes on glibc x86_64). With a custom vtable where realloc == NULL,
 * jxl_alloc prepends a size_t header and the returned pointer may be only
 * 8-byte aligned.
 *
 * jxl_alloc_aligned / jxl_free_aligned: use for buffers indexed as SIMD
 * vector types (__m128, __m128i, __m256i, etc.). The compiler may emit aligned
 * loads/stores (movdqa/vmovdqa) on such indexing at -O3 even when image data
 * uses loadu/storeu. alignment must be a power of two >= sizeof(void*).
 * JXL_ALLOC_ALIGN_SIMD128 (16) and JXL_ALLOC_ALIGN_SIMD256 (32) match Rust
 * Vec<__m128i> / Vec<__m256i> alignment. Always pair with jxl_free_aligned.
 *
 * Scalar-only scratch (int16_t, float rows, etc.) may use jxl_alloc.
 */

void jxl_allocator_init(jxl_allocator_state *state, const jxl_allocator_t *user);
void *jxl_alloc(jxl_allocator_state *state, size_t size);
void *jxl_alloc_aligned(jxl_allocator_state *state, size_t alignment, size_t size);
void *jxl_calloc(jxl_allocator_state *state, size_t nmemb, size_t size);
void *jxl_realloc(jxl_allocator_state *state, void *ptr, size_t size);
const void *jxl_realloc_const(jxl_allocator_state *state, const void *ptr, size_t size);
void jxl_free(jxl_allocator_state *state, void *ptr);
void jxl_free_const(jxl_allocator_state *state, const void *ptr);
void jxl_free_aligned(jxl_allocator_state *state, void *ptr);
char *jxl_strdup(jxl_allocator_state *state, const char *src);

#define JXL_ALLOC_ALIGN_SIMD128 16u
#define JXL_ALLOC_ALIGN_SIMD256 32u

void *jxl_ctx_alloc(jxl_context *ctx, size_t size);
void *jxl_ctx_alloc_aligned(jxl_context *ctx, size_t alignment, size_t size);
void *jxl_ctx_calloc(jxl_context *ctx, size_t nmemb, size_t size);
void *jxl_ctx_realloc(jxl_context *ctx, void *ptr, size_t size);
void jxl_ctx_free(jxl_context *ctx, void *ptr);
void jxl_ctx_free_aligned(jxl_context *ctx, void *ptr);
char *jxl_ctx_strdup(jxl_context *ctx, const char *src);

#endif /* JXL_OXIDE_ALLOCATOR_H_ */
