// SPDX-License-Identifier: MIT OR Apache-2.0
#include "jbr/data.h"

#include "bitstream/container/brotli_decode.h"

#include <string.h>

struct jxl_jbr_data {
    jxl_allocator_state *alloc;
    jxl_jbr_header header;
    int header_parsed;
    jxl_brotli_decoder *brotli;
    uint8_t *decompressed;
    size_t decompressed_len;
    int finalized;
};

jxl_jbr_data *jxl_jbr_data_create(jxl_allocator_state *alloc) {
    jxl_jbr_data *data = jxl_calloc(alloc, 1, sizeof(*data));
    if (data == NULL) {
        return NULL;
    }
    data->alloc = alloc;
    jxl_jbr_header_init(&data->header);
    return data;
}

void jxl_jbr_data_destroy(jxl_allocator_state *alloc, jxl_jbr_data *data) {
    if (data == NULL) {
        return;
    }
    jxl_allocator_state *a = alloc != NULL ? alloc : data->alloc;
    jxl_jbr_header_free(a, &data->header);
    if (data->brotli != NULL) {
        jxl_brotli_decoder_destroy(a, data->brotli);
    }
    jxl_free(a, data->decompressed);
    jxl_free(a, data);
}

static jxl_jbr_status start_from_buffer(jxl_allocator_state *alloc, jxl_jbr_data *data,
                                        const uint8_t *buf, size_t len) {
    jxl_bs bs;
    jxl_bs_init(&bs, buf, len);
    jxl_jbr_status st = jxl_jbr_header_parse(alloc, &bs, &data->header);
    if (st == JXL_JBR_NEED_MORE_DATA) {
        return st;
    }
    if (st != JXL_JBR_OK) {
        return st;
    }
    if (jxl_bs_zero_pad_to_byte(&bs) != JXL_BS_OK) {
        return JXL_JBR_BITSTREAM_ERROR;
    }
    size_t bytes_read = bs.num_read_bits / 8;
    if (bytes_read > len) {
        return JXL_JBR_BITSTREAM_ERROR;
    }
    data->header_parsed = 1;
    data->brotli = jxl_brotli_decoder_create(alloc);
    if (data->brotli == NULL) {
        return JXL_JBR_OUT_OF_MEMORY;
    }
    if (bytes_read < len) {
        jxl_bs_status_t bst =
            jxl_brotli_decoder_feed(data->brotli, buf + bytes_read, len - bytes_read);
        if (bst != JXL_BS_OK) {
            return JXL_JBR_BROTLI_ERROR;
        }
    }
    return JXL_JBR_OK;
}

jxl_jbr_status jxl_jbr_data_try_parse(jxl_allocator_state *alloc, const uint8_t *buf, size_t len,
                                      jxl_jbr_data **out) {
    if (alloc == NULL || out == NULL) {
        return JXL_JBR_BITSTREAM_ERROR;
    }
    jxl_jbr_data *data = jxl_jbr_data_create(alloc);
    if (data == NULL) {
        return JXL_JBR_OUT_OF_MEMORY;
    }
    jxl_jbr_status st = start_from_buffer(alloc, data, buf, len);
    if (st == JXL_JBR_NEED_MORE_DATA) {
        jxl_jbr_data_destroy(alloc, data);
        return JXL_JBR_NEED_MORE_DATA;
    }
    if (st != JXL_JBR_OK) {
        jxl_jbr_data_destroy(alloc, data);
        return st;
    }
    *out = data;
    return JXL_JBR_OK;
}

jxl_jbr_status jxl_jbr_data_feed(jxl_allocator_state *alloc, jxl_jbr_data *data, const uint8_t *buf,
                                 size_t len) {
    if (data == NULL) {
        return JXL_JBR_BITSTREAM_ERROR;
    }
    jxl_allocator_state *a = alloc != NULL ? alloc : data->alloc;
    if (!data->header_parsed) {
        return start_from_buffer(a, data, buf, len);
    }
    if (data->brotli == NULL) {
        return JXL_JBR_INVALID_DATA;
    }
    if (jxl_brotli_decoder_is_finished(data->brotli)) {
        return JXL_JBR_OK;
    }
    if (len > 0 && jxl_brotli_decoder_feed(data->brotli, buf, len) != JXL_BS_OK) {
        return JXL_JBR_BROTLI_ERROR;
    }
    return JXL_JBR_OK;
}

jxl_jbr_status jxl_jbr_data_finalize(jxl_allocator_state *alloc, jxl_jbr_data *data) {
    size_t out_len;
    if (data == NULL || !data->header_parsed || data->brotli == NULL) {
        return JXL_JBR_INVALID_DATA;
    }
    jxl_allocator_state *a = alloc != NULL ? alloc : data->alloc;
    if (jxl_brotli_decoder_finish(data->brotli) != JXL_BS_OK) {
        return JXL_JBR_BROTLI_ERROR;
    }
    out_len = 0;
    const uint8_t *out = jxl_brotli_decoder_output(data->brotli, &out_len);
    size_t expected = jxl_jbr_header_expected_data_len(&data->header);
    if (out_len != expected) {
        return JXL_JBR_INVALID_DATA;
    }
    data->decompressed = NULL;
    if (out_len > 0) {
        data->decompressed = jxl_alloc(a, out_len);
        if (data->decompressed == NULL) {
            return JXL_JBR_OUT_OF_MEMORY;
        }
        memcpy(data->decompressed, out, out_len);
    }
    data->decompressed_len = out_len;
    jxl_brotli_decoder_destroy(a, data->brotli);
    data->brotli = NULL;
    data->finalized = 1;
    return JXL_JBR_OK;
}

const jxl_jbr_header *jxl_jbr_data_header(const jxl_jbr_data *data) {
    return data != NULL && data->header_parsed ? &data->header : NULL;
}

const uint8_t *jxl_jbr_data_payload(const jxl_jbr_data *data, size_t *len_out) {
    if (data == NULL || !data->finalized || len_out == NULL) {
        return NULL;
    }
    *len_out = data->decompressed_len;
    return data->decompressed;
}
