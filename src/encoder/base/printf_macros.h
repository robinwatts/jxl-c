// Copyright (c) the JPEG XL Project Authors. All rights reserved.
//
// Use of this source code is governed by a BSD-style
// license that can be found in LICENSE-BSD.

#ifndef JXL_ENC_BASE_PRINTF_MACROS_H_
#define JXL_ENC_BASE_PRINTF_MACROS_H_

// Format string macros. These should be included after any other system
// library since those may unconditionally define these, depending on the
// platform.

// jxl_pr_iu_s and jxl_pr_id_s macros to print size_t and ptrdiff_t respectively.
#if !defined(jxl_pr_id_s)
#if defined(_WIN64)
#define jxl_pr_id_s "lld"
#elif defined(_WIN32)
#define jxl_pr_id_s "d"
#else
#define jxl_pr_id_s "zd"
#endif
#endif  // jxl_pr_id_s

#if !defined(jxl_pr_iu_s)
#if defined(_WIN64)
#define jxl_pr_iu_s "llu"
#elif defined(_WIN32)
#define jxl_pr_iu_s "u"
#else
#define jxl_pr_iu_s "zu"
#endif
#endif  // jxl_pr_iu_s

#endif  // JXL_ENC_BASE_PRINTF_MACROS_H_
