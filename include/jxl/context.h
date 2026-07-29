// SPDX-License-Identifier: MIT OR Apache-2.0
#ifndef JXL_CONTEXT_H_
#define JXL_CONTEXT_H_

#include <jxl/status.h>

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct jxl_context jxl_context;
typedef struct jxl_cms jxl_cms;

typedef void *(*jxl_alloc_fn)(void *user_data, size_t size);
typedef void (*jxl_free_fn)(void *user_data, void *ptr);
typedef void *(*jxl_calloc_fn)(void *user_data, size_t nmemb, size_t size);
typedef void *(*jxl_realloc_fn)(void *user_data, void *ptr, size_t size);

/*
 * Pluggable heap for a jxl_context. Passed via jxl_context_options::alloc.
 * alloc and free are required; calloc and realloc may be NULL (library defaults).
 */
typedef struct {
    jxl_alloc_fn alloc;
    jxl_free_fn free;
    jxl_calloc_fn calloc;
    jxl_realloc_fn realloc;
    void *user_data;
} jxl_allocator_t;

/*
 * Library session: owns the allocator vtable, optional CMS backend, and
 * production caches (dequant tables, HF coefficient order LUTs).
 *
 * Lifetime
 * --------
 * Create one context per decode session (or per thread). Pass the same ctx to
 * jxl_decoder_create, jxl_decoder_render, and jxl_render_destroy. The context
 * must remain valid until every decoder and render created with it is
 * destroyed. Undefined behaviour if ctx is NULL or destroyed while still in use.
 *
 * Threading
 * ---------
 * Not thread-safe: do not share one context across threads without external
 * locking. Prefer one context per thread.
 *
 * Allocator
 * ---------
 * opts->alloc supplies alloc/free (required). calloc and realloc may be NULL;
 * the library supplies portable defaults (zero-fill and copy-on-grow). All
 * heap use in jxl-c routes through this vtable, including LCMS2 transform
 * setup and Brotli container decompression when those subsystems are used.
 * Pair every successful allocation with a free through the same context.
 */
typedef struct {
    jxl_allocator_t alloc;
    const jxl_cms *cms; /* NULL → built-in LCMS2 */
} jxl_context_options;

/*
 * Create a library context. opts may be NULL (default libc allocator, built-in CMS).
 */
jxl_status_t jxl_context_create(const jxl_context_options *opts, jxl_context **out);
void jxl_context_destroy(jxl_context *ctx);

/* Heap helpers that route through the context allocator. */
void *jxl_alloc(jxl_context *ctx, size_t size);
void jxl_free(jxl_context *ctx, void *ptr);

#ifdef __cplusplus
}
#endif

#endif /* JXL_CONTEXT_H_ */
