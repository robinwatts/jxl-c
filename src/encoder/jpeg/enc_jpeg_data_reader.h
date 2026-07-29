// Copyright (c) the JPEG XL Project Authors. All rights reserved.
//
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file.

// Functions for reading a jpeg byte stream into a jxl_jpeg_data object.

#ifndef LIB_JXL_JPEG_ENC_JPEG_DATA_READER_H_
#define LIB_JXL_JPEG_ENC_JPEG_DATA_READER_H_

#include <stddef.h>
#include <stdint.h>

#include "base/enc_status.h"
#include "jpeg/jpeg_data.h"

// Parses the JPEG stream contained in data[0 ... len) and fills in *jpg with
// the parsed information.
// Returns false if the data is not valid JPEG, or if it contains an unsupported
// JPEG feature.
jxl_enc_status jxl_read_jpeg(const uint8_t* data, size_t len,
                jxl_jpeg_data* jpg);


#endif  // LIB_JXL_JPEG_ENC_JPEG_DATA_READER_H_
