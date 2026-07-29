// SPDX-License-Identifier: MIT OR Apache-2.0
#ifndef JXL_COMPILER_H_
#define JXL_COMPILER_H_

/* Internal compiler helpers — not part of the public API. */

#if defined(__STDC_VERSION__) && __STDC_VERSION__ >= 199901L
#define jxl_inline static inline
#elif defined(__GNUC__) || defined(__clang__) || defined(_MSC_VER)
#define jxl_inline static __inline
#else
#define jxl_inline static
#endif

#if defined(__STDC_VERSION__) && __STDC_VERSION__ >= 199901L && !defined(__cplusplus)
#define jxl_restrict restrict
#elif defined(__GNUC__) || defined(__clang__) || defined(_MSC_VER)
#define jxl_restrict __restrict
#else
#define jxl_restrict
#endif

#if defined(__GNUC__) || defined(__clang__)
#define JXL_ATTRIBUTE_HOT __attribute__((hot))
#define JXL_ALWAYS_INLINE static inline __attribute__((always_inline))
#else
#define JXL_ATTRIBUTE_HOT
#define JXL_ALWAYS_INLINE jxl_inline
#endif

#endif /* JXL_COMPILER_H_ */
