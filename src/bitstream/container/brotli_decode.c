// SPDX-License-Identifier: MIT OR Apache-2.0
#include "brotli_decode.h"

#include <brotli/decode.h>
#include <string.h>

struct jxl_brotli_decoder {
    BrotliDecoderState *state;
    jxl_allocator_state *alloc;
    uint8_t *out;
    size_t out_len;
    size_t out_cap;
    int finished;
};

static void *brotli_jxl_alloc(void *opaque, size_t size) {
    jxl_allocator_state *alloc = (jxl_allocator_state *)opaque;
    if (alloc == NULL || size == 0) {
        return NULL;
    }
    return jxl_alloc(alloc, size);
}

static void brotli_jxl_free(void *opaque, void *address) {
    jxl_allocator_state *alloc = (jxl_allocator_state *)opaque;
    if (alloc == NULL) {
        return;
    }
    jxl_free(alloc, address);
}

static BrotliDecoderState *brotli_create_state(jxl_allocator_state *alloc) {
    if (alloc == NULL) {
        return NULL;
    }
    return BrotliDecoderCreateInstance(brotli_jxl_alloc, brotli_jxl_free, alloc);
}

jxl_brotli_decoder *jxl_brotli_decoder_create(jxl_allocator_state *alloc) {
    jxl_brotli_decoder *dec = jxl_calloc(alloc, 1, sizeof(*dec));
    if (dec == NULL) {
        return NULL;
    }
    dec->alloc = alloc;
    dec->state = brotli_create_state(alloc);
    if (dec->state == NULL) {
        jxl_free(alloc, dec);
        return NULL;
    }
    return dec;
}

void jxl_brotli_decoder_destroy(jxl_allocator_state *alloc, jxl_brotli_decoder *dec) {
    if (dec == NULL) {
        return;
    }
    BrotliDecoderDestroyInstance(dec->state);
    jxl_free(alloc != NULL ? alloc : dec->alloc, dec->out);
    jxl_free(alloc != NULL ? alloc : dec->alloc, dec);
}

void jxl_brotli_decoder_reset(jxl_brotli_decoder *dec) {
    if (dec == NULL) {
        return;
    }
    BrotliDecoderDestroyInstance(dec->state);
    dec->state = brotli_create_state(dec->alloc);
    dec->out_len = 0;
    dec->finished = 0;
}

static jxl_bs_status_t grow_out(jxl_brotli_decoder *dec, size_t need) {
    size_t new_cap;
    uint8_t *grown;
    if (need <= dec->out_cap) {
        return JXL_BS_OK;
    }
    new_cap = dec->out_cap == 0 ? 4096 : dec->out_cap;
    while (new_cap < need) {
        if (new_cap > (SIZE_MAX / 2)) {
            return JXL_BS_VALIDATION_FAILED;
        }
        new_cap *= 2;
    }
    grown = jxl_realloc(dec->alloc, dec->out, new_cap);
    if (grown == NULL) {
        return JXL_BS_EOF;
    }
    dec->out = grown;
    dec->out_cap = new_cap;
    return JXL_BS_OK;
}

jxl_bs_status_t jxl_brotli_decoder_feed(jxl_brotli_decoder *dec, const uint8_t *data, size_t len) {
    size_t available_in;
    const uint8_t *next_in;
    BrotliDecoderResult res;
    if (dec == NULL || dec->state == NULL || dec->finished) {
        return JXL_BS_VALIDATION_FAILED;
    }

    next_in = data;
    available_in = len;

    while (available_in > 0 || BrotliDecoderHasMoreOutput(dec->state)) {
        size_t total_out;
        uint8_t *next_out = dec->out + dec->out_len;
        size_t available_out = dec->out_cap - dec->out_len;
        if (available_out == 0) {
            jxl_bs_status_t st = grow_out(dec, dec->out_len + 4096);
            if (st != JXL_BS_OK) {
                return st;
            }
            continue;
        }

        total_out = 0;
        res = BrotliDecoderDecompressStream(
            dec->state, &available_in, &next_in, &available_out, &next_out, &total_out);

        dec->out_len += total_out;

        if (res == BROTLI_DECODER_RESULT_SUCCESS) {
            dec->finished = 1;
            return JXL_BS_OK;
        }
        if (res == BROTLI_DECODER_RESULT_NEEDS_MORE_INPUT) {
            return JXL_BS_OK;
        }
        if (res == BROTLI_DECODER_RESULT_NEEDS_MORE_OUTPUT) {
            jxl_bs_status_t st = grow_out(dec, dec->out_len + 4096);
            if (st != JXL_BS_OK) {
                return st;
            }
            continue;
        }
        return JXL_BS_VALIDATION_FAILED;
    }

    return JXL_BS_OK;
}

jxl_bs_status_t jxl_brotli_decoder_finish(jxl_brotli_decoder *dec) {
    if (dec == NULL || dec->state == NULL) {
        return JXL_BS_VALIDATION_FAILED;
    }
    if (dec->finished) {
        return JXL_BS_OK;
    }
    return jxl_brotli_decoder_feed(dec, NULL, 0);
}

const uint8_t *jxl_brotli_decoder_output(const jxl_brotli_decoder *dec, size_t *len) {
    if (len != NULL) {
        *len = dec != NULL ? dec->out_len : 0;
    }
    return dec != NULL ? dec->out : NULL;
}

int jxl_brotli_decoder_is_finished(const jxl_brotli_decoder *dec) {
    return dec != NULL && dec->finished;
}
