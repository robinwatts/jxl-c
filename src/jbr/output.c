// SPDX-License-Identifier: MIT OR Apache-2.0
#include "jbr/output.h"

#include <string.h>

void jxl_jbr_output_init(jxl_jbr_output *out) {
    if (out != NULL) {
        memset(out, 0, sizeof(*out));
    }
}

void jxl_jbr_output_free(jxl_allocator_state *alloc, jxl_jbr_output *out) {
    if (out == NULL) {
        return;
    }
    jxl_free(alloc, out->data);
    out->data = NULL;
    out->len = out->cap = 0;
}

jxl_jbr_status jxl_jbr_output_write(jxl_allocator_state *alloc, jxl_jbr_output *out,
                                    const uint8_t *data, size_t len) {
    size_t need;
    if (out == NULL || (len > 0 && data == NULL)) {
        return JXL_JBR_WRITE_ERROR;
    }
    if (len == 0) {
        return JXL_JBR_OK;
    }
    need = out->len + len;
    if (need > out->cap) {
        size_t new_cap = out->cap == 0 ? 4096 : out->cap;
        while (new_cap < need) {
            if (new_cap > SIZE_MAX / 2) {
                return JXL_JBR_WRITE_ERROR;
            }
            new_cap *= 2;
        }
        uint8_t *grown = jxl_realloc(alloc, out->data, new_cap);
        if (grown == NULL) {
            return JXL_JBR_OUT_OF_MEMORY;
        }
        out->data = grown;
        out->cap = new_cap;
    }
    memcpy(out->data + out->len, data, len);
    out->len = need;
    return JXL_JBR_OK;
}
