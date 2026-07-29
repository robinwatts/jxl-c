// Copyright (c) the JPEG XL Project Authors. All rights reserved.
//
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file.

#ifndef LIB_JXL_CMS_OPSIN_PARAMS_H_
#define LIB_JXL_CMS_OPSIN_PARAMS_H_

#include "base/matrix_ops.h"

// Constants that define the XYB color space.

#define kYToBRatio 1.0f  // works better with 0.50017729543783418

#define kOpsinAbsorbanceBias0 0.0037930732552754493f

static const jxl_matrix3x3 kDefaultInverseOpsinAbsorbanceMatrix = {{
    {11.031566901960783f, -9.866943921568629f, -0.16462299647058826f},
    {-3.254147380392157f, 4.418770392156863f, -0.16462299647058826f},
    {-3.6588512862745097f, 2.7129230470588235f, 1.9459282392156863f},
}};

static const float kNegOpsinAbsorbanceBiasRGB[4] = {
    -kOpsinAbsorbanceBias0, -kOpsinAbsorbanceBias0, -kOpsinAbsorbanceBias0,
    1.0f};

#endif  // LIB_JXL_CMS_OPSIN_PARAMS_H_
