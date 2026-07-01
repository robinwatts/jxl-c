// SPDX-License-Identifier: MIT OR Apache-2.0
#include "modular_render.h"

#include "render/modular_encode.h"
#include "render/render_frame.h"

jxl_status_t jxl_render_modular_keyframe(const jxl_keyframe_render_params *params,
                                         const jxl_parsed_image_header *parsed,
                                         const uint8_t *codestream, size_t cs_len, jxl_bs *bs,
                                         jxl_reference_store *refs, jxl_render *r) {
    jxl_modular_encode_result enc;
    jxl_status_t st;
    if (params == NULL || params->alloc == NULL || parsed == NULL || codestream == NULL ||
        bs == NULL || r == NULL) {
        return JXL_ERROR_INVALID_INPUT;
    }

    jxl_modular_encode_result_init(&enc);

    st = jxl_modular_encode_keyframe(params, parsed, codestream, cs_len, bs,
                                     params->filter_region, r, &enc);
    if (st != JXL_OK) {
        jxl_modular_encode_result_free(params->alloc, &enc);
        return st;
    }

    if (enc.valid) {
        st = jxl_render_post_encode_from_modular_result(params->ctx, params->alloc, parsed, &enc,
                                                        params->output_region, refs, r);
        if (st != JXL_OK) {
            jxl_render_set_error((jxl_keyframe_render_params *)params,
                                 "failed post-encode render stage");
        } else {
            st = jxl_render_convert_color_for_record(params->ctx, params->alloc, parsed, &enc.fh, r, 0);
        }
    }

    jxl_modular_encode_result_free(params->alloc, &enc);
    return st;
}
