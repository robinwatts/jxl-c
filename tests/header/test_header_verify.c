// SPDX-License-Identifier: MIT OR Apache-2.0
#include "test_paths.h"

#include "jxl_oxide/jxl_oxide.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>


#include "header_fields.inc"

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
    size_t i;
    int failed;
    jxl_context *ctx = NULL;
    if (jxl_context_create(NULL, &ctx) != JXL_OK) {
        fprintf(stderr, "jxl_context_create failed\n");
        return 1;
    }

    failed = 0;
    for (i = 0; i < k_header_field_count; ++i) {
        char path[1024];
        size_t file_len;
        snprintf(path, sizeof(path), "%s/%s/input.jxl", JXL_OXIDE_FIXTURES_DIR,
                 k_header_fields[i].name);

        uint8_t *file = NULL;
        file_len = 0;
        if (read_file(path, &file, &file_len) != 0) {
            fprintf(stderr, "missing %s\n", path);
            failed = 1;
            continue;
        }

        jxl_decoder *dec = NULL;
        if (jxl_decoder_create(ctx, NULL, &dec) != JXL_OK) {
            free(file);
            failed = 1;
            continue;
        }
        if (jxl_decoder_feed(dec, file, file_len) != JXL_OK) {
            fprintf(stderr, "%s: feed failed\n", k_header_fields[i].name);
            failed = 1;
            jxl_decoder_destroy(ctx, dec);
            free(file);
            continue;
        }
        free(file);

        jxl_status_t st = jxl_decoder_try_init(dec);
        if (st != JXL_OK) {
            fprintf(stderr, "%s: try_init: %s\n", k_header_fields[i].name,
                    jxl_status_string(st));
            failed = 1;
            jxl_decoder_destroy(ctx, dec);
            continue;
        }

        const jxl_image_header *hdr = jxl_decoder_header(dec);
        if (hdr == NULL) {
            fprintf(stderr, "%s: no header\n", k_header_fields[i].name);
            failed = 1;
            jxl_decoder_destroy(ctx, dec);
            continue;
        }

        if (hdr->width != k_header_fields[i].width || hdr->height != k_header_fields[i].height ||
            hdr->bit_depth != k_header_fields[i].bit_depth ||
            hdr->num_extra_channels != k_header_fields[i].num_extra ||
            hdr->have_animation != k_header_fields[i].have_animation) {
            fprintf(stderr,
                    "%s: mismatch got %ux%u bd=%u extra=%u anim=%d expected %ux%u bd=%u "
                    "extra=%u anim=%d\n",
                    k_header_fields[i].name, hdr->width, hdr->height, hdr->bit_depth,
                    hdr->num_extra_channels, hdr->have_animation, k_header_fields[i].width,
                    k_header_fields[i].height, k_header_fields[i].bit_depth,
                    k_header_fields[i].num_extra, k_header_fields[i].have_animation);
            failed = 1;
        }
        jxl_decoder_destroy(ctx, dec);
    }

    jxl_context_destroy(ctx);

    if (failed) {
        return 1;
    }
    printf("test_header_verify: ok (%zu fixtures)\n", k_header_field_count);
    return 0;
}
