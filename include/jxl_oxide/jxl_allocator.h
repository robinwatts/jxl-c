// SPDX-License-Identifier: MIT OR Apache-2.0
#ifndef JXL_OXIDE_ALLOCATOR_TYPES_H_
#define JXL_OXIDE_ALLOCATOR_TYPES_H_

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

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

#ifdef __cplusplus
}
#endif

#endif /* JXL_OXIDE_ALLOCATOR_TYPES_H_ */
