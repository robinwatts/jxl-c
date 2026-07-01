// SPDX-License-Identifier: MIT OR Apache-2.0
#include "render/simd/features.h"

#include "context.h"

#include <stddef.h>

#if defined(__x86_64__) || defined(_M_X64) || defined(__i386__) || defined(_M_IX86)
#if defined(_MSC_VER)
#include <intrin.h>
#else
#include <cpuid.h>
#endif
#endif

#if defined(__aarch64__) || defined(_M_ARM64)
#if defined(__linux__)
#include <sys/auxv.h>
#ifndef HWCAP_ASIMD
#define HWCAP_ASIMD (1 << 1)
#endif
#elif defined(__APPLE__)
#include <sys/sysctl.h>
#include <sys/types.h>
#endif
#endif

static jxl_cpu_features g_standalone_cpu;
static int g_standalone_cpu_init;

static const jxl_cpu_features *standalone_cpu_features(void) {
    if (!g_standalone_cpu_init) {
        jxl_cpu_features_detect(&g_standalone_cpu);
        g_standalone_cpu_init = 1;
    }
    return &g_standalone_cpu;
}

void jxl_cpu_features_detect(jxl_cpu_features *out) {
    jxl_cpu_features f;

    if (out == NULL) {
        return;
    }
    f.sse41 = 0;
    f.avx2 = 0;
    f.fma = 0;
    f.neon = 0;

#if defined(__x86_64__) || defined(_M_X64) || defined(__i386__) || defined(_M_IX86)
#if defined(_MSC_VER)
    {
        int info[4];
        __cpuid(info, 1);
        const unsigned ecx = (unsigned)info[2];
        f.sse41 = (ecx >> 19) & 1u;
        f.fma = (ecx >> 12) & 1u;

        __cpuid(info, 7);
        const unsigned ebx = (unsigned)info[1];
        f.avx2 = (ebx >> 5) & 1u;
    }
#else
    {
        unsigned eax, ebx, ecx, edx;
        if (__get_cpuid(1, &eax, &ebx, &ecx, &edx)) {
            f.sse41 = (ecx >> 19) & 1u;
            f.fma = (ecx >> 12) & 1u;
        }
        if (__get_cpuid_max(0, NULL) >= 7) {
            if (__get_cpuid_count(7, 0, &eax, &ebx, &ecx, &edx)) {
                f.avx2 = (ebx >> 5) & 1u;
            }
        }
    }
#endif
#endif

#if defined(__aarch64__) || defined(_M_ARM64)
#if defined(__linux__)
    {
        unsigned long hwcap = getauxval(AT_HWCAP);
        f.neon = (hwcap & HWCAP_ASIMD) != 0;
    }
#elif defined(__APPLE__)
    {
        int val = 0;
        size_t len = sizeof(val);
        if (sysctlbyname("hw.optional.neon", &val, &len, NULL, 0) == 0) {
            f.neon = val != 0;
        } else {
            f.neon = 1;
        }
    }
#else
    f.neon = 1;
#endif
#endif

    *out = f;
}

const jxl_cpu_features *jxl_context_cpu_features(jxl_context *ctx) {
    if (ctx == NULL) {
        return standalone_cpu_features();
    }
    if (!ctx->cpu_features.initialized) {
        jxl_cpu_features_detect(&ctx->cpu_features.features);
        ctx->cpu_features.initialized = 1;
    }
    return &ctx->cpu_features.features;
}
