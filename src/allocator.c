// SPDX-License-Identifier: MIT OR Apache-2.0
#include "allocator.h"

#include "context.h"

#include "jxl_oxide/jxl_types.h"
#include <stdlib.h>
#include <string.h>

static void *default_alloc(void *user_data, size_t size) {
    (void)user_data;
    return malloc(size);
}

static void default_free(void *user_data, void *ptr) {
    (void)user_data;
    free(ptr);
}

static void *default_calloc(void *user_data, size_t nmemb, size_t size) {
    (void)user_data;
    return calloc(nmemb, size);
}

static void *default_realloc(void *user_data, void *ptr, size_t size) {
    (void)user_data;
    return realloc(ptr, size);
}

static int alloc_size_overflow(size_t nmemb, size_t size, size_t *out_total) {
    if (nmemb != 0 && size > SIZE_MAX / nmemb) {
        return 0;
    }
    *out_total = nmemb * size;
    return 1;
}

static int alloc_add_overflow(size_t a, size_t b, size_t *out_total) {
    if (a > SIZE_MAX - b) {
        return 0;
    }
    *out_total = a + b;
    return 1;
}

static int alloc_is_power_of_two(size_t alignment) {
    return alignment != 0 && (alignment & (alignment - 1)) == 0;
}

static void allocator_set_defaults(jxl_allocator_t *vtable) {
    vtable->alloc = default_alloc;
    vtable->free = default_free;
    vtable->calloc = default_calloc;
    vtable->realloc = default_realloc;
    vtable->user_data = NULL;
}

static size_t *alloc_block_ptr(void *ptr) {
    return (size_t *)ptr - 1;
}

static void *alloc_user_ptr(size_t *block) {
    return block + 1;
}

void jxl_allocator_init(jxl_allocator_state *state, const jxl_allocator_t *user) {
    if (state == NULL) {
        return;
    }
    if (user != NULL && user->alloc != NULL && user->free != NULL) {
        state->vtable = *user;
    } else {
        allocator_set_defaults(&state->vtable);
    }
}

void *jxl_alloc(jxl_allocator_state *state, size_t size) {
    size_t *block;
    if (state == NULL || size == 0) {
        return NULL;
    }
    if (state->vtable.realloc != NULL) {
        return state->vtable.alloc(state->vtable.user_data, size);
    }
    block =
        (size_t *)state->vtable.alloc(state->vtable.user_data, size + sizeof(size_t));
    if (block == NULL) {
        return NULL;
    }
    *block = size;
    return alloc_user_ptr(block);
}

void jxl_free(jxl_allocator_state *state, void *ptr) {
    if (state == NULL || ptr == NULL) {
        return;
    }
    if (state->vtable.realloc != NULL) {
        state->vtable.free(state->vtable.user_data, ptr);
        return;
    }
    state->vtable.free(state->vtable.user_data, alloc_block_ptr(ptr));
}

void jxl_free_const(jxl_allocator_state *state, const void *ptr) {
    union { void *p; const void *cp; } x;
    x.cp = ptr;
    jxl_free(state, x.p);
}

void *jxl_alloc_aligned(jxl_allocator_state *state, size_t alignment, size_t size) {
    size_t raw_size;
    void *raw;
    uintptr_t aligned;
    if (state == NULL || size == 0 || alignment < sizeof(void *) || !alloc_is_power_of_two(alignment)) {
        return NULL;
    }
    raw_size = 0;
    if (!alloc_add_overflow(size, alignment - 1, &raw_size) ||
        !alloc_add_overflow(raw_size, sizeof(void *), &raw_size)) {
        return NULL;
    }
    raw = jxl_alloc(state, raw_size);
    if (raw == NULL) {
        return NULL;
    }
    aligned =
        ((uintptr_t)raw + sizeof(void *) + alignment - 1) & ~(uintptr_t)(alignment - 1);
    ((void **)aligned)[-1] = raw;
    return (void *)aligned;
}

void jxl_free_aligned(jxl_allocator_state *state, void *ptr) {
    if (state == NULL || ptr == NULL) {
        return;
    }
    jxl_free(state, ((void **)ptr)[-1]);
}

void *jxl_calloc(jxl_allocator_state *state, size_t nmemb, size_t size) {
    size_t total;
    void *ptr;
    if (state == NULL || nmemb == 0 || size == 0) {
        return NULL;
    }
    total = 0;
    if (!alloc_size_overflow(nmemb, size, &total)) {
        return NULL;
    }
    if (state->vtable.calloc != NULL) {
        return state->vtable.calloc(state->vtable.user_data, nmemb, size);
    }
    ptr = jxl_alloc(state, total);
    if (ptr != NULL) {
        memset(ptr, 0, total);
    }
    return ptr;
}

void *jxl_realloc(jxl_allocator_state *state, void *ptr, size_t size) {
    size_t old_size;
    size_t copy;
    void *grown;
    if (state == NULL) {
        return NULL;
    }
    if (state->vtable.realloc != NULL) {
        return state->vtable.realloc(state->vtable.user_data, ptr, size);
    }
    if (ptr == NULL) {
        return size == 0 ? NULL : jxl_alloc(state, size);
    }
    if (size == 0) {
        jxl_free(state, ptr);
        return NULL;
    }
    old_size = *alloc_block_ptr(ptr);
    grown = jxl_alloc(state, size);
    if (grown == NULL) {
        return NULL;
    }
    copy = old_size < size ? old_size : size;
    memcpy(grown, ptr, copy);
    jxl_free(state, ptr);
    return grown;
}

const void *jxl_realloc_const(jxl_allocator_state *state, const void *ptr, size_t size) {
    union { void *p; const void *cp; } x;
    x.cp = ptr;
    return jxl_realloc(state, x.p, size);
}

char *jxl_strdup(jxl_allocator_state *state, const char *src) {
    size_t len;
    char *copy;
    if (src == NULL) {
        return NULL;
    }
    len = strlen(src) + 1;
    copy = jxl_alloc(state, len);
    if (copy == NULL) {
        return NULL;
    }
    memcpy(copy, src, len);
    return copy;
}

void *jxl_ctx_alloc(jxl_context *ctx, size_t size) {
    return jxl_alloc(jxl_context_alloc_state(ctx), size);
}

void *jxl_ctx_alloc_aligned(jxl_context *ctx, size_t alignment, size_t size) {
    return jxl_alloc_aligned(jxl_context_alloc_state(ctx), alignment, size);
}

void *jxl_ctx_calloc(jxl_context *ctx, size_t nmemb, size_t size) {
    return jxl_calloc(jxl_context_alloc_state(ctx), nmemb, size);
}

void *jxl_ctx_realloc(jxl_context *ctx, void *ptr, size_t size) {
    return jxl_realloc(jxl_context_alloc_state(ctx), ptr, size);
}

void jxl_ctx_free(jxl_context *ctx, void *ptr) {
    jxl_free(jxl_context_alloc_state(ctx), ptr);
}

void jxl_ctx_free_aligned(jxl_context *ctx, void *ptr) {
    jxl_free_aligned(jxl_context_alloc_state(ctx), ptr);
}

char *jxl_ctx_strdup(jxl_context *ctx, const char *src) {
    return jxl_strdup(jxl_context_alloc_state(ctx), src);
}
