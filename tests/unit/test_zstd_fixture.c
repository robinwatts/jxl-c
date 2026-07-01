// SPDX-License-Identifier: MIT OR Apache-2.0
#include "test_paths.h"

/* Smoke test: read Rust decode golden (.buf.zst) via libzstd. */
#include <zstd.h>

#include "jxl_oxide/jxl_types.h"
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

int main(void) {
    char path[1024];
    int n = snprintf(path, sizeof(path), "%s/grayalpha/output.buf.zst", JXL_OXIDE_FIXTURES_DIR);
    size_t compressed_len;
    if (n < 0 || (size_t)n >= sizeof(path)) {
        return 1;
    }

    uint8_t *compressed = NULL;
    compressed_len = 0;
    if (read_file(path, &compressed, &compressed_len) != 0) {
        fprintf(stderr, "missing %s\n", path);
        return 1;
    }

    unsigned long long frame_size = ZSTD_getFrameContentSize(compressed, compressed_len);
    if (frame_size == ZSTD_CONTENTSIZE_ERROR || frame_size == ZSTD_CONTENTSIZE_UNKNOWN) {
        fprintf(stderr, "invalid zstd frame in %s\n", path);
        free(compressed);
        return 1;
    }

    uint8_t *decompressed = malloc((size_t)frame_size);
    if (decompressed == NULL) {
        free(compressed);
        return 1;
    }

    size_t decoded =
        ZSTD_decompress(decompressed, (size_t)frame_size, compressed, compressed_len);
    free(compressed);
    if (ZSTD_isError(decoded)) {
        fprintf(stderr, "ZSTD_decompress: %s\n", ZSTD_getErrorName(decoded));
        free(decompressed);
        return 1;
    }
    if (decoded != (size_t)frame_size) {
        fprintf(stderr, "unexpected decompressed size %zu (expected %llu)\n", decoded,
                frame_size);
        free(decompressed);
        return 1;
    }

    if (decoded < 12) {
        fprintf(stderr, "golden too small\n");
        free(decompressed);
        return 1;
    }

    uint32_t width = (uint32_t)decompressed[0] | ((uint32_t)decompressed[1] << 8) |
                     ((uint32_t)decompressed[2] << 16) | ((uint32_t)decompressed[3] << 24);
    uint32_t height = (uint32_t)decompressed[4] | ((uint32_t)decompressed[5] << 8) |
                      ((uint32_t)decompressed[6] << 16) | ((uint32_t)decompressed[7] << 24);
    uint32_t channels = (uint32_t)decompressed[8] | ((uint32_t)decompressed[9] << 8) |
                        ((uint32_t)decompressed[10] << 16) |
                        ((uint32_t)decompressed[11] << 24);

    free(decompressed);

    if (width != 32 || height != 32 || channels != 2) {
        fprintf(stderr, "grayalpha golden header mismatch: %ux%u %u channels\n", width, height,
                channels);
        return 1;
    }

    printf("test_zstd_fixture: ok (zstd %s)\n", ZSTD_versionString());
    return 0;
}
