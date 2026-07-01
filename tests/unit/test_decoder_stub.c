// SPDX-License-Identifier: MIT OR Apache-2.0
#include "jxl_oxide/jxl_oxide.h"

#include <assert.h>
#include "test_helpers.h"
#include <stdio.h>
#include <string.h>

int main(void) {
    jxl_context *ctx = NULL;
    JXL_TEST_ASSERT_EQ(jxl_context_create(NULL, &ctx), JXL_OK);

    jxl_decoder *dec = NULL;
    JXL_TEST_ASSERT_EQ(jxl_decoder_create(ctx, NULL, &dec), JXL_OK);
    assert(dec != NULL);

    JXL_TEST_ASSERT_EQ(jxl_decoder_try_init(dec), JXL_NEED_MORE_DATA);

    assert(jxl_status_string(JXL_OK)[0] != '\0');
    assert(jxl_decoder_header(dec) == NULL);

    jxl_decoder_destroy(ctx, dec);
    jxl_context_destroy(ctx);
    printf("test_decoder_stub: ok\n");
    return 0;
}
