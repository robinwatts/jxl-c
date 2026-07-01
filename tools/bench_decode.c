// SPDX-License-Identifier: MIT OR Apache-2.0
/*
 * Decode benchmark — mirrors crates/jxl-oxide-tests/benches/decode.rs fixtures.
 *
 * Usage: bench_decode <file.jxl> [iterations]
 */
#define _POSIX_C_SOURCE 200809L

#define _CRT_SECURE_NO_WARNINGS // Shut up, MSVC

#include "jxl_oxide/jxl_oxide.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

static double now_seconds(void) {
#ifdef _WIN32
    return clock() / (double)CLOCKS_PER_SEC;
#else
    struct timespec ts;
    if (clock_gettime(CLOCK_MONOTONIC, &ts) != 0) {
        return 0.0;
    }
    return (double)ts.tv_sec + (double)ts.tv_nsec * 1e-9;
#endif
}

static int read_file(const char *path, uint8_t **out_data, size_t *out_len) {
    FILE *f = fopen(path, "rb");
    long size;
    uint8_t *data;

    if (f == NULL) {
        return -1;
    }
    if (fseek(f, 0, SEEK_END) != 0) {
        fclose(f);
        return -1;
    }
    size = ftell(f);
    if (size < 0) {
        fclose(f);
        return -1;
    }
    if (fseek(f, 0, SEEK_SET) != 0) {
        fclose(f);
        return -1;
    }
    data = malloc((size_t)size);
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

static int decode_once(jxl_context *ctx, const uint8_t *data, size_t len, const char *label) {
    jxl_decoder *dec = NULL;
    jxl_status_t status;
    jxl_render *render = NULL;
    int ok = 0;

    if (jxl_decoder_create(ctx, NULL, &dec) != JXL_OK) {
        if (label != NULL) {
            fprintf(stderr, "%s: jxl_decoder_create failed\n", label);
        }
        return 0;
    }
    status = jxl_decoder_feed(dec, data, len);
    if (status != JXL_OK) {
        if (label != NULL) {
            fprintf(stderr, "%s: jxl_decoder_feed: %s (%s)\n", label, jxl_status_string(status),
                    jxl_decoder_last_error(dec));
        }
        jxl_decoder_destroy(ctx, dec);
        return 0;
    }
    status = jxl_decoder_try_init(dec);
    if (status != JXL_OK) {
        if (label != NULL) {
            fprintf(stderr, "%s: jxl_decoder_try_init: %s (%s)\n", label,
                    jxl_status_string(status), jxl_decoder_last_error(dec));
        }
        jxl_decoder_destroy(ctx, dec);
        return 0;
    }
    status = jxl_decoder_render(ctx, dec, &render);
    if (status != JXL_OK || render == NULL) {
        if (label != NULL) {
            fprintf(stderr, "%s: jxl_decoder_render: %s (%s)\n", label,
                    jxl_status_string(status), jxl_decoder_last_error(dec));
        }
        jxl_decoder_destroy(ctx, dec);
        return 0;
    }
    ok = 1;
    jxl_render_destroy(ctx, render);
    jxl_decoder_destroy(ctx, dec);
    return ok;
}

int main(int argc, char **argv) {
    const char *path;
    int iterations = 5;
    uint8_t *data = NULL;
    size_t len = 0;
    jxl_context *ctx = NULL;
    double t0;
    double t1;
    int i;
    int successes = 0;
    uint64_t pixels = 0;

    if (argc < 2) {
        fprintf(stderr, "usage: %s <file.jxl> [iterations]\n", argv[0]);
        return 1;
    }
    path = argv[1];
    if (argc >= 3) {
        iterations = atoi(argv[2]);
        if (iterations < 1) {
            iterations = 1;
        }
    }

    if (read_file(path, &data, &len) != 0) {
        fprintf(stderr, "failed to read %s\n", path);
        return 1;
    }
    if (jxl_context_create(NULL, &ctx) != JXL_OK) {
        fprintf(stderr, "jxl_context_create failed\n");
        free(data);
        return 1;
    }

    if (!decode_once(ctx, data, len, path)) {
        fprintf(stderr, "warm-up decode failed for %s\n", path);
        jxl_context_destroy(ctx);
        free(data);
        return 1;
    }

    {
        jxl_decoder *dec = NULL;
        jxl_render *render = NULL;
        if (jxl_decoder_create(ctx, NULL, &dec) == JXL_OK &&
            jxl_decoder_feed(dec, data, len) == JXL_OK &&
            jxl_decoder_try_init(dec) == JXL_OK &&
            jxl_decoder_render(ctx, dec, &render) == JXL_OK && render != NULL) {
            pixels = (uint64_t)jxl_render_width(render) * (uint64_t)jxl_render_height(render);
            jxl_render_destroy(ctx, render);
        }
        if (dec != NULL) {
            jxl_decoder_destroy(ctx, dec);
        }
    }

    t0 = now_seconds();
    for (i = 0; i < iterations; ++i) {
        if (decode_once(ctx, data, len, NULL)) {
            successes++;
        }
    }
    t1 = now_seconds();

    jxl_context_destroy(ctx);
    free(data);

    if (successes == 0) {
        fprintf(stderr, "all iterations failed\n");
        return 1;
    }

    {
        double elapsed = t1 - t0;
        double mpix_per_s = 0.0;
        if (pixels > 0 && elapsed > 0.0) {
            mpix_per_s = ((double)pixels * (double)successes) / elapsed / 1e6;
        }
        printf("%s: %d/%d ok, %.3f s total, %.2f Mpix/s\n", path, successes, iterations,
               elapsed, mpix_per_s);
    }
    return 0;
}
