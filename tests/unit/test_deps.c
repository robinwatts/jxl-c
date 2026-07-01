// SPDX-License-Identifier: MIT OR Apache-2.0
#include "jxl_oxide/jxl_oxide.h"

#include <assert.h>
#include "test_helpers.h"
#include <stdio.h>

jxl_status_t jxl_deps_self_test(void);

int main(void) {
    JXL_TEST_ASSERT_EQ(jxl_deps_self_test(), JXL_OK);
    printf("test_deps: ok\n");
    return 0;
}
