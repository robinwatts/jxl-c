// SPDX-License-Identifier: MIT OR Apache-2.0
#include "test_paths.h"

#include "allocator.h"
#include "bitstream/bitstream.h"
#include "codestream_collect.h"
#include "image/image_internal.h"
#include "jxl_oxide/jxl_oxide.h"
#include "test_helpers.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>


static int read_fixture(const char *path, uint8_t **out, size_t *out_len) {
    long sz;
    FILE *f = fopen(path, "rb");
    uint8_t *buf;
    if (f == NULL) {
        return -1;
    }
    if (fseek(f, 0, SEEK_END) != 0) {
        fclose(f);
        return -1;
    }
    sz = ftell(f);
    if (sz < 0) {
        fclose(f);
        return -1;
    }
    if (fseek(f, 0, SEEK_SET) != 0) {
        fclose(f);
        return -1;
    }
    buf = malloc((size_t)sz);
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

static int decode_fixture(jxl_context *ctx, const char *name) {
    char path[1024];
    size_t file_len;
    jxl_status_t st;
    uint8_t *file = NULL;
    file_len = 0;
    jxl_decoder *dec = NULL;
    jxl_render *render = NULL;

    snprintf(path, sizeof(path), "%s/benchmark-data/%s.jxl", JXL_OXIDE_FIXTURES_DIR, name);
    if (read_fixture(path, &file, &file_len) != 0) {
        fprintf(stderr, "%s: read failed\n", name);
        return 0;
    }

    if (jxl_decoder_create(ctx, NULL, &dec) != JXL_OK) {
        free(file);
        return 0;
    }
    st = jxl_decoder_feed(dec, file, file_len);
    free(file);
    if (st != JXL_OK) {
        fprintf(stderr, "%s: feed failed\n", name);
        jxl_decoder_destroy(ctx, dec);
        return 0;
    }
    st = jxl_decoder_try_init(dec);
    if (st != JXL_OK) {
        fprintf(stderr, "%s: try_init failed: %s\n", name, jxl_decoder_last_error(dec));
        jxl_decoder_destroy(ctx, dec);
        return 0;
    }
    st = jxl_decoder_render(ctx, dec, &render);
    if (st != JXL_OK || render == NULL) {
        fprintf(stderr, "%s: render failed: %s\n", name, jxl_decoder_last_error(dec));
        jxl_decoder_destroy(ctx, dec);
        return 0;
    }

    jxl_render_destroy(ctx, render);
    jxl_decoder_destroy(ctx, dec);
    return 1;
}

int main(void) {
    jxl_context *ctx = NULL;
    if (jxl_context_create(NULL, &ctx) != JXL_OK) {
        return 1;
    }

    if (!decode_fixture(ctx, "genshin-cafe.d2-e6-epf2") ||
        !decode_fixture(ctx, "genshin-cafe.d2-e6-epf3")) {
        jxl_context_destroy(ctx);
        return 1;
    }

    jxl_context_destroy(ctx);
    printf("test_genshin_cafe: ok\n");
    return 0;
}
