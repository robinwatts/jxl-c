// SPDX-License-Identifier: MIT OR Apache-2.0
#ifndef JXL_RENDER_SIMD_FEATURES_H_
#define JXL_RENDER_SIMD_FEATURES_H_

#include "allocator.h"

typedef struct jxl_context jxl_context;

typedef struct {
    unsigned sse41;
    unsigned avx2;
    unsigned fma;
    unsigned neon;
} jxl_cpu_features;

void jxl_cpu_features_detect(jxl_cpu_features *out);

const jxl_cpu_features *jxl_context_cpu_features(jxl_context *ctx);

static inline const jxl_cpu_features *jxl_cpu_features_local(jxl_cpu_features *storage) {
    jxl_cpu_features_detect(storage);
    return storage;
}

#define JXL_CPU_FEATURES_LOCAL(name) \
    jxl_cpu_features name##_storage; \
    const jxl_cpu_features *name = jxl_cpu_features_local(&name##_storage)

#endif /* JXL_RENDER_SIMD_FEATURES_H_ */
