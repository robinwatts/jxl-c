// SPDX-License-Identifier: MIT OR Apache-2.0
#ifndef JXL_STATIC_ASSERT_H_
#define JXL_STATIC_ASSERT_H_

/* C99 compile-time assertion (negative array size if cond is false). */
#define JXL_STATIC_ASSERT(cond, msg) \
    typedef char jxl_static_assert_##__LINE__[(cond) ? 1 : -1]

#endif /* JXL_STATIC_ASSERT_H_ */
