// SPDX-License-Identifier: MIT OR Apache-2.0
#include "test_paths.h"

#include "conformance_npy.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>


static int read_file(const char *path, uint8_t **out_data, size_t *out_len) {
    FILE *f = fopen(path, "rb");
    if (f == NULL) {
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

int jxl_conformance_cache_npy_path(const char *hash, char *out, size_t out_len) {
    if (hash == NULL || out == NULL || out_len == 0) {
        return -1;
    }
    int n = snprintf(out, out_len, "%s/%s.npy", JXL_OXIDE_CACHE_DIR, hash);
    if (n < 0 || (size_t)n >= out_len) {
        return -1;
    }
    return 0;
}

int jxl_conformance_cache_icc_path(const char *hash, char *out, size_t out_len) {
    if (hash == NULL || out == NULL || out_len == 0) {
        return -1;
    }
    int n = snprintf(out, out_len, "%s/%s.icc", JXL_OXIDE_CACHE_DIR, hash);
    if (n < 0 || (size_t)n >= out_len) {
        return -1;
    }
    return 0;
}

int jxl_conformance_file_load(const char *path, uint8_t **out_data, size_t *out_len) {
    if (path == NULL || out_data == NULL || out_len == NULL) {
        return -1;
    }
    return read_file(path, out_data, out_len);
}

int jxl_conformance_npy_load(const char *path, jxl_conformance_npy *out) {
    size_t len;
    size_t header_end;
    unsigned long num_frames;
    size_t payload;
    if (path == NULL || out == NULL) {
        return -1;
    }
    memset(out, 0, sizeof(*out));

    uint8_t *data = NULL;
    len = 0;
    if (read_file(path, &data, &len) != 0) {
        fprintf(stderr, "failed to open npy %s\n", path);
        return -1;
    }
    if (len < 10 || memcmp(data, "\x93NUMPY", 6) != 0) {
        fprintf(stderr, "invalid npy magic in %s\n", path);
        free(data);
        return -1;
    }

    uint16_t meta_len = (uint16_t)data[8] | ((uint16_t)data[9] << 8);
    header_end = 10u + meta_len;
    if (header_end > len) {
        fprintf(stderr, "truncated npy header in %s\n", path);
        free(data);
        return -1;
    }

    /* Shape is (H, W, C) or (frames, H, W, C); keyframe 0 only. */
    const char *shape_tag = "'shape': (";
    const char *header = (const char *)(data + 10);
    const char *shape = strstr(header, shape_tag);
    if (shape == NULL) {
        fprintf(stderr, "missing shape in npy header %s\n", path);
        free(data);
        return -1;
    }
    shape += strlen(shape_tag);
    unsigned long d0 = 0, d1 = 0, d2 = 0, d3 = 0;
    int dims = sscanf(shape, "%lu, %lu, %lu, %lu", &d0, &d1, &d2, &d3);
    unsigned long h = 0, w = 0, c = 1;
    num_frames = 1;
    if (dims == 4) {
        num_frames = d0;
        h = d1;
        w = d2;
        c = d3;
    } else if (dims == 3) {
        h = d0;
        w = d1;
        c = d2;
    } else if (dims == 2) {
        h = d0;
        w = d1;
        c = 1;
    } else {
        fprintf(stderr, "failed to parse npy shape in %s\n", path);
        free(data);
        return -1;
    }

    payload = len - header_end;
    size_t frame_samples = (size_t)h * (size_t)w * (size_t)c;
    size_t total_samples = frame_samples * (size_t)num_frames;
    if (payload < total_samples * sizeof(float)) {
        fprintf(stderr, "npy payload too small in %s\n", path);
        free(data);
        return -1;
    }

    float *fb = malloc(total_samples * sizeof(float));
    if (fb == NULL) {
        free(data);
        return -1;
    }
    memcpy(fb, data + header_end, total_samples * sizeof(float));
    free(data);

    out->samples = fb;
    out->height = (uint32_t)h;
    out->width = (uint32_t)w;
    out->channels = (uint32_t)c;
    out->num_frames = (uint32_t)num_frames;
    return 0;
}

void jxl_conformance_npy_free(jxl_conformance_npy *npy) {
    if (npy == NULL) {
        return;
    }
    free(npy->samples);
    memset(npy, 0, sizeof(*npy));
}
