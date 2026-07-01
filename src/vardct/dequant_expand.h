// SPDX-License-Identifier: MIT OR Apache-2.0
#ifndef JXL_VARDCT_DEQUANT_EXPAND_H_
#define JXL_VARDCT_DEQUANT_EXPAND_H_

#include "context.h"
#include "vardct/dequant.h"
#include "vardct/error.h"

void jxl_context_dequant_free(jxl_context *ctx);

jxl_vardct_status_t jxl_dequant_matrix_set_build_weights(jxl_context *ctx,
                                                         jxl_dequant_matrix_set *set);
void jxl_dequant_matrix_set_free_weights(jxl_context *ctx, const jxl_dequant_matrix_set *set);

const float *jxl_dequant_matrix_weights(jxl_context *ctx, const jxl_dequant_matrix_set *set,
                                        size_t matrix_idx, size_t channel, size_t *len_out);
const float *jxl_dequant_matrix_weights_transposed(jxl_context *ctx,
                                                   const jxl_dequant_matrix_set *set,
                                                   size_t matrix_idx, size_t channel,
                                                   size_t *len_out);

#endif /* JXL_VARDCT_DEQUANT_EXPAND_H_ */
