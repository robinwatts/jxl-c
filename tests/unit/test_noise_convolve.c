// SPDX-License-Identifier: MIT OR Apache-2.0
#include "test_paths.h"

#include "allocator.h"
#include "bitstream/bitstream.h"
#include "codestream_collect.h"
#include "frame/frame.h"
#include "frame/frame_header.h"
#include "image/image_internal.h"
#include "render/features/noise.h"

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

int main(void) {
    char path[512];
    size_t file_len;
    jxl_allocator_state alloc;
    size_t cs_len;
    jxl_bs bs;
    jxl_parsed_image_header parsed;
    jxl_frame frame;
    snprintf(path, sizeof(path), "%s/noise/input.jxl", JXL_OXIDE_CONFORMANCE_DIR);

    uint8_t *file = NULL;
    file_len = 0;
    if (read_file(path, &file, &file_len) != 0) {
        fprintf(stderr, "failed to read %s\n", path);
        return 1;
    }

    jxl_allocator_init(&alloc, NULL);
    uint8_t *cs = NULL;
    cs_len = 0;
    if (jxl_collect_codestream(&alloc, file, file_len, &cs, &cs_len) != JXL_OK) {
        free(file);
        return 1;
    }
    free(file);

    jxl_bs_init(&bs, cs, cs_len);
    memset(&parsed, 0, sizeof(parsed));
    if (jxl_image_header_parse(&bs, &parsed) != JXL_BS_OK) {
        jxl_free(&alloc, cs);
        return 1;
    }
    if (jxl_image_skip_post_header(&alloc, &bs, &parsed) != JXL_BS_OK) {
        jxl_free(&alloc, cs);
        return 1;
    }

    jxl_frame_init(&frame);
    if (jxl_frame_parse(&alloc, &bs, &parsed, &frame) != JXL_FRAME_OK) {
        jxl_frame_free(&alloc, &frame);
        jxl_free(&alloc, cs);
        return 1;
    }
    jxl_free(&alloc, cs);

    if (frame.header.width != 500u || frame.header.height != 606u) {
        fprintf(stderr, "unexpected frame size %ux%u\n", frame.header.width, frame.header.height);
        jxl_frame_free(&alloc, &frame);
        return 1;
    }

    const uint64_t expected = 0x448aad21af9005eaULL;
    uint64_t checksum = jxl_noise_planes_ch0_checksum(&frame.header, 1, 0);
    if (checksum != expected) {
        fprintf(stderr,
                "noise convolve checksum mismatch: got 0x%08lx%08lx expected 0x%08lx%08lx\n",
                (unsigned long)(checksum >> 32), (unsigned long)(checksum & 0xffffffffu),
                (unsigned long)(expected >> 32), (unsigned long)(expected & 0xffffffffu));
        jxl_frame_free(&alloc, &frame);
        return 1;
    }
    jxl_frame_free(&alloc, &frame);

    printf("noise convolve checksum ok\n");
    return 0;
}
