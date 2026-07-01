// SPDX-License-Identifier: MIT OR Apache-2.0
#include "test_paths.h"

#include "jxl_oxide/jxl_oxide.h"

#include <assert.h>
#include "test_helpers.h"
#include <stdio.h>
#include <stdlib.h>


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

static void test_animation_newtons_cradle(void) {
    uint32_t kf;
    char path[1024];
    size_t len;
    int n = snprintf(path, sizeof(path), "%s/animation_newtons_cradle/input.jxl",
                     JXL_OXIDE_CONFORMANCE_DIR);
    assert(n > 0 && (size_t)n < sizeof(path));

    uint8_t *data = NULL;
    len = 0;
    if (read_file(path, &data, &len) != 0) {
        printf("test_decoder_animation: skip (no fixture at %s)\n", path);
        return;
    }

    jxl_context *ctx = NULL;
    JXL_TEST_ASSERT_EQ(jxl_context_create(NULL, &ctx), JXL_OK);

    jxl_decoder *dec = NULL;
    JXL_TEST_ASSERT_EQ(jxl_decoder_create(ctx, NULL, &dec), JXL_OK);
    JXL_TEST_ASSERT_EQ(jxl_decoder_feed(dec, data, len), JXL_OK);
    free(data);

    JXL_TEST_ASSERT_EQ(jxl_decoder_try_init(dec), JXL_OK);
    const jxl_image_header *hdr = jxl_decoder_header(dec);
    assert(hdr != NULL && hdr->have_animation);

    uint32_t num_kf = jxl_decoder_num_keyframes(dec);
    assert(num_kf == 36);

    for (kf = 0; kf < num_kf; ++kf) {
        jxl_render *render = NULL;
        JXL_TEST_ASSERT_EQ(jxl_decoder_render_keyframe(ctx, dec, kf, &render), JXL_OK);
        assert(render != NULL);
        assert(jxl_render_keyframe_index(render) == kf);
        assert(jxl_render_width(render) > 0);
        assert(jxl_render_height(render) > 0);
        jxl_render_destroy(ctx, render);
    }

    jxl_decoder_destroy(ctx, dec);
    jxl_context_destroy(ctx);
}

int main(void) {
    test_animation_newtons_cradle();
    printf("test_decoder_animation: ok\n");
    return 0;
}
