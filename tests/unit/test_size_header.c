// SPDX-License-Identifier: MIT OR Apache-2.0
#include "bitstream/bitstream.h"
#include "codestream_collect.h"
#include "image/image_internal.h"

#include <assert.h>
#include "test_helpers.h"
#include <stdio.h>
#include <stdlib.h>

int main(void) {
    jxl_allocator_state alloc;
    size_t cs_len;
    jxl_bs bs;
    uint32_t sig;
    jxl_size_header size;
    FILE *f = fopen(JXL_OXIDE_FIXTURES_DIR "/grayalpha/input.jxl", "rb");
    assert(f != NULL);
    fseek(f, 0, SEEK_END);
    long n = ftell(f);
    fseek(f, 0, SEEK_SET);
    uint8_t *file = malloc((size_t)n);
    assert(fread(file, 1, (size_t)n, f) == (size_t)n);
    fclose(f);

    jxl_allocator_init(&alloc, NULL);
    uint8_t *cs = NULL;
    cs_len = 0;
    JXL_TEST_ASSERT_EQ(jxl_collect_codestream(&alloc, file, (size_t)n, &cs, &cs_len), JXL_OK);
    free(file);

    jxl_bs_init(&bs, cs, cs_len);
    sig = 0;
    JXL_TEST_ASSERT_EQ(jxl_bs_read_bits(&bs, 16, &sig), JXL_BS_OK);
    assert(sig == 0x0aff);

    JXL_TEST_ASSERT_EQ(jxl_size_header_parse(&bs, &size), JXL_BS_OK);
    assert(size.width == 32);
    assert(size.height == 32);

    free(cs);
    printf("test_size_header: ok\n");
    return 0;
}
