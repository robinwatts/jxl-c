// Copyright (c) the JPEG XL Project Authors. All rights reserved.
//
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file.

#include "lib/jxl/luminance.h"

#include "lib/jxl/base/common.h"
#include "lib/jxl/image_metadata.h"


void jxl_set_intensity_target(jxl_image_metadata* m) {
  if (jxl_cms_custom_transfer_function_is_pq(jxl_enc_color_encoding_tf(&m->color_encoding))) {
    // Peak luminance of PQ as defined by SMPTE ST 2084:2014.
    jxl_image_metadata_set_intensity_target(m, 10000);
  } else if (jxl_cms_custom_transfer_function_is_hlg(jxl_enc_color_encoding_tf(&m->color_encoding))) {
    // Nominal display peak luminance used as a reference by
    // Rec. ITU-R BT.2100-2.
    jxl_image_metadata_set_intensity_target(m, 1000);
  } else {
    // SDR
    jxl_image_metadata_set_intensity_target(m, kDefaultIntensityTarget);
  }
}

