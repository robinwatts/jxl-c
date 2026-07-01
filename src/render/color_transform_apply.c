// SPDX-License-Identifier: MIT OR Apache-2.0
#include "color_transform_apply.h"

#include "render/color_transform.h"

jxl_status_t jxl_color_transform_xyb_to_encoding(jxl_context *ctx, float *x, float *y, float *b,
                                                 size_t num_pixels,
                                                 const jxl_opsin_inverse_parsed *opsin,
                                                 const jxl_colour_encoding_parsed *target,
                                                 float intensity_target) {
    if (x == NULL || y == NULL || b == NULL || opsin == NULL || target == NULL ||
        num_pixels == 0) {
        return JXL_ERROR_INVALID_INPUT;
    }
    if (target->colour_space != JXL_COLOUR_SPACE_RGB_I &&
        target->colour_space != JXL_COLOUR_SPACE_GRAY_I) {
        return JXL_ERROR_UNSUPPORTED;
    }
    if (target->white_point != JXL_WHITE_POINT_D65_I ||
        target->primaries != JXL_PRIMARIES_SRGB_I) {
        return JXL_ERROR_UNSUPPORTED;
    }
    if (target->transfer != JXL_TRANSFER_SRGB_I && target->transfer != JXL_TRANSFER_LINEAR_I &&
        target->transfer != JXL_TRANSFER_BT709_I && target->transfer != JXL_TRANSFER_PQ_I) {
        return JXL_ERROR_UNSUPPORTED;
    }

    jxl_color_transform_xyb_to_linear_rgb(ctx, x, y, b, num_pixels, opsin, intensity_target);
    if (target->transfer != JXL_TRANSFER_LINEAR_I) {
        jxl_color_transform_apply_forward_transfer(ctx, x, num_pixels, target->transfer,
                                                   intensity_target);
        jxl_color_transform_apply_forward_transfer(ctx, y, num_pixels, target->transfer,
                                                   intensity_target);
        jxl_color_transform_apply_forward_transfer(ctx, b, num_pixels, target->transfer,
                                                   intensity_target);
    }
    return JXL_OK;
}
