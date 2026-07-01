// SPDX-License-Identifier: MIT OR Apache-2.0
#include "allocator.h"
#include "render/render_buffer.h"

#include <stdio.h>
#include <string.h>

static int expect_clone_and_truncate(void) {
    size_t i;
    jxl_allocator_state alloc;
    jxl_allocator_init(&alloc, NULL);

    jxl_render *r = jxl_render_create(&alloc, 2u, 1u, 4u, 2u);
    if (r == NULL) {
        return 1;
    }
    jxl_modular_region region = jxl_modular_region_with_size(4u, 2u);
    jxl_render_init_all_planes(r, &region);

    for (i = 0; i < 8; ++i) {
        r->planes[0][i] = (float)(i + 1);
    }
    for (i = 0; i < 8; ++i) {
        r->planes[1][i] = 0.5f + (float)i;
    }

    if (jxl_render_clone_gray(&alloc, r) != JXL_OK) {
        jxl_render_free(&alloc, r);
        return 1;
    }
    if (r->num_planes != 4u || r->color_planes != 3u) {
        jxl_render_free(&alloc, r);
        return 1;
    }
    if (memcmp(r->planes[0], r->planes[1], 8 * sizeof(float)) != 0 ||
        memcmp(r->planes[0], r->planes[2], 8 * sizeof(float)) != 0) {
        jxl_render_free(&alloc, r);
        return 1;
    }
    if (r->planes[3][0] != 0.5f || r->planes[3][7] != 7.5f) {
        jxl_render_free(&alloc, r);
        return 1;
    }

    if (jxl_render_remove_color_planes(&alloc, r, 1u) != JXL_OK) {
        jxl_render_free(&alloc, r);
        return 1;
    }
    if (r->num_planes != 2u || r->color_planes != 1u) {
        jxl_render_free(&alloc, r);
        return 1;
    }
    if (r->planes[0][3] != 4.0f || r->planes[1][0] != 0.5f) {
        jxl_render_free(&alloc, r);
        return 1;
    }

    jxl_render_free(&alloc, r);
    return 0;
}

int main(void) {
    if (expect_clone_and_truncate() != 0) {
        fprintf(stderr, "clone_gray unit test failed\n");
        return 1;
    }
    return 0;
}
