// SPDX-License-Identifier: MIT OR Apache-2.0
#include "jxl_fuzz_decode.h"

#include "jxl_oxide/jxl_oxide.h"

void jxl_fuzz_decode(const uint8_t *data, size_t len, uint32_t dim_limit) {
    jxl_context *ctx = NULL;
    jxl_decoder *dec = NULL;
    jxl_status_t status;
    size_t pos = 0;

    if (data == NULL && len != 0) {
        return;
    }
    if (jxl_context_create(NULL, &ctx) != JXL_OK) {
        return;
    }
    if (jxl_decoder_create(ctx, NULL, &dec) != JXL_OK) {
        jxl_context_destroy(ctx);
        return;
    }

    while (pos < len) {
        size_t chunk = len - pos;
        if (pos == 0 && len > 1) {
            chunk = ((size_t)data[0] % (len - 1)) + 1;
        }
        status = jxl_decoder_feed(dec, data + pos, chunk);
        pos += chunk;
        if (status != JXL_OK && status != JXL_NEED_MORE_DATA) {
            break;
        }
    }

    status = jxl_decoder_try_init(dec);
    if (status == JXL_OK) {
        const jxl_image_header *header = jxl_decoder_header(dec);
        if (header != NULL) {
            uint32_t max_dim =
                header->width > header->height ? header->width : header->height;
            if (max_dim <= dim_limit) {
                uint32_t num_keyframes = jxl_decoder_num_keyframes(dec);
                uint32_t kf;
                for (kf = 0; kf < num_keyframes; ++kf) {
                    jxl_render *render = NULL;
                    if (jxl_decoder_render_keyframe(ctx, dec, kf, &render) == JXL_OK &&
                        render != NULL) {
                        jxl_render_destroy(ctx, render);
                    }
                }
            }
        }
    }

    jxl_decoder_destroy(ctx, dec);
    jxl_context_destroy(ctx);
}
