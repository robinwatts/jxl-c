// SPDX-License-Identifier: MIT OR Apache-2.0
#include "test_paths.h"

/*
 * Conformance progress test for crates/jxl-oxide-tests/conformance/testcases/<fixture>.
 *
 * Compares every keyframe against cached reference_image.npy from the conformance corpus.
 * Fixtures not in JXL_CONFORMANCE_PASSING_FIXTURES are registered with WILL_FAIL.
 */
#include "conformance_compare.h"
#include "conformance_npy.h"
#include "jxl_oxide/jxl_oxide.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>


#ifndef JXL_CONFORMANCE_CASES_INC
#define JXL_CONFORMANCE_CASES_INC "conformance_cases.inc"
#endif

#include JXL_CONFORMANCE_CASES_INC

#ifndef JXL_CONFORMANCE_FIXTURE_NAME
#define JXL_CONFORMANCE_FIXTURE_NAME "lz77_flower"
#endif

static const jxl_conformance_case *find_case(const char *name) {
    size_t i;
    for (i = 0; i < k_conformance_case_count; ++i) {
        if (strcmp(k_conformance_cases[i].name, name) == 0) {
            return &k_conformance_cases[i];
        }
    }
    return NULL;
}

static int read_file(const char *path, uint8_t **out_data, size_t *out_len) {
    FILE *f = fopen(path, "rb");
    if (f == NULL) {
        fprintf(stderr, "failed to open %s\n", path);
        return -1;
    }
    if (fseek(f, 0, SEEK_END) != 0) {
        fclose(f);
        return -1;
    }
    long size = ftell(f);
    if (size < 0) {
        fclose(f);
        return -1;
    }
    if (fseek(f, 0, SEEK_SET) != 0) {
        fclose(f);
        return -1;
    }
    uint8_t *data = malloc((size_t)size);
    if (data == NULL) {
        fclose(f);
        return -1;
    }
    if (fread(data, 1, (size_t)size, f) != (size_t)size) {
        free(data);
        fclose(f);
        return -1;
    }
    fclose(f);
    *out_data = data;
    *out_len = (size_t)size;
    return 0;
}

int main(void) {
    uint32_t kf;
    char input_path[1024];
    char npy_path[1024];
    size_t len;
    jxl_conformance_npy reference;
    const jxl_conformance_case *expect = find_case(JXL_CONFORMANCE_FIXTURE_NAME);
    if (expect == NULL) {
        fprintf(stderr, "unknown conformance case %s\n", JXL_CONFORMANCE_FIXTURE_NAME);
        return 1;
    }

    int n = snprintf(input_path, sizeof(input_path), "%s/%s/input.jxl",
                     JXL_OXIDE_CONFORMANCE_DIR, expect->name);
    if (n < 0 || (size_t)n >= sizeof(input_path)) {
        return 1;
    }
    if (jxl_conformance_cache_npy_path(expect->npy_hash, npy_path, sizeof(npy_path)) != 0) {
        return 1;
    }

    uint8_t *data = NULL;
    len = 0;
    if (read_file(input_path, &data, &len) != 0) {
        return 1;
    }

    jxl_context *ctx = NULL;
    if (jxl_context_create(NULL, &ctx) != JXL_OK) {
        free(data);
        return 1;
    }

    jxl_decoder *dec = NULL;
    jxl_status_t status = jxl_decoder_create(ctx, NULL, &dec);
    if (status != JXL_OK) {
        jxl_context_destroy(ctx);
        free(data);
        return 1;
    }

    status = jxl_decoder_feed(dec, data, len);
    free(data);
    if (status != JXL_OK) {
        fprintf(stderr, "%s: feed failed: %s\n", expect->name, jxl_status_string(status));
        jxl_decoder_destroy(ctx, dec);
        jxl_context_destroy(ctx);
        return 1;
    }

    status = jxl_decoder_try_init(dec);
    if (expect->expect_animation_reject) {
        if (status != JXL_ERROR_ANIMATION_NOT_SUPPORTED) {
            fprintf(stderr, "%s: expected animation rejection, got %s (%s)\n", expect->name,
                    jxl_status_string(status), jxl_decoder_last_error(dec));
            jxl_decoder_destroy(ctx, dec);
            jxl_context_destroy(ctx);
            return 1;
        }
        printf("conformance %s: animation rejected as expected\n", expect->name);
        jxl_decoder_destroy(ctx, dec);
        jxl_context_destroy(ctx);
        return 0;
    }

    if (status != JXL_OK) {
        fprintf(stderr, "%s: init failed: %s (%s)\n", expect->name, jxl_status_string(status),
                jxl_decoder_last_error(dec));
        jxl_decoder_destroy(ctx, dec);
        jxl_context_destroy(ctx);
        return 1;
    }

    if (expect->icc_hash[0] != '\0') {
        char icc_path[1024];
        size_t icc_len;
        if (jxl_conformance_cache_icc_path(expect->icc_hash, icc_path, sizeof(icc_path)) != 0) {
            jxl_decoder_destroy(ctx, dec);
            jxl_context_destroy(ctx);
            return 1;
        }
        uint8_t *icc = NULL;
        icc_len = 0;
        if (jxl_conformance_file_load(icc_path, &icc, &icc_len) != 0) {
            fprintf(stderr,
                    "%s: missing reference icc at %s (run gen_conformance_cases.py to prefetch)\n",
                    expect->name, icc_path);
            jxl_decoder_destroy(ctx, dec);
            jxl_context_destroy(ctx);
            return 1;
        }
        status = jxl_decoder_request_icc(dec, icc, icc_len);
        free(icc);
        if (status != JXL_OK) {
            fprintf(stderr, "%s: request_icc failed: %s (%s)\n", expect->name,
                    jxl_status_string(status), jxl_decoder_last_error(dec));
            jxl_decoder_destroy(ctx, dec);
            jxl_context_destroy(ctx);
            return 1;
        }
    }

    memset(&reference, 0, sizeof(reference));
    if (jxl_conformance_npy_load(npy_path, &reference) != 0) {
        fprintf(stderr,
                "%s: missing reference npy at %s (run gen_conformance_cases.py to prefetch)\n",
                expect->name, npy_path);
        jxl_decoder_destroy(ctx, dec);
        jxl_context_destroy(ctx);
        return 1;
    }

    jxl_render *render = NULL;
    uint32_t num_keyframes = jxl_decoder_num_keyframes(dec);
    if (num_keyframes == 0) {
        fprintf(stderr, "%s: no keyframes found\n", expect->name);
        jxl_decoder_destroy(ctx, dec);
        jxl_conformance_npy_free(&reference);
        jxl_context_destroy(ctx);
        return 1;
    }
    if (reference.num_frames != num_keyframes) {
        fprintf(stderr, "%s: decoder has %u keyframes, reference has %u\n", expect->name,
                num_keyframes, reference.num_frames);
        jxl_decoder_destroy(ctx, dec);
        jxl_conformance_npy_free(&reference);
        jxl_context_destroy(ctx);
        return 1;
    }

    for (kf = 0; kf < num_keyframes; ++kf) {
        status = jxl_decoder_render_keyframe(ctx, dec, kf, &render);
        if (status != JXL_OK || render == NULL) {
            const char *detail = jxl_decoder_last_error(dec);
            fprintf(stderr, "%s keyframe %u: render failed: %s (%s)\n", expect->name, kf,
                    jxl_status_string(status), detail != NULL ? detail : "");
            jxl_decoder_destroy(ctx, dec);
            jxl_conformance_npy_free(&reference);
            jxl_context_destroy(ctx);
            return 1;
        }
        if (jxl_render_keyframe_index(render) != kf) {
            fprintf(stderr, "%s keyframe %u: render index mismatch %u\n", expect->name, kf,
                    jxl_render_keyframe_index(render));
            jxl_render_destroy(ctx, render);
            jxl_decoder_destroy(ctx, dec);
            jxl_conformance_npy_free(&reference);
            jxl_context_destroy(ctx);
            return 1;
        }

        const int cmp =
            jxl_conformance_compare_render(&reference, kf, render, expect->peak_error, expect->rmse,
                                           expect->name);
        if (cmp != 0) {
            fprintf(stderr, "%s: mismatch at keyframe %u\n", expect->name, kf);
        }
        jxl_render_destroy(ctx, render);
        render = NULL;
        if (cmp != 0) {
            jxl_decoder_destroy(ctx, dec);
            jxl_conformance_npy_free(&reference);
            jxl_context_destroy(ctx);
            return 1;
        }
    }
    jxl_decoder_destroy(ctx, dec);
    jxl_conformance_npy_free(&reference);
    jxl_context_destroy(ctx);

    printf("conformance %s: reference match (%u keyframe(s))\n", expect->name, num_keyframes);
    return 0;
}
