// SPDX-License-Identifier: MIT OR Apache-2.0
#include "golden_buf_zst.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef JXL_HAVE_ZSTD
#include <zstd.h>
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

static uint32_t read_u32_le(const uint8_t *p) {
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) |
           ((uint32_t)p[3] << 24);
}

static uint16_t float_sample_to_u16(float v) {
    float scaled = v * 65535.0f + 0.5f;
    if (scaled <= 0.0f) {
        return 0;
    }
    if (scaled >= 65535.0f) {
        return 65535;
    }
    return (uint16_t)scaled;
}

static int fixture_uses_vardct_threshold(const char *fixture_name) {
    if (fixture_name == NULL) {
        return 0;
    }
    if (strstr(fixture_name, "vardct") != NULL) {
        return 1;
    }
    return strcmp(fixture_name, "genshin_ycbcr_420") == 0 ||
           strcmp(fixture_name, "issue_425") == 0 ||
           strcmp(fixture_name, "starrail_jpegli_xyb") == 0;
}

static uint16_t peak_threshold(const char *fixture_name, uint32_t bit_depth) {
    uint32_t bd;
    if (fixture_uses_vardct_threshold(fixture_name)) {
        return (uint16_t)(0.004 * 65535.0 + 0.5);
    }
    bd = bit_depth > 0 ? bit_depth : 8;
    if (bd >= 14) {
        return 1;
    }
    return (uint16_t)(1u << (14u - bd));
}

int jxl_golden_load_file(const char *path, jxl_golden_buf *out) {
    size_t compressed_len;
    int size_known;
    if (out == NULL) {
        return -1;
    }
    memset(out, 0, sizeof(*out));

#ifndef JXL_HAVE_ZSTD
    fprintf(stderr, "libzstd not available; cannot load golden %s\n", path);
    return -1;
#else
    uint8_t *compressed = NULL;
    compressed_len = 0;
    if (read_file(path, &compressed, &compressed_len) != 0) {
        return -1;
    }

    unsigned long long frame_size = ZSTD_getFrameContentSize(compressed, compressed_len);
    if (frame_size == ZSTD_CONTENTSIZE_ERROR) {
        fprintf(stderr, "invalid zstd frame in %s\n", path);
        free(compressed);
        return -1;
    }
    size_known = 1;
    if (frame_size == ZSTD_CONTENTSIZE_UNKNOWN) {
        size_known = 0;
        frame_size = ZSTD_decompressBound(compressed, compressed_len);
        if (frame_size == 0) {
            fprintf(stderr, "cannot bound zstd frame in %s\n", path);
            free(compressed);
            return -1;
        }
    }

    uint8_t *decompressed = malloc((size_t)frame_size);
    if (decompressed == NULL) {
        free(compressed);
        return -1;
    }

    size_t decoded =
        ZSTD_decompress(decompressed, (size_t)frame_size, compressed, compressed_len);
    free(compressed);
    if (ZSTD_isError(decoded)) {
        fprintf(stderr, "ZSTD_decompress(%s): %s\n", path, ZSTD_getErrorName(decoded));
        free(decompressed);
        return -1;
    }
    if (size_known && decoded != (size_t)frame_size) {
        fprintf(stderr, "unexpected decompressed size for %s\n", path);
        free(decompressed);
        return -1;
    }
    if (decoded < 12) {
        fprintf(stderr, "golden too small in %s\n", path);
        free(decompressed);
        return -1;
    }

    out->data = decompressed;
    out->len = decoded;
    out->pos = 12;
    out->width = read_u32_le(decompressed + 0);
    out->height = read_u32_le(decompressed + 4);
    out->channels = read_u32_le(decompressed + 8);
    return 0;
#endif
}

static int trim_newline(char *s) {
    if (s == NULL) {
        return -1;
    }
    size_t n = strlen(s);
    while (n > 0 && (s[n - 1] == '\n' || s[n - 1] == '\r' || s[n - 1] == ' ')) {
        s[n - 1] = '\0';
        --n;
    }
    return n > 0 ? 0 : -1;
}

static int read_url_from_file(const char *path, char *url, size_t url_cap) {
    FILE *f = fopen(path, "rb");
    if (f == NULL) {
        return -1;
    }
    if (fgets(url, (int)url_cap, f) == NULL) {
        fclose(f);
        return -1;
    }
    fclose(f);
    return trim_newline(url);
}

static int fetch_url_to_file(const char *url, const char *dest) {
    char cmd[2048];
    int n = snprintf(cmd, sizeof(cmd), "curl -sfL '%s' -o '%s'", url, dest);
    if (n < 0 || (size_t)n >= sizeof(cmd)) {
        return -1;
    }
    return system(cmd) == 0 ? 0 : -1;
}

int jxl_golden_load_fixture(const char *fixtures_dir, const char *fixture_name,
                            jxl_golden_buf *out) {
    char golden_path[1024];
    char url_path[1024];
    char url[512];
    if (fixtures_dir == NULL || fixture_name == NULL || out == NULL) {
        return -1;
    }
    int n = snprintf(golden_path, sizeof(golden_path), "%s/%s/output.buf.zst", fixtures_dir,
                     fixture_name);
    if (n < 0 || (size_t)n >= sizeof(golden_path)) {
        return -1;
    }
    FILE *probe = fopen(golden_path, "rb");
    if (probe != NULL) {
        fclose(probe);
        return jxl_golden_load_file(golden_path, out);
    }

    n = snprintf(url_path, sizeof(url_path), "%s/%s/output.buf.zst.url", fixtures_dir,
                 fixture_name);
    if (n < 0 || (size_t)n >= sizeof(url_path)) {
        return -1;
    }
    if (read_url_from_file(url_path, url, sizeof(url)) != 0) {
        fprintf(stderr, "missing golden %s (no local .zst or .url)\n", golden_path);
        return -1;
    }
    if (fetch_url_to_file(url, golden_path) != 0) {
        fprintf(stderr, "failed to fetch golden from %s\n", url);
        return -1;
    }
    return jxl_golden_load_file(golden_path, out);
}

void jxl_golden_free(jxl_golden_buf *golden) {
    if (golden == NULL) {
        return;
    }
    free(golden->data);
    memset(golden, 0, sizeof(*golden));
}

int jxl_golden_compare_render(jxl_golden_buf *golden, const jxl_render *render,
                              const jxl_image_header *header, const char *fixture_name) {
                                  uint32_t plane;
    if (golden == NULL || golden->data == NULL || render == NULL || header == NULL) {
        return -1;
    }

    if (golden->pos >= golden->len) {
        fprintf(stderr, "golden stream truncated before keyframe marker\n");
        return -1;
    }

    uint8_t marker = golden->data[golden->pos++];
    if (marker != 0) {
        fprintf(stderr, "unexpected golden keyframe marker %u\n", marker);
        return 1;
    }

    uint32_t width = jxl_render_width(render);
    uint32_t height = jxl_render_height(render);
    uint32_t planes = jxl_render_num_planes(render);

    if (width != golden->width || height != golden->height) {
        fprintf(stderr, "render size %ux%u != golden %ux%u\n", width, height, golden->width,
                golden->height);
        return 1;
    }
    if (planes != golden->channels) {
        fprintf(stderr, "render planes %u != golden channels %u\n", planes, golden->channels);
        return 1;
    }

    uint16_t threshold = peak_threshold(fixture_name, header->bit_depth);
    size_t pixels = (size_t)width * (size_t)height;

    for (plane = 0; plane < planes; ++plane) {
        size_t i;
        const float *actual = jxl_render_plane(render, plane);
        if (actual == NULL) {
            fprintf(stderr, "render plane %u is null\n", plane);
            return -1;
        }
        if (golden->pos + pixels * 2 > golden->len) {
            fprintf(stderr, "golden stream truncated in plane %u\n", plane);
            return -1;
        }

        const uint8_t *expected_bytes = golden->data + golden->pos;
        for (i = 0; i < pixels; ++i) {
            uint16_t expected =
                (uint16_t)expected_bytes[i * 2] | ((uint16_t)expected_bytes[i * 2 + 1] << 8);
            uint16_t sample = float_sample_to_u16(actual[i]);
            uint16_t diff = sample > expected ? (uint16_t)(sample - expected)
                                              : (uint16_t)(expected - sample);
            if (diff >= threshold) {
                fprintf(stderr,
                        "%s plane %u pixel %zu: actual=%u expected=%u diff=%u threshold=%u\n",
                        fixture_name != NULL ? fixture_name : "fixture", plane, i, sample,
                        expected, diff, threshold);
                return 1;
            }
        }
        golden->pos += pixels * 2;
    }

    return 0;
}
