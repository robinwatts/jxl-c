// SPDX-License-Identifier: MIT OR Apache-2.0
#ifndef JXL_ORACLE_GOLDEN_BUF_ZST_H_
#define JXL_ORACLE_GOLDEN_BUF_ZST_H_

#include "jxl_oxide/jxl_oxide.h"

#include <stddef.h>
#include "jxl_oxide/jxl_types.h"

typedef struct {
    uint8_t *data;
    size_t len;
    size_t pos;
    uint32_t width;
    uint32_t height;
    uint32_t channels;
} jxl_golden_buf;

/* Load and decompress output.buf.zst. Returns 0 on success. */
int jxl_golden_load_file(const char *path, jxl_golden_buf *out);

/* Load golden from fixture dir (local output.buf.zst or fetch via output.buf.zst.url). */
int jxl_golden_load_fixture(const char *fixtures_dir, const char *fixture_name,
                            jxl_golden_buf *out);

void jxl_golden_free(jxl_golden_buf *golden);

/*
 * Compare render planes to the next keyframe in the golden stream.
 * Returns 0 on match, 1 on pixel/header mismatch, -1 on I/O or format error.
 */
int jxl_golden_compare_render(jxl_golden_buf *golden, const jxl_render *render,
                              const jxl_image_header *header, const char *fixture_name);

#endif /* JXL_ORACLE_GOLDEN_BUF_ZST_H_ */
