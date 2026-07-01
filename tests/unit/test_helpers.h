// SPDX-License-Identifier: MIT OR Apache-2.0
#ifndef JXL_TEST_HELPERS_H_
#define JXL_TEST_HELPERS_H_

#include <assert.h>
#include <stdlib.h>

/* Use for setup calls with side effects. assert() alone must not be the only
 * use of a call whose result or outputs matter (NDEBUG makes assert a no-op). */

#define JXL_TEST_REQUIRE(cond) \
    do { \
        if (!(cond)) { \
            assert(cond); \
        } \
    } while (0)

#define JXL_TEST_ASSERT_EQ(expr, expected) \
    do { \
        int _jxl_actual = (int)(expr); \
        assert(_jxl_actual == (int)(expected)); \
    } while (0)

#endif /* JXL_TEST_HELPERS_H_ */
