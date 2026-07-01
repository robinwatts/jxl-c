// SPDX-License-Identifier: MIT OR Apache-2.0
#ifndef JXL_CONFORMANCE_NPY_H_
#define JXL_CONFORMANCE_NPY_H_

#include <stddef.h>
#include "jxl_oxide/jxl_types.h"

typedef struct {
    float *samples;
    uint32_t width;
    uint32_t height;
    uint32_t channels;
    uint32_t num_frames;
} jxl_conformance_npy;

/* Load interleaved float32 .npy (all keyframes when shape is 4D). Returns 0 on success. */
int jxl_conformance_npy_load(const char *path, jxl_conformance_npy *out);

void jxl_conformance_npy_free(jxl_conformance_npy *npy);

/* Resolve crates/jxl-oxide-tests/tests/cache/<hash>.npy from JXL_OXIDE_CACHE_DIR. */
int jxl_conformance_cache_npy_path(const char *hash, char *out, size_t out_len);

int jxl_conformance_cache_icc_path(const char *hash, char *out, size_t out_len);

/* Load raw file bytes. Caller frees *out_data. */
int jxl_conformance_file_load(const char *path, uint8_t **out_data, size_t *out_len);

#endif /* JXL_CONFORMANCE_NPY_H_ */
