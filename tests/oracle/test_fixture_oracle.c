// SPDX-License-Identifier: MIT OR Apache-2.0
#include "test_paths.h"

/*
 * Oracle progress test for crates/jxl-oxide-tests/decode/<fixture>.
 *
 * Returns 0 when C decode matches the Rust golden (keyframe 0).
 * Fixtures not in JXL_ORACLE_PASSING_FIXTURES are registered with WILL_FAIL as progress trackers.
 */
#include "golden_buf_zst.h"
#include "jxl_oxide/jxl_oxide.h"

#include <stdio.h>
#include "jxl_oxide/jxl_types.h"
#include <stdlib.h>
#include <string.h>


#ifndef JXL_ORACLE_FIXTURE_NAME
#define JXL_ORACLE_FIXTURE_NAME "grayalpha"
#endif

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
    jxl_golden_buf golden;
    size_t len;
    int n = snprintf(input_path, sizeof(input_path), "%s/%s/input.jxl",
                     JXL_OXIDE_FIXTURES_DIR, JXL_ORACLE_FIXTURE_NAME);
    if (n < 0 || (size_t)n >= sizeof(input_path)) {
        fprintf(stderr, "fixture path too long\n");
        return 1;
    }

    memset(&golden, 0, sizeof(golden));
    const int have_golden =
        jxl_golden_load_fixture(JXL_OXIDE_FIXTURES_DIR, JXL_ORACLE_FIXTURE_NAME, &golden) == 0;
    if (!have_golden) {
        return 1;
    }

    uint8_t *data = NULL;
    len = 0;
    if (read_file(input_path, &data, &len) != 0) {
        jxl_golden_free(&golden);
        return 1;
    }

    jxl_context *ctx = NULL;
    jxl_status_t status = jxl_context_create(NULL, &ctx);
    if (status != JXL_OK) {
        fprintf(stderr, "jxl_context_create: %s\n", jxl_status_string(status));
        free(data);
        jxl_golden_free(&golden);
        return 1;
    }

    jxl_decoder *dec = NULL;
    status = jxl_decoder_create(ctx, NULL, &dec);
    if (status != JXL_OK) {
        fprintf(stderr, "jxl_decoder_create: %s\n", jxl_status_string(status));
        free(data);
        jxl_golden_free(&golden);
        jxl_context_destroy(ctx);
        return 1;
    }

    status = jxl_decoder_feed(dec, data, len);
    free(data);
    if (status != JXL_OK) {
        fprintf(stderr, "jxl_decoder_feed: %s\n", jxl_status_string(status));
        jxl_decoder_destroy(ctx, dec);
        jxl_golden_free(&golden);
        jxl_context_destroy(ctx);
        return 1;
    }

    status = jxl_decoder_try_init(dec);
    if (status != JXL_OK) {
        fprintf(stderr, "jxl_decoder_try_init(%s): %s (%s)\n", JXL_ORACLE_FIXTURE_NAME,
                jxl_status_string(status), jxl_decoder_last_error(dec));
        jxl_decoder_destroy(ctx, dec);
        jxl_golden_free(&golden);
        jxl_context_destroy(ctx);
        return 1;
    }

    const jxl_image_header *header_ptr = jxl_decoder_header(dec);
    if (header_ptr == NULL) {
        fprintf(stderr, "missing image header after init (%s)\n", JXL_ORACLE_FIXTURE_NAME);
        jxl_decoder_destroy(ctx, dec);
        jxl_golden_free(&golden);
        jxl_context_destroy(ctx);
        return 1;
    }
    jxl_image_header header = *header_ptr;

    uint32_t num_keyframes = jxl_decoder_num_keyframes(dec);
    if (num_keyframes == 0) {
        fprintf(stderr, "no keyframes in %s\n", JXL_ORACLE_FIXTURE_NAME);
        jxl_decoder_destroy(ctx, dec);
        jxl_golden_free(&golden);
        jxl_context_destroy(ctx);
        return 1;
    }

    for (kf = 0; kf < num_keyframes; ++kf) {
        if (golden.pos >= golden.len || golden.data[golden.pos] == 0xff) {
            fprintf(stderr, "golden truncated before keyframe %u in %s\n", kf,
                    JXL_ORACLE_FIXTURE_NAME);
            jxl_decoder_destroy(ctx, dec);
            jxl_golden_free(&golden);
            jxl_context_destroy(ctx);
            return 1;
        }

        jxl_render *render = NULL;
        status = jxl_decoder_render_keyframe(ctx, dec, kf, &render);
        if (status != JXL_OK || render == NULL) {
            fprintf(stderr, "jxl_decoder_render_keyframe(%s, %u): %s (%s)\n",
                    JXL_ORACLE_FIXTURE_NAME, kf, jxl_status_string(status),
                    jxl_decoder_last_error(dec));
            jxl_decoder_destroy(ctx, dec);
            jxl_golden_free(&golden);
            jxl_context_destroy(ctx);
            return 1;
        }

        const int cmp =
            jxl_golden_compare_render(&golden, render, &header, JXL_ORACLE_FIXTURE_NAME);
        jxl_render_destroy(ctx, render);
        if (cmp != 0) {
            jxl_decoder_destroy(ctx, dec);
            jxl_golden_free(&golden);
            jxl_context_destroy(ctx);
            return 1;
        }
    }
    jxl_decoder_destroy(ctx, dec);
    jxl_golden_free(&golden);
    jxl_context_destroy(ctx);

    printf("oracle %s: golden match (%u keyframe(s))\n", JXL_ORACLE_FIXTURE_NAME, num_keyframes);
    return 0;
}
