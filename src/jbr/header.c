// SPDX-License-Identifier: MIT OR Apache-2.0
#include "jbr/header.h"

#include <string.h>

static const uint8_t k_header_icc[] = "ICC_PROFILE\0";
static const uint8_t k_header_exif[] = "Exif\0\0";
static const uint8_t k_header_xmp[] = "http://ns.adobe.com/xap/1.0/\0";

static const jxl_u32_spec k_num_huff_specs[4] = {JXL_U32_C(4), JXL_U32_BITS(2, 3), JXL_U32_BITS(10, 4),
                                                 JXL_U32_BITS(26, 6)};
static const jxl_u32_spec k_last_needed_pass_specs[4] = {JXL_U32_C(0), JXL_U32_C(1), JXL_U32_C(2),
                                                       JXL_U32_BITS(3, 3)};
static const jxl_u32_spec k_num_reset_specs[4] = {JXL_U32_C(0), JXL_U32_BITS(1, 2), JXL_U32_BITS(4, 4),
                                                  JXL_U32_BITS(20, 16)};
static const jxl_u32_spec k_reset_diff_specs[4] = {JXL_U32_C(0), JXL_U32_BITS(1, 3), JXL_U32_BITS(9, 5),
                                                 JXL_U32_BITS(41, 28)};
static const jxl_u32_spec k_num_ezr_specs[4] = {JXL_U32_C(0), JXL_U32_BITS(1, 2), JXL_U32_BITS(4, 4),
                                                JXL_U32_BITS(20, 16)};
static const jxl_u32_spec k_ezr_num_runs_specs[4] = {JXL_U32_C(1), JXL_U32_BITS(2, 2), JXL_U32_BITS(5, 4),
                                                     JXL_U32_BITS(20, 8)};
static const jxl_u32_spec k_ezr_run_len_specs[4] = {JXL_U32_C(0), JXL_U32_BITS(1, 3), JXL_U32_BITS(9, 5),
                                                    JXL_U32_BITS(41, 28)};
static const jxl_u32_spec k_tail_len_specs[4] = {JXL_U32_C(0), JXL_U32_BITS(1, 8), JXL_U32_BITS(257, 16),
                                                 JXL_U32_BITS(65793, 22)};
static const jxl_u32_spec k_app_ty_specs[4] = {JXL_U32_C(0), JXL_U32_C(1), JXL_U32_BITS(2, 1),
                                               JXL_U32_BITS(4, 2)};

static jxl_jbr_status bs_to_jbr(jxl_bs_status_t st) {
    if (st == JXL_BS_EOF) {
        return JXL_JBR_NEED_MORE_DATA;
    }
    return JXL_JBR_BITSTREAM_ERROR;
}

static void scan_more_info_free(jxl_allocator_state *alloc, jxl_jbr_scan_more_info *smi) {
    if (smi == NULL) {
        return;
    }
    jxl_free(alloc, smi->reset_points);
    jxl_free(alloc, smi->ezr_keys);
    jxl_free(alloc, smi->ezr_vals);
    memset(smi, 0, sizeof(*smi));
}

void jxl_jbr_header_init(jxl_jbr_header *h) {
    if (h != NULL) {
        memset(h, 0, sizeof(*h));
    }
}

void jxl_jbr_header_free(jxl_allocator_state *alloc, jxl_jbr_header *h) {
    size_t i;
    if (h == NULL) {
        return;
    }
    jxl_free(alloc, h->markers);
    jxl_free(alloc, h->app_markers);
    jxl_free(alloc, h->com_lengths);
    jxl_free(alloc, h->quant_tables);
    jxl_free(alloc, h->components);
    for (i = 0; i < h->huffman_codes_len; ++i) {
        jxl_jbr_huffman_code_free(alloc, &h->huffman_codes[i]);
    }
    jxl_free(alloc, h->huffman_codes);
    for (i = 0; i < h->scan_info_len; ++i) {
        jxl_free(alloc, h->scan_info[i].component_info);
    }
    jxl_free(alloc, h->scan_info);
    if (h->scan_more_info != NULL) {
        size_t i;
        for (i = 0; i < h->scan_more_info_len; ++i) {
            scan_more_info_free(alloc, &h->scan_more_info[i]);
        }
    }
    jxl_free(alloc, h->scan_more_info);
    jxl_free(alloc, h->intermarker_lengths);
    jxl_free(alloc, h->padding.bits);
    memset(h, 0, sizeof(*h));
}

static jxl_jbr_status parse_app_marker(jxl_bs *bs, jxl_jbr_app_marker *out) {
    uint32_t ty = 0;
    uint32_t length = 0;
    if (jxl_bs_read_u32(bs, k_app_ty_specs, &ty) != JXL_BS_OK ||
        jxl_bs_read_bits(bs, 16, &length) != JXL_BS_OK) {
        return bs_to_jbr(JXL_BS_EOF);
    }
    out->ty = ty;
    out->length = length + 1;
    return JXL_JBR_OK;
}

static jxl_jbr_status parse_quant_table(jxl_bs *bs, jxl_jbr_quant_table *out) {
    uint32_t precision = 0;
    uint32_t index = 0;
    int is_last = 0;
    if (jxl_bs_read_bits(bs, 1, &precision) != JXL_BS_OK ||
        jxl_bs_read_bits(bs, 2, &index) != JXL_BS_OK ||
        jxl_bs_read_bool(bs, &is_last) != JXL_BS_OK) {
        return bs_to_jbr(JXL_BS_EOF);
    }
    out->precision = (uint8_t)precision;
    out->index = (uint8_t)index;
    out->is_last = is_last;
    return JXL_JBR_OK;
}

static jxl_jbr_status parse_scan_component(jxl_bs *bs, jxl_jbr_scan_component_info *out) {
    uint32_t comp_idx = 0;
    uint32_t ac_tbl_idx = 0;
    uint32_t dc_tbl_idx = 0;
    if (jxl_bs_read_bits(bs, 2, &comp_idx) != JXL_BS_OK ||
        jxl_bs_read_bits(bs, 2, &ac_tbl_idx) != JXL_BS_OK ||
        jxl_bs_read_bits(bs, 2, &dc_tbl_idx) != JXL_BS_OK) {
        return bs_to_jbr(JXL_BS_EOF);
    }
    out->comp_idx = (uint8_t)comp_idx;
    out->ac_tbl_idx = (uint8_t)ac_tbl_idx;
    out->dc_tbl_idx = (uint8_t)dc_tbl_idx;
    return JXL_JBR_OK;
}

static jxl_jbr_status parse_scan_info(jxl_allocator_state *alloc, jxl_bs *bs,
                                      jxl_jbr_scan_info *out) {
                                          size_t i;
    uint32_t num_comps = 0;
    uint32_t ss = 0;
    uint32_t se = 0;
    uint32_t al = 0;
    uint32_t ah = 0;
    uint32_t last_needed;
    if (jxl_bs_read_bits(bs, 2, &num_comps) != JXL_BS_OK ||
        jxl_bs_read_bits(bs, 6, &ss) != JXL_BS_OK ||
        jxl_bs_read_bits(bs, 6, &se) != JXL_BS_OK ||
        jxl_bs_read_bits(bs, 4, &al) != JXL_BS_OK ||
        jxl_bs_read_bits(bs, 4, &ah) != JXL_BS_OK) {
        return bs_to_jbr(JXL_BS_EOF);
    }
    out->ss = (uint8_t)ss;
    out->se = (uint8_t)se;
    out->al = (uint8_t)al;
    out->ah = (uint8_t)ah;
    out->component_info_len = (size_t)num_comps + 1;
    out->component_info =
        jxl_calloc(alloc, out->component_info_len, sizeof(*out->component_info));
    if (out->component_info == NULL) {
        return JXL_JBR_OUT_OF_MEMORY;
    }
    for (i = 0; i < out->component_info_len; ++i) {
        jxl_jbr_status st = parse_scan_component(bs, &out->component_info[i]);
        if (st != JXL_JBR_OK) {
            return st;
        }
    }
    last_needed = 0;
    if (jxl_bs_read_u32(bs, k_last_needed_pass_specs, &last_needed) != JXL_BS_OK) {
        return bs_to_jbr(JXL_BS_EOF);
    }
    out->last_needed_pass = (uint8_t)last_needed;
    return JXL_JBR_OK;
}

static jxl_jbr_status parse_scan_more_info(jxl_allocator_state *alloc, jxl_bs *bs,
                                           jxl_jbr_scan_more_info *out) {
    uint32_t num_reset = 0;
    uint32_t num_ezr;
    if (jxl_bs_read_u32(bs, k_num_reset_specs, &num_reset) != JXL_BS_OK) {
        return bs_to_jbr(JXL_BS_EOF);
    }
    out->reset_points_len = num_reset;
    if (num_reset > 0) {
        uint32_t i;
        uint32_t last_block;
        int has_last;
        out->reset_points = jxl_alloc(alloc, num_reset * sizeof(uint32_t));
        if (out->reset_points == NULL) {
            return JXL_JBR_OUT_OF_MEMORY;
        }
        last_block = 0;
        has_last = 0;
        for (i = 0; i < num_reset; ++i) {
            uint32_t diff = 0;
            if (jxl_bs_read_u32(bs, k_reset_diff_specs, &diff) != JXL_BS_OK) {
                return bs_to_jbr(JXL_BS_EOF);
            }
            uint32_t block_idx = has_last ? last_block + diff + 1 : diff;
            if (block_idx > (3u << 26)) {
                return JXL_JBR_INVALID_DATA;
            }
            out->reset_points[i] = block_idx;
            last_block = block_idx;
            has_last = 1;
        }
    }

    num_ezr = 0;
    if (jxl_bs_read_u32(bs, k_num_ezr_specs, &num_ezr) != JXL_BS_OK) {
        return bs_to_jbr(JXL_BS_EOF);
    }
    out->ezr_len = num_ezr;
    if (num_ezr > 0) {
        uint32_t i;
        uint32_t last_block;
        int has_last;
        out->ezr_keys = jxl_alloc(alloc, num_ezr * sizeof(uint32_t));
        out->ezr_vals = jxl_alloc(alloc, num_ezr * sizeof(uint32_t));
        if (out->ezr_keys == NULL || out->ezr_vals == NULL) {
            return JXL_JBR_OUT_OF_MEMORY;
        }
        last_block = 0;
        has_last = 0;
        for (i = 0; i < num_ezr; ++i) {
            uint32_t num_runs = 0;
            uint32_t run_length = 0;
            if (jxl_bs_read_u32(bs, k_ezr_num_runs_specs, &num_runs) != JXL_BS_OK ||
                jxl_bs_read_u32(bs, k_ezr_run_len_specs, &run_length) != JXL_BS_OK) {
                return bs_to_jbr(JXL_BS_EOF);
            }
            uint32_t block_idx = has_last ? last_block + run_length + 1 : run_length;
            if (block_idx > (3u << 26)) {
                return JXL_JBR_INVALID_DATA;
            }
            out->ezr_keys[i] = block_idx;
            out->ezr_vals[i] = num_runs;
            last_block = block_idx;
            has_last = 1;
        }
    }
    return JXL_JBR_OK;
}

static jxl_jbr_status parse_padding(jxl_allocator_state *alloc, jxl_bs *bs,
                                      jxl_jbr_padding *out) {
                                          uint32_t i;
    uint32_t num_bits = 0;
    uint32_t full_bytes;
    uint32_t extra_bits;
    if (jxl_bs_read_bits(bs, 24, &num_bits) != JXL_BS_OK) {
        return bs_to_jbr(JXL_BS_EOF);
    }
    full_bytes = num_bits / 8;
    extra_bits = num_bits % 8;
    size_t cap = (size_t)full_bytes + (extra_bits != 0 ? 1 : 0);
    out->bits = jxl_alloc(alloc, cap);
    if (out->bits == NULL) {
        return JXL_JBR_OUT_OF_MEMORY;
    }
    out->bits_len = cap;
    for (i = 0; i < full_bytes; ++i) {
        uint32_t b = 0;
        if (jxl_bs_read_bits(bs, 8, &b) != JXL_BS_OK) {
            return bs_to_jbr(JXL_BS_EOF);
        }
        out->bits[i] = (uint8_t)b;
    }
    if (extra_bits != 0) {
        uint32_t b = 0;
        if (jxl_bs_read_bits(bs, extra_bits, &b) != JXL_BS_OK) {
            return bs_to_jbr(JXL_BS_EOF);
        }
        out->bits[full_bytes] = (uint8_t)b;
    }
    return JXL_JBR_OK;
}

jxl_jbr_status jxl_jbr_header_parse(jxl_allocator_state *alloc, jxl_bs *bs, jxl_jbr_header *out) {
    size_t i;
    int is_gray;
    size_t marker_cap;
    size_t num_app;
    size_t num_com;
    size_t num_scans;
    size_t num_inter;
    int has_dri;
    uint32_t num_quant;
    uint32_t comp_type;
    size_t num_comp;
    uint8_t comp_ids[3];
    uint32_t num_huff;
    int has_padding;
    if (alloc == NULL || bs == NULL || out == NULL) {
        return JXL_JBR_BITSTREAM_ERROR;
    }
    jxl_jbr_header_free(alloc, out);
    jxl_jbr_header_init(out);

    is_gray = 0;
    if (jxl_bs_read_bool(bs, &is_gray) != JXL_BS_OK) {
        return bs_to_jbr(JXL_BS_EOF);
    }
    out->is_gray = is_gray;

    marker_cap = 16;
    out->markers = jxl_alloc(alloc, marker_cap);
    if (out->markers == NULL) {
        return JXL_JBR_OUT_OF_MEMORY;
    }
    num_app = 0;
    num_com = 0;
    num_scans = 0;
    num_inter = 0;
    has_dri = 0;

    for (;;) {
        uint32_t marker_bits = 0;
        if (jxl_bs_read_bits(bs, 6, &marker_bits) != JXL_BS_OK) {
            jxl_jbr_header_free(alloc, out);
            return bs_to_jbr(JXL_BS_EOF);
        }
        uint8_t marker = (uint8_t)(marker_bits + 0xc0);
        if (out->markers_len >= marker_cap) {
            marker_cap *= 2;
            uint8_t *grown = jxl_realloc(alloc, out->markers, marker_cap);
            if (grown == NULL) {
                jxl_jbr_header_free(alloc, out);
                return JXL_JBR_OUT_OF_MEMORY;
            }
            out->markers = grown;
        }
        out->markers[out->markers_len++] = marker;
        if (marker >= 0xe0 && marker <= 0xef) {
            num_app++;
        } else if (marker == 0xfe) {
            num_com++;
        } else if (marker == 0xda) {
            num_scans++;
        } else if (marker == 0xff) {
            num_inter++;
        } else if (marker == 0xdd) {
            has_dri = 1;
        }
        if (marker == 0xd9) {
            break;
        }
    }

    if (num_app > 0) {
        size_t i;
        out->app_markers = jxl_calloc(alloc, num_app, sizeof(*out->app_markers));
        if (out->app_markers == NULL) {
            jxl_jbr_header_free(alloc, out);
            return JXL_JBR_OUT_OF_MEMORY;
        }
        out->app_markers_len = num_app;
        for (i = 0; i < num_app; ++i) {
            jxl_jbr_status st = parse_app_marker(bs, &out->app_markers[i]);
            if (st != JXL_JBR_OK) {
                jxl_jbr_header_free(alloc, out);
                return st;
            }
        }
    }

    if (num_com > 0) {
        size_t i;
        out->com_lengths = jxl_alloc(alloc, num_com * sizeof(uint32_t));
        if (out->com_lengths == NULL) {
            jxl_jbr_header_free(alloc, out);
            return JXL_JBR_OUT_OF_MEMORY;
        }
        out->com_lengths_len = num_com;
        for (i = 0; i < num_com; ++i) {
            uint32_t len = 0;
            if (jxl_bs_read_bits(bs, 16, &len) != JXL_BS_OK) {
                jxl_jbr_header_free(alloc, out);
                return bs_to_jbr(JXL_BS_EOF);
            }
            out->com_lengths[i] = len + 1;
        }
    }

    num_quant = 0;
    if (jxl_bs_read_bits(bs, 2, &num_quant) != JXL_BS_OK) {
        jxl_jbr_header_free(alloc, out);
        return bs_to_jbr(JXL_BS_EOF);
    }
    num_quant += 1;
    out->quant_tables = jxl_calloc(alloc, num_quant, sizeof(*out->quant_tables));
    if (out->quant_tables == NULL) {
        jxl_jbr_header_free(alloc, out);
        return JXL_JBR_OUT_OF_MEMORY;
    }
    out->quant_tables_len = num_quant;
    for (i = 0; i < num_quant; ++i) {
        jxl_jbr_status st = parse_quant_table(bs, &out->quant_tables[i]);
        if (st != JXL_JBR_OK) {
            jxl_jbr_header_free(alloc, out);
            return st;
        }
    }

    comp_type = 0;
    if (jxl_bs_read_bits(bs, 2, &comp_type) != JXL_BS_OK) {
        jxl_jbr_header_free(alloc, out);
        return bs_to_jbr(JXL_BS_EOF);
    }
    num_comp = 0;
    if (comp_type == 0) {
        comp_ids[0] = 1;
        num_comp = 1;
    } else if (comp_type == 1) {
        comp_ids[0] = 1;
        comp_ids[1] = 2;
        comp_ids[2] = 3;
        num_comp = 3;
    } else if (comp_type == 2) {
        comp_ids[0] = 'R';
        comp_ids[1] = 'G';
        comp_ids[2] = 'B';
        num_comp = 3;
    } else {
        size_t i;
        uint32_t n = 0;
        if (jxl_bs_read_bits(bs, 2, &n) != JXL_BS_OK) {
            jxl_jbr_header_free(alloc, out);
            return bs_to_jbr(JXL_BS_EOF);
        }
        num_comp = (size_t)n + 1;
        for (i = 0; i < num_comp; ++i) {
            uint32_t id = 0;
            if (jxl_bs_read_bits(bs, 8, &id) != JXL_BS_OK) {
                jxl_jbr_header_free(alloc, out);
                return bs_to_jbr(JXL_BS_EOF);
            }
            comp_ids[i] = (uint8_t)id;
        }
    }
    out->components = jxl_calloc(alloc, num_comp, sizeof(*out->components));
    if (out->components == NULL) {
        jxl_jbr_header_free(alloc, out);
        return JXL_JBR_OUT_OF_MEMORY;
    }
    out->components_len = num_comp;
    for (i = 0; i < num_comp; ++i) {
        uint32_t q_idx = 0;
        if (jxl_bs_read_bits(bs, 2, &q_idx) != JXL_BS_OK) {
            jxl_jbr_header_free(alloc, out);
            return bs_to_jbr(JXL_BS_EOF);
        }
        out->components[i].id = comp_ids[i];
        out->components[i].q_idx = (uint8_t)q_idx;
    }

    num_huff = 0;
    if (jxl_bs_read_u32(bs, k_num_huff_specs, &num_huff) != JXL_BS_OK) {
        jxl_jbr_header_free(alloc, out);
        return bs_to_jbr(JXL_BS_EOF);
    }
    if (num_huff > 0) {
        size_t i;
        out->huffman_codes = jxl_calloc(alloc, num_huff, sizeof(*out->huffman_codes));
        if (out->huffman_codes == NULL) {
            jxl_jbr_header_free(alloc, out);
            return JXL_JBR_OUT_OF_MEMORY;
        }
        out->huffman_codes_len = num_huff;
        for (i = 0; i < num_huff; ++i) {
            jxl_jbr_huffman_code_init(&out->huffman_codes[i]);
            jxl_jbr_status st =
                jxl_jbr_huffman_code_parse(alloc, bs, &out->huffman_codes[i]);
            if (st != JXL_JBR_OK) {
                jxl_jbr_header_free(alloc, out);
                return st;
            }
        }
    }

    if (num_scans > 0) {
        size_t i;
        out->scan_info = jxl_calloc(alloc, num_scans, sizeof(*out->scan_info));
        if (out->scan_info == NULL) {
            jxl_jbr_header_free(alloc, out);
            return JXL_JBR_OUT_OF_MEMORY;
        }
        out->scan_info_len = num_scans;
        for (i = 0; i < num_scans; ++i) {
            jxl_jbr_status st = parse_scan_info(alloc, bs, &out->scan_info[i]);
            if (st != JXL_JBR_OK) {
                jxl_jbr_header_free(alloc, out);
                return st;
            }
        }
    }

    if (has_dri) {
        if (jxl_bs_read_bits(bs, 16, &out->restart_interval) != JXL_BS_OK) {
            jxl_jbr_header_free(alloc, out);
            return bs_to_jbr(JXL_BS_EOF);
        }
    }

    if (num_scans > 0) {
        size_t i;
        out->scan_more_info = jxl_calloc(alloc, num_scans, sizeof(*out->scan_more_info));
        if (out->scan_more_info == NULL) {
            jxl_jbr_header_free(alloc, out);
            return JXL_JBR_OUT_OF_MEMORY;
        }
        out->scan_more_info_len = num_scans;
        for (i = 0; i < num_scans; ++i) {
            jxl_jbr_status st = parse_scan_more_info(alloc, bs, &out->scan_more_info[i]);
            if (st != JXL_JBR_OK) {
                jxl_jbr_header_free(alloc, out);
                return st;
            }
        }
    }

    if (num_inter > 0) {
        size_t i;
        out->intermarker_lengths = jxl_alloc(alloc, num_inter * sizeof(uint32_t));
        if (out->intermarker_lengths == NULL) {
            jxl_jbr_header_free(alloc, out);
            return JXL_JBR_OUT_OF_MEMORY;
        }
        out->intermarker_lengths_len = num_inter;
        for (i = 0; i < num_inter; ++i) {
            if (jxl_bs_read_bits(bs, 16, &out->intermarker_lengths[i]) != JXL_BS_OK) {
                jxl_jbr_header_free(alloc, out);
                return bs_to_jbr(JXL_BS_EOF);
            }
        }
    }

    if (jxl_bs_read_u32(bs, k_tail_len_specs, &out->tail_data_length) != JXL_BS_OK) {
        jxl_jbr_header_free(alloc, out);
        return bs_to_jbr(JXL_BS_EOF);
    }

    has_padding = 0;
    if (jxl_bs_read_bool(bs, &has_padding) != JXL_BS_OK) {
        jxl_jbr_header_free(alloc, out);
        return bs_to_jbr(JXL_BS_EOF);
    }
    if (has_padding) {
        jxl_jbr_status st = parse_padding(alloc, bs, &out->padding);
        if (st != JXL_JBR_OK) {
            jxl_jbr_header_free(alloc, out);
            return st;
        }
    }

    return JXL_JBR_OK;
}

static size_t app_data_len(const jxl_jbr_header *h) {
    size_t i;
    size_t sum = 0;
    for (i = 0; i < h->app_markers_len; ++i) {
        if (h->app_markers[i].ty == 0) {
            sum += h->app_markers[i].length;
        }
    }
    return sum;
}

size_t jxl_jbr_header_expected_data_len(const jxl_jbr_header *h) {
    size_t i;
    size_t com;
    size_t inter;
    if (h == NULL) {
        return 0;
    }
    com = 0;
    for (i = 0; i < h->com_lengths_len; ++i) {
        com += h->com_lengths[i];
    }
    inter = 0;
    for (i = 0; i < h->intermarker_lengths_len; ++i) {
        inter += h->intermarker_lengths[i];
    }
    return app_data_len(h) + com + inter + h->tail_data_length;
}

size_t jxl_jbr_header_expected_icc_len(const jxl_jbr_header *h) {
    size_t i;
    size_t sum;
    if (h == NULL) {
        return 0;
    }
    sum = 0;
    for (i = 0; i < h->app_markers_len; ++i) {
        if (h->app_markers[i].ty == 1) {
            sum += h->app_markers[i].length - 5 - (sizeof(k_header_icc) - 1);
        }
    }
    return sum;
}

size_t jxl_jbr_header_expected_exif_len(const jxl_jbr_header *h) {
    size_t i;
    if (h == NULL) {
        return 0;
    }
    for (i = 0; i < h->app_markers_len; ++i) {
        if (h->app_markers[i].ty == 2) {
            return h->app_markers[i].length - 3 - (sizeof(k_header_exif) - 1);
        }
    }
    return 0;
}

size_t jxl_jbr_header_expected_xmp_len(const jxl_jbr_header *h) {
    size_t i;
    if (h == NULL) {
        return 0;
    }
    for (i = 0; i < h->app_markers_len; ++i) {
        if (h->app_markers[i].ty == 3) {
            return h->app_markers[i].length - 3 - (sizeof(k_header_xmp) - 1);
        }
    }
    return 0;
}

int jxl_jbr_scan_more_info_has_reset(const jxl_jbr_scan_more_info *smi, uint32_t block_idx) {
    size_t i;
    if (smi == NULL) {
        return 0;
    }
    for (i = 0; i < smi->reset_points_len; ++i) {
        if (smi->reset_points[i] == block_idx) {
            return 1;
        }
    }
    return 0;
}

int jxl_jbr_scan_more_info_has_extra_zero_runs(const jxl_jbr_scan_more_info *smi,
                                               uint32_t block_idx, uint32_t *value_out) {
                                                   size_t i;
    if (smi == NULL) {
        return 0;
    }
    for (i = 0; i < smi->ezr_len; ++i) {
        if (smi->ezr_keys[i] == block_idx) {
            if (value_out != NULL) {
                *value_out = smi->ezr_vals[i];
            }
            return 1;
        }
    }
    return 0;
}

uint32_t jxl_jbr_scan_more_info_extra_zero_runs(const jxl_jbr_scan_more_info *smi,
                                                uint32_t block_idx) {
    uint32_t value = 0;
    if (jxl_jbr_scan_more_info_has_extra_zero_runs(smi, block_idx, &value)) {
        return value;
    }
    return 0;
}
