// SPDX-License-Identifier: MIT OR Apache-2.0
#ifndef JXL_JBR_SCAN_H_
#define JXL_JBR_SCAN_H_

#include "allocator.h"
#include "context.h"
#include "frame/frame.h"
#include "frame/hf_global.h"
#include "frame/lf_global.h"
#include "frame/lf_group.h"
#include "jbr/error.h"
#include "jbr/header.h"
#include "jbr/huffman.h"
#include "jbr/output.h"
#include "jbr/bit_writer.h"
#include "modular/param.h"

#include <stddef.h>
#include "jxl_oxide/jxl_types.h"

#define JXL_JBR_CFL_FIXED_POINT_BITS 11
#define JXL_JBR_CFL_DEFAULT_COLOR_FACTOR 84

typedef struct {
    int32_t *data[3];
    size_t width[3];
    size_t height[3];
    size_t stride[3];
} jxl_jbr_group_coeff_bufs;

typedef struct {
    jxl_lf_global lf_global;
    jxl_hf_global hf_global;
    jxl_lf_group *lf_groups;
    uint32_t num_lf_groups;
    jxl_jbr_group_coeff_bufs *pass_groups;
    uint32_t num_groups;
    int16_t dc_offset[3];
} jxl_jbr_parsed_frame;

typedef struct {
    const jxl_jbr_scan_info *si;
    const jxl_jbr_scan_more_info *smi;
    jxl_channel_shift upsampling_shifts_ycbcr[3];
    uint32_t *hsamples;
    uint32_t *vsamples;
    size_t num_comps;
    uint32_t max_hsample;
    uint32_t max_vsample;
    uint32_t w8;
    uint32_t h8;
} jxl_jbr_scan_params;

typedef struct {
    jxl_jbr_bit_writer bit_writer;
    int16_t *dc_pred;
    size_t dc_pred_len;
    uint32_t eobrun;
    const jxl_jbr_huffman_table *last_ac_table;
    int has_last_ac_table;
    uint8_t *refinement_bitlen;
    uint64_t *refinement_bits;
    size_t refinement_len;
    size_t refinement_cap;
    uint8_t rst_m;
} jxl_jbr_scan_state;

typedef struct jxl_jbr_reconstructor {
    jxl_jbr_parsed_frame parsed;
    int is_progressive;
    uint32_t restart_interval;
    jxl_jbr_huffman_table dc_tables[4];
    int has_dc_table[4];
    jxl_jbr_huffman_table ac_tables[4];
    int has_ac_table[4];

    const jxl_jbr_header *header;
    const jxl_frame *frame;
    jxl_context *ctx;

    size_t marker_ptr;
    size_t app_marker_ptr;
    size_t next_icc_marker;
    size_t icc_marker_offset;
    size_t num_icc_markers;
    const uint8_t *app_data;
    const uint8_t *com_data;
    const uint8_t *intermarker_data;
    size_t huffman_code_ptr;
    size_t quant_ptr;
    uint16_t last_quant_val[64];
    int has_last_quant_val;
    jxl_bs padding_bs;
    int has_padding_bs;
    size_t scan_info_ptr;
    const uint8_t *tail_data;

    const uint8_t *icc_profile;
    size_t icc_len;
    const uint8_t *exif;
    size_t exif_len;
    const uint8_t *xmp;
    size_t xmp_len;

    size_t com_length_ptr;
    size_t intermarker_length_ptr;
} jxl_jbr_reconstructor;

void jxl_jbr_scan_state_init(jxl_jbr_scan_state *state, jxl_allocator_state *alloc, size_t num_comps);
void jxl_jbr_scan_state_free(jxl_allocator_state *alloc, jxl_jbr_scan_state *state);

jxl_jbr_status jxl_jbr_process_sequential(jxl_jbr_scan_state *state, jxl_allocator_state *alloc,
                                          size_t component_idx, const jxl_jbr_huffman_table *dc_table,
                                          const jxl_jbr_huffman_table *ac_table, int16_t dc,
                                          const int16_t *ac, size_t ac_len, int has_extra_zero_runs,
                                          uint32_t extra_zero_runs);

jxl_jbr_status jxl_jbr_process_progressive_first(jxl_jbr_scan_state *state,
                                                 jxl_allocator_state *alloc, size_t component_idx,
                                                 const jxl_jbr_huffman_table *dc_table,
                                                 const jxl_jbr_huffman_table *ac_table, int has_dc,
                                                 int16_t dc, const int16_t *ac, size_t ac_len,
                                                 int has_extra_zero_runs, uint32_t extra_zero_runs);

jxl_jbr_status jxl_jbr_process_progressive_refinement(jxl_jbr_scan_state *state,
                                                      jxl_allocator_state *alloc,
                                                      const jxl_jbr_huffman_table *ac_table,
                                                      int has_dc, int16_t dc, const int16_t *ac,
                                                      size_t ac_len, int has_extra_zero_runs,
                                                      uint32_t extra_zero_runs);

jxl_jbr_status jxl_jbr_process_scan(jxl_jbr_reconstructor *recon, jxl_allocator_state *alloc,
                                    int scan_type, const jxl_jbr_scan_params *params,
                                    jxl_jbr_output *out);

#endif /* JXL_JBR_SCAN_H_ */
