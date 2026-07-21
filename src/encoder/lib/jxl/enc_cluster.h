// Copyright (c) the JPEG XL Project Authors. All rights reserved.
//
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file.

// Functions for clustering similar histograms together.

#ifndef LIB_JXL_ENC_CLUSTER_H_
#define LIB_JXL_ENC_CLUSTER_H_

#include <stddef.h>
#include <stdint.h>

#include "lib/jxl/base/array.h"
#include "lib/jxl/base/status.h"
#include "lib/jxl/enc_ans_params.h"

// `in` / `out` are parallel to `in_counts` / `out_counts`.
jxl_status jxl_cluster_histograms(const jxl_histogram_params* params,
                         const jxl_array_histogram* in,
                         const jxl_hist_count_streams* in_counts,
                         size_t max_histograms, jxl_array_histogram* out,
                         jxl_hist_count_streams* out_counts,
                         jxl_array_u32* histogram_symbols);

#endif  // LIB_JXL_ENC_CLUSTER_H_
