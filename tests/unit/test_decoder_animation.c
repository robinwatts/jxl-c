// SPDX-License-Identifier: MIT OR Apache-2.0
#include "test_paths.h"

#include "jxl/decode.h"

#include <assert.h>
#include "test_helpers.h"
#include <math.h>
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

static int rational_near_seconds(uint32_t numer, uint32_t denom, double seconds, double tol) {
    if (denom == 0) {
        return 0;
    }
    double got = (double)numer / (double)denom;
    return fabs(got - seconds) <= tol;
}

static void test_still_image_animation_unsupported(void) {
    char path[1024];
    int n = snprintf(path, sizeof(path), "%s/grayalpha/input.jxl", JXL_C_FIXTURES_DIR);
    assert(n > 0 && (size_t)n < sizeof(path));

    uint8_t *data = NULL;
    size_t len = 0;
    if (read_file(path, &data, &len) != 0) {
        printf("test_decoder_animation: skip still-image timing test (no fixture at %s)\n", path);
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
    assert(hdr != NULL && !hdr->have_animation);

    jxl_animation_header anim;
    JXL_TEST_ASSERT_EQ(jxl_decoder_animation(dec, &anim), JXL_ERROR_UNSUPPORTED);

    jxl_decoder_destroy(ctx, dec);
    jxl_context_destroy(ctx);
}

static void test_animation_newtons_cradle(void) {
    uint32_t kf;
    char path[1024];
    size_t len;
    int n = snprintf(path, sizeof(path), "%s/animation_newtons_cradle/input.jxl",
                     JXL_C_CONFORMANCE_DIR);
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

    jxl_animation_header anim;
    JXL_TEST_ASSERT_EQ(jxl_decoder_animation(dec, &anim), JXL_OK);
    assert(anim.tps_numerator == 100);
    assert(anim.tps_denominator == 1);
    assert(anim.num_loops == 0);
    assert(!anim.have_timecodes);

    uint32_t num_kf = jxl_decoder_num_keyframes(dec);
    assert(num_kf == 36);

    static const double expected_durations[] = {
        0.05, 0.02, 0.02, 0.02, 0.02, 0.02, 0.02, 0.02, 0.02, 0.04,
        0.02, 0.02, 0.02, 0.02, 0.02, 0.02, 0.02, 0.02, 0.05, 0.02,
        0.02, 0.02, 0.02, 0.02, 0.02, 0.02, 0.02, 0.04, 0.02, 0.02,
        0.02, 0.02, 0.02, 0.02, 0.02, 0.02,
    };
    assert(num_kf == (uint32_t)(sizeof(expected_durations) / sizeof(expected_durations[0])));

    for (kf = 0; kf < num_kf; ++kf) {
        jxl_render *render = NULL;
        JXL_TEST_ASSERT_EQ(jxl_decoder_render_keyframe(ctx, dec, kf, &render), JXL_OK);
        assert(render != NULL);
        assert(jxl_render_keyframe_index(render) == kf);
        assert(jxl_render_width(render) > 0);
        assert(jxl_render_height(render) > 0);
        assert(jxl_render_timecode(render) == 0);

        uint32_t duration_ticks = jxl_render_duration(render);
        assert(duration_ticks > 0);

        uint32_t numer = 0;
        uint32_t denom = 0;
        jxl_animation_frame_duration_rational(&anim, duration_ticks, &numer, &denom);
        assert(rational_near_seconds(numer, denom, expected_durations[kf], 1e-9));

        jxl_render_destroy(ctx, render);
    }

    jxl_decoder_destroy(ctx, dec);
    jxl_context_destroy(ctx);
}

int main(void) {
    test_still_image_animation_unsupported();
    test_animation_newtons_cradle();
    printf("test_decoder_animation: ok\n");
    return 0;
}
