// SPDX-License-Identifier: MIT OR Apache-2.0
#ifndef JXL_BITSTREAM_CONSTS_H_
#define JXL_BITSTREAM_CONSTS_H_

#include <stddef.h>
#include "jxl_oxide/jxl_types.h"

#define JXL_CODESTREAM_SIG_LEN 2u
#define JXL_CONTAINER_SIG_LEN 12u

extern const uint8_t jxl_codestream_sig[JXL_CODESTREAM_SIG_LEN];
extern const uint8_t jxl_container_sig[JXL_CONTAINER_SIG_LEN];

#endif /* JXL_BITSTREAM_CONSTS_H_ */
