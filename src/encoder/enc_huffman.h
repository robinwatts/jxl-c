// Copyright (c) the JPEG XL Project Authors. All rights reserved.
//
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file.

#ifndef LIB_JXL_ENC_HUFFMAN_H_
#define LIB_JXL_ENC_HUFFMAN_H_

#include <stddef.h>
#include <stdint.h>

#include "base/enc_status.h"
#include "enc_bit_writer.h"


// Builds a Huffman tree for the given histogram, and encodes it into writer
// in a format suitable for Huffman bitstream decoding.
// An allotment for `writer` must already have been created by the caller.
jxl_enc_status jxl_build_and_store_huffman_tree(const uint32_t* histogram, size_t length,
                                uint8_t* depth, uint16_t* bits,
                                jxl_bit_writer* writer);


#endif  // LIB_JXL_ENC_HUFFMAN_H_
