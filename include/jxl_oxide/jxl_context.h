// SPDX-License-Identifier: MIT OR Apache-2.0
#ifndef JXL_OXIDE_CONTEXT_H_
#define JXL_OXIDE_CONTEXT_H_

#include "jxl_status.h"
#include "jxl_types.h"

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct jxl_context jxl_context;

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
 * heap use in jxl-oxide routes through this vtable, including LCMS2 transform
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

#ifdef __cplusplus
}
#endif

#endif /* JXL_OXIDE_CONTEXT_H_ */
