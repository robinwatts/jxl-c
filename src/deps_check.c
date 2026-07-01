// SPDX-License-Identifier: MIT OR Apache-2.0
#include "deps_check.h"

#include <brotli/decode.h>
#include "jxl_oxide/jxl_types.h"

#if defined(JXL_OXIDE_C_HAVE_LCMS2)
#include <lcms2.h>
#endif

jxl_status_t jxl_deps_self_test(void) {
    BrotliDecoderState *brotli = BrotliDecoderCreateInstance(NULL, NULL, NULL);
    if (brotli == NULL) {
        return JXL_ERROR_OUT_OF_MEMORY;
    }
    BrotliDecoderDestroyInstance(brotli);

#if defined(JXL_OXIDE_C_HAVE_LCMS2)
    cmsContext cms = cmsCreateContext(NULL, NULL);
    if (cms == NULL) {
        return JXL_ERROR_OUT_OF_MEMORY;
    }
    cmsDeleteContext(cms);
#else
    return JXL_ERROR_UNSUPPORTED;
#endif

    return JXL_OK;
}
