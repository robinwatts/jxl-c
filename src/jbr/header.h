// SPDX-License-Identifier: MIT OR Apache-2.0
#ifndef JXL_JBR_HEADER_H_
#define JXL_JBR_HEADER_H_

#include "allocator.h"
#include "bitstream/bitstream.h"
#include "jbr/error.h"
#include "jbr/huffman.h"

#include <stddef.h>
#include "jxl_oxide/jxl_types.h"

typedef struct {
    uint32_t ty;
    uint32_t length;
} jxl_jbr_app_marker;

typedef struct {
    uint8_t precision;
    uint8_t index;
    int is_last;
} jxl_jbr_quant_table;

typedef struct {
    uint8_t id;
    uint8_t q_idx;
} jxl_jbr_component;

typedef struct {
    uint8_t comp_idx;
    uint8_t ac_tbl_idx;
    uint8_t dc_tbl_idx;
} jxl_jbr_scan_component_info;

typedef struct {
    uint8_t ss;
    uint8_t se;
    uint8_t al;
    uint8_t ah;
    jxl_jbr_scan_component_info *component_info;
    size_t component_info_len;
    uint8_t last_needed_pass;
} jxl_jbr_scan_info;

typedef struct {
    uint32_t *reset_points;
    size_t reset_points_len;
    uint32_t *ezr_keys;
    uint32_t *ezr_vals;
    size_t ezr_len;
} jxl_jbr_scan_more_info;

typedef struct {
    uint8_t *bits;
    size_t bits_len;
} jxl_jbr_padding;

typedef struct {
    int is_gray;
    uint8_t *markers;
    size_t markers_len;
    jxl_jbr_app_marker *app_markers;
    size_t app_markers_len;
    uint32_t *com_lengths;
    size_t com_lengths_len;
    jxl_jbr_quant_table *quant_tables;
    size_t quant_tables_len;
    jxl_jbr_component *components;
    size_t components_len;
    jxl_jbr_huffman_code *huffman_codes;
    size_t huffman_codes_len;
    jxl_jbr_scan_info *scan_info;
    size_t scan_info_len;
    uint32_t restart_interval;
    jxl_jbr_scan_more_info *scan_more_info;
    size_t scan_more_info_len;
    uint32_t *intermarker_lengths;
    size_t intermarker_lengths_len;
    uint32_t tail_data_length;
    jxl_jbr_padding padding;
} jxl_jbr_header;

void jxl_jbr_header_init(jxl_jbr_header *h);
void jxl_jbr_header_free(jxl_allocator_state *alloc, jxl_jbr_header *h);

/* Returns JXL_JBR_NEED_MORE_DATA on EOF during parse. */
jxl_jbr_status jxl_jbr_header_parse(jxl_allocator_state *alloc, jxl_bs *bs, jxl_jbr_header *out);

size_t jxl_jbr_header_expected_data_len(const jxl_jbr_header *h);
size_t jxl_jbr_header_expected_icc_len(const jxl_jbr_header *h);
size_t jxl_jbr_header_expected_exif_len(const jxl_jbr_header *h);
size_t jxl_jbr_header_expected_xmp_len(const jxl_jbr_header *h);

int jxl_jbr_scan_more_info_has_reset(const jxl_jbr_scan_more_info *smi, uint32_t block_idx);
int jxl_jbr_scan_more_info_has_extra_zero_runs(const jxl_jbr_scan_more_info *smi,
                                               uint32_t block_idx, uint32_t *value_out);
uint32_t jxl_jbr_scan_more_info_extra_zero_runs(const jxl_jbr_scan_more_info *smi,
                                                uint32_t block_idx);

#endif /* JXL_JBR_HEADER_H_ */
