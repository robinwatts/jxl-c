// SPDX-License-Identifier: MIT OR Apache-2.0
#include "bitstream/bitstream.h"
#include "modular/modular.h"

#include "allocator.h"

static jxl_allocator_state *test_alloc(void) {
    static jxl_allocator_state alloc;
    static int init;
    if (!init) { jxl_allocator_init(&alloc, NULL); init = 1; }
    return &alloc;
}

#include <assert.h>
#include "test_helpers.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void test_shift_size_jpeg(void) {
    uint32_t ups[3] = {2, 1, 2};
    jxl_channel_shift sh = jxl_channel_shift_from_jpeg_upsampling(ups, 0);
    uint32_t w = 0;
    uint32_t h = 0;
    jxl_channel_shift_shift_size(&sh, 100, 50, &w, &h);
    assert(w == 100);
    assert(h == 25);
}

static void test_wp_header_default(void) {
    jxl_bs bs;
    uint8_t byte = 0x01;
    jxl_wp_header wp;
    jxl_bs_init(&bs, &byte, 1);
    JXL_TEST_ASSERT_EQ(jxl_wp_header_parse(&bs, &wp), JXL_MODULAR_OK);
    assert(wp.default_wp);
    assert(wp.wp_p1 == 16);
}

/* use_global_tree=1, default wp, nb_transforms=0 */
static const uint8_t k_header_global_ma[] = {0x03};

static void test_modular_header_global(void) {
    jxl_allocator_state alloc;
    jxl_bs bs;
    jxl_modular_params params;
    jxl_ma_config global_ma;
    jxl_modular_header_ma hm;
    jxl_modular_channels channels;
    jxl_modular_image_destination dest;
    jxl_channel_shift shifts[1];
    jxl_modular_parse_ctx ctx = {0};
    jxl_allocator_init(&alloc, NULL);

    jxl_bs_init(&bs, k_header_global_ma, sizeof(k_header_global_ma));

    jxl_modular_params_init(&params);
    shifts[0] = jxl_channel_shift_from_shift(0);

    if (!jxl_modular_params_set_channels(test_alloc(), &params, 64, 64, 256, 8, shifts, 1)) {
        assert(0);
    }

    jxl_ma_config_init(&global_ma);

    ctx.params = &params;
    ctx.global_ma = &global_ma;
    ctx.tracker = NULL;


    jxl_modular_header_ma_init(&hm);
    jxl_modular_channels_init(&channels);

    JXL_TEST_ASSERT_EQ(jxl_modular_read_local_header(&alloc, &bs, &ctx, &hm, &channels), JXL_MODULAR_OK);
    assert(hm.header.use_global_tree);
    assert(hm.header.nb_transforms == 0);
    assert(channels.info_len == 1);
    assert(channels.info[0].width == 64);

    jxl_modular_image_destination_init(&dest);
    JXL_TEST_ASSERT_EQ(jxl_modular_image_destination_create(&alloc, &hm, 256, 8, JXL_MODULAR_SAMPLE_I32,
                                                              &channels, NULL, &dest),
                       JXL_MODULAR_OK);
    assert(dest.image_channels_len == 1);
    assert(dest.image_channels[0].width == 64);

    jxl_modular_image_destination_free(&alloc, &dest);
    jxl_modular_header_ma_free(&alloc, &hm);
    jxl_modular_channels_free(test_alloc(), &channels);
    jxl_modular_params_free(test_alloc(), &params);
}

static void test_rct_prepare(void) {
    jxl_modular_channels ch;
    jxl_transform_info tr;
    jxl_modular_channels_init(&ch);
    jxl_modular_channel_info a = jxl_modular_channel_info_new(32, 32, jxl_channel_shift_from_shift(0));
    jxl_modular_channel_info b = jxl_modular_channel_info_new(32, 32, jxl_channel_shift_from_shift(0));
    jxl_modular_channel_info c = jxl_modular_channel_info_new(32, 32, jxl_channel_shift_from_shift(0));
    if (jxl_modular_channels_push(test_alloc(), &ch, a) != JXL_MODULAR_OK ||
        jxl_modular_channels_push(test_alloc(), &ch, b) != JXL_MODULAR_OK ||
        jxl_modular_channels_push(test_alloc(), &ch, c) != JXL_MODULAR_OK) {
        assert(0);
    }

    memset(&tr, 0, sizeof(tr));
    tr.kind = JXL_TRANSFORM_KIND_RCT;
    tr.u.rct.begin_c = 0;
    tr.u.rct.rct_type = 6;
    JXL_TEST_ASSERT_EQ(jxl_transform_prepare_channel_info(test_alloc(), &tr, &ch), JXL_MODULAR_OK);
    jxl_modular_channels_free(test_alloc(), &ch);
}

int main(void) {
    test_shift_size_jpeg();
    test_wp_header_default();
    test_modular_header_global();
    test_rct_prepare();
    printf("test_modular_parse: ok\n");
    return 0;
}
