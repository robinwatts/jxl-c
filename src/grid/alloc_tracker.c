// SPDX-License-Identifier: MIT OR Apache-2.0
#include "alloc_tracker.h"

#include <string.h>

struct jxl_grid_alloc_tracker {
    jxl_allocator_state *alloc;
    size_t bytes_left;
};

struct jxl_grid_alloc_handle {
    size_t bytes;
    jxl_grid_alloc_tracker *tracker;
};

jxl_grid_alloc_tracker *jxl_grid_alloc_tracker_create(jxl_allocator_state *alloc,
                                                      size_t bytes_limit) {
    jxl_grid_alloc_tracker *t;
    if (alloc == NULL) {
        return NULL;
    }
    t = jxl_calloc(alloc, 1, sizeof(*t));
    if (t == NULL) {
        return NULL;
    }
    t->alloc = alloc;
    t->bytes_left = bytes_limit;
    return t;
}

void jxl_grid_alloc_tracker_destroy(jxl_grid_alloc_tracker *tracker) {
    jxl_allocator_state *alloc;
    if (tracker == NULL) {
        return;
    }
    alloc = tracker->alloc;
    jxl_free(alloc, tracker);
}

int jxl_grid_alloc_tracker_alloc(jxl_grid_alloc_tracker *tracker, size_t bytes,
                                 jxl_grid_alloc_handle **out) {
    jxl_grid_alloc_handle *h;
    if (out == NULL) {
        return 0;
    }
    *out = NULL;
    if (tracker == NULL) {
        return 1;
    }

    if (tracker->bytes_left < bytes) {
        return 0;
    }
    tracker->bytes_left -= bytes;

    h = jxl_alloc(tracker->alloc, sizeof(*h));
    if (h == NULL) {
        tracker->bytes_left += bytes;
        return 0;
    }
    h->bytes = bytes;
    h->tracker = tracker;
    *out = h;
    return 1;
}

void jxl_grid_alloc_handle_release(jxl_grid_alloc_handle *handle) {
    jxl_allocator_state *alloc;
    if (handle == NULL) {
        return;
    }
    alloc =
        handle->tracker != NULL ? handle->tracker->alloc : NULL;
    if (handle->bytes != 0 && handle->tracker != NULL) {
        handle->tracker->bytes_left += handle->bytes;
        handle->bytes = 0;
    }
    if (alloc != NULL) {
        jxl_free(alloc, handle);
    }
}

void jxl_grid_alloc_tracker_expand(jxl_grid_alloc_tracker *tracker, size_t by_bytes) {
    if (tracker != NULL) {
        tracker->bytes_left += by_bytes;
    }
}

int jxl_grid_alloc_tracker_shrink(jxl_grid_alloc_tracker *tracker, size_t by_bytes) {
    if (tracker == NULL) {
        return 1;
    }
    if (tracker->bytes_left < by_bytes) {
        return 0;
    }
    tracker->bytes_left -= by_bytes;
    return 1;
}

jxl_grid_alloc_tracker *jxl_grid_alloc_handle_tracker(const jxl_grid_alloc_handle *handle) {
    return handle != NULL ? handle->tracker : NULL;
}
