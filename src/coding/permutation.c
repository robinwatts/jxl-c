// SPDX-License-Identifier: MIT OR Apache-2.0
#include "decoder.h"

#include "coding/internal.h"
#include "coding/util.h"

#include <string.h>

static uint32_t permutation_context(uint32_t x) {
    uint32_t ctx = jxl_coding_add_log2_ceil(x);
    return ctx < 7 ? ctx : 7;
}

void jxl_coding_permutation_destroy(jxl_allocator_state *alloc, size_t *permutation) {
    jxl_free(alloc, permutation);
}

jxl_coding_status_t jxl_coding_read_permutation(jxl_allocator_state *alloc, jxl_bs *bs,
                                                jxl_coding_decoder *dec, uint32_t size,
                                                uint32_t skip, size_t **permutation_out,
                                                size_t *permutation_len_out) {
                                                    uint32_t idx;
                                                    uint32_t i;
    uint32_t end;
    uint32_t prev_val;
    jxl_coding_status_t st;
    uint32_t *lehmer;
    size_t *temp;
    size_t *permutation;
    if (permutation_out != NULL) {
        *permutation_out = NULL;
    }
    if (permutation_len_out != NULL) {
        *permutation_len_out = 0;
    }

    end = 0;
    st = jxl_coding_decoder_read_varint(dec, bs, permutation_context(size), &end);
    if (st != JXL_CODING_OK) {
        return st;
    }
    if (end > size - skip) {
        return JXL_CODING_INVALID_PERMUTATION;
    }

    lehmer = jxl_alloc(alloc, (size_t)end * sizeof(uint32_t));
    if (lehmer == NULL && end > 0) {
        return JXL_CODING_OUT_OF_MEMORY;
    }

    prev_val = 0;
    for (idx = 0; idx < end; ++idx) {
        st = jxl_coding_decoder_read_varint(dec, bs, permutation_context(prev_val), &lehmer[idx]);
        if (st != JXL_CODING_OK) {
            jxl_free(alloc, lehmer);
            return st;
        }
        if (lehmer[idx] >= size - skip - idx) {
            jxl_free(alloc, lehmer);
            return JXL_CODING_INVALID_PERMUTATION;
        }
        prev_val = lehmer[idx];
    }

    temp = jxl_alloc(alloc, (size_t)(size - skip) * sizeof(size_t));
    permutation = jxl_alloc(alloc, (size_t)size * sizeof(size_t));
    if (temp == NULL || permutation == NULL) {
        jxl_free(alloc, lehmer);
        jxl_free(alloc, temp);
        jxl_free(alloc, permutation);
        return JXL_CODING_OUT_OF_MEMORY;
    }
    for (i = skip; i < size; ++i) {
        temp[i - skip] = (size_t)i;
    }
    for (i = 0; i < skip; ++i) {
        permutation[i] = (size_t)i;
    }
    for (i = 0; i < end; ++i) {
        idx = (size_t)lehmer[i];
        permutation[skip + i] = temp[idx];
        memmove(&temp[idx], &temp[idx + 1], (size_t)(size - skip - idx - 1) * sizeof(size_t));
    }
    memcpy(permutation + skip + end, temp, (size_t)(size - skip - end) * sizeof(size_t));

    jxl_free(alloc, lehmer);
    jxl_free(alloc, temp);

    if (permutation_out != NULL) {
        *permutation_out = permutation;
    } else {
        jxl_free(alloc, permutation);
    }
    if (permutation_len_out != NULL) {
        *permutation_len_out = size;
    }
    return JXL_CODING_OK;
}
