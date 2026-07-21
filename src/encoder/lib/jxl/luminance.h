// Copyright (c) the JPEG XL Project Authors. All rights reserved.
//
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file.

#ifndef LIB_JXL_LUMINANCE_H_
#define LIB_JXL_LUMINANCE_H_

#include "lib/jxl/image_metadata.h"

// Chooses a default intensity target based on the transfer function of the
// image, if known. For SDR images or images not known to be HDR, returns
// kDefaultIntensityTarget, for images known to have PQ or HLG transfer function
// returns a higher value.

void jxl_set_intensity_target(jxl_image_metadata* m);


#endif  // LIB_JXL_LUMINANCE_H_
