// Copyright (c) the JPEG XL Project Authors. All rights reserved.
//
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file.

#include "lib/jxl/loop_filter.h"

#include <math.h>
#include <stddef.h>

#include "lib/jxl/base/compiler_specific.h"
#include "lib/jxl/base/status.h"
#include "lib/jxl/fields.h"

jxl_status jxl_loop_filter_visit_fields(jxl_loop_filter* self, jxl_visitor* JXL_RESTRICT visitor) {
  // Must come before AllDefault.

  if (jxl_status_ok(jxl_visitor_all_default(visitor, &self->fields, &self->all_default))) {
    // Overwrite all serialized fields, but not any nonserialized_*.
    jxl_visitor_set_default(visitor, &self->fields);
    return jxl_ok_status();
  }

  JXL_QUIET_RETURN_IF_ERROR(jxl_visitor_bool(visitor, true, &self->gab));
  if (jxl_status_ok(jxl_visitor_conditional(visitor, self->gab))) {
    JXL_QUIET_RETURN_IF_ERROR(jxl_visitor_bool(visitor, false, &self->gab_custom));
    if (jxl_status_ok(jxl_visitor_conditional(visitor, self->gab_custom))) {
      JXL_QUIET_RETURN_IF_ERROR(
          jxl_visitor_f16(visitor, 1.1 * 0.104699568f, &self->gab_x_weight1));
      JXL_QUIET_RETURN_IF_ERROR(
          jxl_visitor_f16(visitor, 1.1 * 0.055680538f, &self->gab_x_weight2));
      if (fabs(1.0f + (self->gab_x_weight1 + self->gab_x_weight2) * 4) < 1e-8) {
        return JXL_FAILURE(
            "Gaborish x weights lead to near 0 unnormalized kernel");
      }
      JXL_QUIET_RETURN_IF_ERROR(
          jxl_visitor_f16(visitor, 1.1 * 0.104699568f, &self->gab_y_weight1));
      JXL_QUIET_RETURN_IF_ERROR(
          jxl_visitor_f16(visitor, 1.1 * 0.055680538f, &self->gab_y_weight2));
      if (fabs(1.0f + (self->gab_y_weight1 + self->gab_y_weight2) * 4) < 1e-8) {
        return JXL_FAILURE(
            "Gaborish y weights lead to near 0 unnormalized kernel");
      }
      JXL_QUIET_RETURN_IF_ERROR(
          jxl_visitor_f16(visitor, 1.1 * 0.104699568f, &self->gab_b_weight1));
      JXL_QUIET_RETURN_IF_ERROR(
          jxl_visitor_f16(visitor, 1.1 * 0.055680538f, &self->gab_b_weight2));
      if (fabs(1.0f + (self->gab_b_weight1 + self->gab_b_weight2) * 4) < 1e-8) {
        return JXL_FAILURE(
            "Gaborish b weights lead to near 0 unnormalized kernel");
      }
    }
  }

  JXL_QUIET_RETURN_IF_ERROR(jxl_visitor_bits(visitor, 2, 2, &self->epf_iters));
  if (jxl_status_ok(jxl_visitor_conditional(visitor, self->epf_iters > 0))) {
    if (jxl_status_ok(jxl_visitor_conditional(visitor, !self->nonserialized_is_modular))) {
      JXL_QUIET_RETURN_IF_ERROR(jxl_visitor_bool(visitor, false, &self->epf_sharp_custom));
      if (jxl_status_ok(jxl_visitor_conditional(visitor, self->epf_sharp_custom))) {
        for (size_t i = 0; i < kEpfSharpEntries; ++i) {
          JXL_QUIET_RETURN_IF_ERROR(jxl_visitor_f16(visitor, 
              (float)(i) / (float)(kEpfSharpEntries - 1), &self->epf_sharp_lut[i]));
        }
      }
    }

    JXL_QUIET_RETURN_IF_ERROR(jxl_visitor_bool(visitor, false, &self->epf_weight_custom));
    if (jxl_status_ok(jxl_visitor_conditional(visitor, self->epf_weight_custom))) {
      JXL_QUIET_RETURN_IF_ERROR(jxl_visitor_f16(visitor, 40.0f, &self->epf_channel_scale[0]));
      JXL_QUIET_RETURN_IF_ERROR(jxl_visitor_f16(visitor, 5.0f, &self->epf_channel_scale[1]));
      JXL_QUIET_RETURN_IF_ERROR(jxl_visitor_f16(visitor, 3.5f, &self->epf_channel_scale[2]));
      JXL_QUIET_RETURN_IF_ERROR(jxl_visitor_f16(visitor, 0.45f, &self->epf_pass1_zeroflush));
      JXL_QUIET_RETURN_IF_ERROR(jxl_visitor_f16(visitor, 0.6f, &self->epf_pass2_zeroflush));
    }

    JXL_QUIET_RETURN_IF_ERROR(jxl_visitor_bool(visitor, false, &self->epf_sigma_custom));
    if (jxl_status_ok(jxl_visitor_conditional(visitor, self->epf_sigma_custom))) {
      if (jxl_status_ok(jxl_visitor_conditional(visitor, !self->nonserialized_is_modular))) {
        JXL_QUIET_RETURN_IF_ERROR(jxl_visitor_f16(visitor, 0.46f, &self->epf_quant_mul));
      }
      JXL_QUIET_RETURN_IF_ERROR(jxl_visitor_f16(visitor, 0.9f, &self->epf_pass0_sigma_scale));
      JXL_QUIET_RETURN_IF_ERROR(jxl_visitor_f16(visitor, 6.5f, &self->epf_pass2_sigma_scale));
      JXL_QUIET_RETURN_IF_ERROR(
          jxl_visitor_f16(visitor, 0.6666666666666666f, &self->epf_border_sad_mul));
    }
    if (jxl_status_ok(jxl_visitor_conditional(visitor, self->nonserialized_is_modular))) {
      JXL_QUIET_RETURN_IF_ERROR(jxl_visitor_f16(visitor, 1.0f, &self->epf_sigma_for_modular));
      if (self->epf_sigma_for_modular < 1e-8) {
        return JXL_FAILURE("EPF: sigma for modular is too small");
      }
    }
  }

  JXL_QUIET_RETURN_IF_ERROR(jxl_visitor_begin_extensions(visitor, &self->extensions));
  // Extensions: in chronological order of being added to the format.
  return jxl_visitor_end_extensions(visitor);
}

