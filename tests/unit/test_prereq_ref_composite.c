// SPDX-License-Identifier: MIT OR Apache-2.0
#include "test_paths.h"

#include "allocator.h"
#include "codestream_collect.h"
#include "context.h"
#include "render/patch_render.h"
#include "test_helpers.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>


static int read_file(const char *path, uint8_t **out, size_t *out_len) {
    FILE *f = fopen(path, "rb");
    if (f == NULL) {
        return -1;
    }
    if (fseek(f, 0, SEEK_END) != 0) {
        fclose(f);
        return -1;
    }
    long sz = ftell(f);
    if (sz < 0) {
        fclose(f);
        return -1;
    }
    if (fseek(f, 0, SEEK_SET) != 0) {
        fclose(f);
        return -1;
    }
    uint8_t *buf = malloc((size_t)sz);
    if (buf == NULL) {
        fclose(f);
        return -1;
    }
    if (fread(buf, 1, (size_t)sz, f) != (size_t)sz) {
        free(buf);
        fclose(f);
        return -1;
    }
    fclose(f);
    *out = buf;
    *out_len = (size_t)sz;
    return 0;
}

static double ref_plane_sum(const jxl_ref_image *ref, uint32_t plane) {
    size_t i;
    double sum;
    if (ref == NULL || !ref->valid || ref->planes == NULL || plane >= ref->num_planes ||
        ref->planes[plane] == NULL || ref->width == 0 || ref->height == 0) {
        return 0.0;
    }
    uint32_t w = ref->plane_w[plane] != 0 ? ref->plane_w[plane] : ref->width;
    sum = 0.0;
    size_t pixels = (size_t)w * (size_t)ref->height;
    for (i = 0; i < pixels; ++i) {
        float v = ref->planes[plane][i];
        sum += v < 0.0f ? -(double)v : (double)v;
    }
    return sum;
}

int main(void) {
    uint32_t i;
    char path[512];
    size_t input_len;
    jxl_allocator_state alloc;
    jxl_reference_store refs;
    jxl_progressive_lf_store lf_store;
    int any_ref;
    snprintf(path, sizeof(path), "%s/blendmodes_5/input.jxl", JXL_OXIDE_CONFORMANCE_DIR);

    uint8_t *input = NULL;
    input_len = 0;
    if (read_file(path, &input, &input_len) != 0) {
        fprintf(stderr, "failed to read %s\n", path);
        return 1;
    }

    jxl_context *library_ctx = NULL;
    JXL_TEST_ASSERT_EQ(jxl_context_create(NULL, &library_ctx), JXL_OK);

    jxl_allocator_init(&alloc, NULL);
    jxl_reference_store_init(&refs);
    jxl_progressive_lf_store_init(&lf_store);

    jxl_status_t st =
        jxl_decode_prerequisite_frames(library_ctx, &alloc, input, input_len, &refs, &lf_store, 0,
                                       NULL, 0);
    free(input);
    if (st != JXL_OK) {
        fprintf(stderr, "jxl_decode_prerequisite_frames failed: %d\n", (int)st);
        jxl_progressive_lf_store_free(&alloc, &lf_store);
        jxl_reference_store_free(&alloc, &refs);
        return 1;
    }

    any_ref = 0;
    for (i = 0; i < 4; ++i) {
        if (refs.slots[i].valid) {
            any_ref = 1;
            if (refs.slots[i].width == 0 || refs.slots[i].height == 0 ||
                refs.slots[i].num_planes == 0) {
                fprintf(stderr, "ref slot %u has invalid dimensions\n", i);
                jxl_progressive_lf_store_free(&alloc, &lf_store);
                jxl_reference_store_free(&alloc, &refs);
                return 1;
            }
            double sum = ref_plane_sum(&refs.slots[i], 0);
            if (sum <= 0.0) {
                fprintf(stderr, "ref slot %u plane 0 is empty (sum=%g)\n", i, sum);
                jxl_progressive_lf_store_free(&alloc, &lf_store);
                jxl_reference_store_free(&alloc, &refs);
                return 1;
            }
        }
    }

    if (!any_ref) {
        fprintf(stderr, "expected at least one composited reference slot for blendmodes_5\n");
        jxl_progressive_lf_store_free(&alloc, &lf_store);
        jxl_reference_store_free(&alloc, &refs);
        return 1;
    }

    jxl_progressive_lf_store_free(&alloc, &lf_store);
    jxl_reference_store_free(&alloc, &refs);
    jxl_context_destroy(library_ctx);
    return 0;
}
