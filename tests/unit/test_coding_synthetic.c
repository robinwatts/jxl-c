// SPDX-License-Identifier: MIT OR Apache-2.0
#include "bitstream/bitstream.h"
#include "coding/coding.h"

#include <assert.h>
#include "test_helpers.h"
#include <stdio.h>
#include <string.h>

#include "../coding/ans_synthetic.inc"

int main(void) {
    size_t i;
    jxl_allocator_state alloc;
    jxl_bs bs;
    jxl_bs payload;
    jxl_allocator_init(&alloc, NULL);

    jxl_bs_init(&bs, k_ans_synthetic_setup, k_ans_synthetic_setup_len);
    jxl_coding_decoder *dec = NULL;
    JXL_TEST_ASSERT_EQ(jxl_coding_decoder_parse(&alloc, &bs, 1, &dec), JXL_CODING_OK);
    assert(jxl_bs_num_read_bits(&bs) == k_ans_synthetic_setup_bits);

    jxl_bs_init(&payload, NULL, 0);
    JXL_TEST_ASSERT_EQ(jxl_coding_decoder_begin(dec, &payload), JXL_CODING_OK);

    for (i = 0; i < k_ans_synthetic_symbol_count; ++i) {
        uint32_t sym = 0;
        JXL_TEST_ASSERT_EQ(jxl_coding_decoder_read_varint(dec, &payload, 0, &sym), JXL_CODING_OK);
        assert(sym == k_ans_synthetic_symbols[i]);
    }
    JXL_TEST_ASSERT_EQ(jxl_coding_decoder_finalize(dec), JXL_CODING_OK);

    jxl_coding_decoder_destroy(&alloc, dec);
    printf("test_coding_synthetic: ok\n");
    return 0;
}
