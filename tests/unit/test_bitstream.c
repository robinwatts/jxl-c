// SPDX-License-Identifier: MIT OR Apache-2.0
#include "bitstream/bitstream.h"
#include "bitstream/unpack.h"

#include <assert.h>
#include "test_helpers.h"
#include <stdio.h>

int main(void) {
    /* u32=15 (selector 2 + 4 bits) then bool=1 at bit 6. */
    static const uint8_t buf[] = {0x72};
    jxl_bs bs;
    uint32_t val;
    int flag;
    jxl_bs_init(&bs, buf, sizeof(buf));

    const jxl_u32_spec specs[4] = {JXL_U32_C(1), JXL_U32_BITS(0, 2), JXL_U32_BITS(3, 4),
                                   JXL_U32_BITS(19, 8)};
    val = 0;
    JXL_TEST_ASSERT_EQ(jxl_bs_read_u32(&bs, specs, &val), JXL_BS_OK);
    assert(val == 15);

    assert(jxl_unpack_signed(0) == 0);
    assert(jxl_unpack_signed(1) == -1);
    assert(jxl_unpack_signed(2) == 1);

    flag = 0;
    JXL_TEST_ASSERT_EQ(jxl_bs_read_bool(&bs, &flag), JXL_BS_OK);
    assert(flag == 1);

    printf("test_bitstream: ok\n");
    return 0;
}
