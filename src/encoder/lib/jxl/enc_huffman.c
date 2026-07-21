// Copyright (c) the JPEG XL Project Authors. All rights reserved.
//
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file.

#include "lib/jxl/enc_huffman.h"

#include <stddef.h>
#include <stdint.h>

#include "lib/jxl/base/array.h"
#include "lib/jxl/base/common.h"
#include "lib/jxl/base/status.h"
#include "lib/jxl/enc_huffman_tree.h"


enum { kCodeLengthCodes = 18 };

static void jxl_store_huffman_tree_of_huffman_tree_to_bit_mask(const int num_codes,
                                            const uint8_t* code_length_bitdepth,
                                            jxl_bit_writer* writer) {
  static const uint8_t kStorageOrder[kCodeLengthCodes] = {
      1, 2, 3, 4, 0, 5, 17, 6, 16, 7, 8, 9, 10, 11, 12, 13, 14, 15};
  // The bit lengths of the Huffman code over the code length alphabet
  // are compressed with the following static Huffman code:
  //   Symbol   Code
  //   ------   ----
  //   0          00
  //   1        1110
  //   2         110
  //   3          01
  //   4          10
  //   5        1111
  static const uint8_t kHuffmanBitLengthHuffmanCodeSymbols[6] = {0, 7, 3,
                                                                 2, 1, 15};
  static const uint8_t kHuffmanBitLengthHuffmanCodeBitLengths[6] = {2, 4, 3,
                                                                    2, 2, 4};

  // Throw away trailing zeros:
  size_t codes_to_store = kCodeLengthCodes;
  if (num_codes > 1) {
    for (; codes_to_store > 0; --codes_to_store) {
      if (code_length_bitdepth[kStorageOrder[codes_to_store - 1]] != 0) {
        break;
      }
    }
  }
  size_t skip_some = 0;  // skips none.
  if (code_length_bitdepth[kStorageOrder[0]] == 0 &&
      code_length_bitdepth[kStorageOrder[1]] == 0) {
    skip_some = 2;  // skips two.
    if (code_length_bitdepth[kStorageOrder[2]] == 0) {
      skip_some = 3;  // skips three.
    }
  }
  jxl_bit_writer_write(writer, 2, skip_some);
  for (size_t i = skip_some; i < codes_to_store; ++i) {
    size_t l = code_length_bitdepth[kStorageOrder[i]];
    jxl_bit_writer_write(writer, kHuffmanBitLengthHuffmanCodeBitLengths[l],
                  kHuffmanBitLengthHuffmanCodeSymbols[l]);
  }
}

static jxl_status jxl_store_huffman_tree_to_bit_mask(const size_t huffman_tree_size,
                                 const uint8_t* huffman_tree,
                                 const uint8_t* huffman_tree_extra_bits,
                                 const uint8_t* code_length_bitdepth,
                                 const uint16_t* code_length_bitdepth_symbols,
                                 jxl_bit_writer* writer) {
  for (size_t i = 0; i < huffman_tree_size; ++i) {
    size_t ix = huffman_tree[i];
    jxl_bit_writer_write(writer, code_length_bitdepth[ix], code_length_bitdepth_symbols[ix]);
    JXL_ENSURE(ix <= 17);
    // Extra bits
    switch (ix) {
      case 16:
        jxl_bit_writer_write(writer, 2, huffman_tree_extra_bits[i]);
        break;
      case 17:
        jxl_bit_writer_write(writer, 3, huffman_tree_extra_bits[i]);
        break;
      default:
        // no-op
        break;
    }
  }
  return jxl_ok_status();
}

static void jxl_store_simple_huffman_tree(const uint8_t* depths, size_t symbols[4],
                            size_t num_symbols, size_t max_bits,
                            jxl_bit_writer* writer) {
  // value of 1 indicates a simple Huffman code
  jxl_bit_writer_write(writer, 2, 1);
  jxl_bit_writer_write(writer, 2, num_symbols - 1);  // NSYM - 1

  // Sort
  for (size_t i = 0; i < num_symbols; i++) {
    for (size_t j = i + 1; j < num_symbols; j++) {
      if (depths[symbols[j]] < depths[symbols[i]]) {
        jxl_swap(&symbols[j], &symbols[i]);
      }
    }
  }

  if (num_symbols == 2) {
    jxl_bit_writer_write(writer, max_bits, symbols[0]);
    jxl_bit_writer_write(writer, max_bits, symbols[1]);
  } else if (num_symbols == 3) {
    jxl_bit_writer_write(writer, max_bits, symbols[0]);
    jxl_bit_writer_write(writer, max_bits, symbols[1]);
    jxl_bit_writer_write(writer, max_bits, symbols[2]);
  } else {
    jxl_bit_writer_write(writer, max_bits, symbols[0]);
    jxl_bit_writer_write(writer, max_bits, symbols[1]);
    jxl_bit_writer_write(writer, max_bits, symbols[2]);
    jxl_bit_writer_write(writer, max_bits, symbols[3]);
    // tree-select
    jxl_bit_writer_write(writer, 1, depths[symbols[0]] == 1 ? 1 : 0);
  }
}

// num = alphabet size
// depths = symbol depths
static jxl_status jxl_store_huffman_tree_body(const uint8_t* depths, size_t num, jxl_bit_writer* writer,
                            jxl_array_u8* arena) {
  // Write the Huffman tree into the compact representation.
  JXL_RETURN_IF_ERROR(jxl_array_init(arena, jxl_bit_writer_memory_manager(writer)));
  JXL_RETURN_IF_ERROR(jxl_array_resize(arena, 2 * num));
  uint8_t* huffman_tree = jxl_array_data(arena);
  uint8_t* huffman_tree_extra_bits = jxl_array_data(arena) + num;
  size_t huffman_tree_size = 0;
  jxl_write_huffman_tree(depths, num, &huffman_tree_size, huffman_tree,
                   huffman_tree_extra_bits);

  // Calculate the statistics of the Huffman tree in the compact representation.
  uint32_t huffman_tree_histogram[kCodeLengthCodes] = {0};
  for (size_t i = 0; i < huffman_tree_size; ++i) {
    ++huffman_tree_histogram[huffman_tree[i]];
  }

  int num_codes = 0;
  int code = 0;
  for (int i = 0; i < kCodeLengthCodes; ++i) {
    if (huffman_tree_histogram[i]) {
      if (num_codes == 0) {
        code = i;
        num_codes = 1;
      } else if (num_codes == 1) {
        num_codes = 2;
        break;
      }
    }
  }

  // Calculate another Huffman tree to use for compressing both the
  // earlier Huffman tree with.
  uint8_t code_length_bitdepth[kCodeLengthCodes] = {0};
  uint16_t code_length_bitdepth_symbols[kCodeLengthCodes] = {0};
  jxl_create_huffman_tree(jxl_bit_writer_memory_manager(writer),
                    &huffman_tree_histogram[0], kCodeLengthCodes, 5,
                    &code_length_bitdepth[0]);
  jxl_convert_bit_depths_to_symbols(code_length_bitdepth, kCodeLengthCodes,
                            &code_length_bitdepth_symbols[0]);

  // Now, we have all the data, let's start storing it
  jxl_store_huffman_tree_of_huffman_tree_to_bit_mask(num_codes, code_length_bitdepth,
                                         writer);

  if (num_codes == 1) {
    code_length_bitdepth[code] = 0;
  }

  // Store the real huffman tree now.
  JXL_RETURN_IF_ERROR(jxl_store_huffman_tree_to_bit_mask(
      huffman_tree_size, huffman_tree, huffman_tree_extra_bits,
      &code_length_bitdepth[0], code_length_bitdepth_symbols, writer));
  return jxl_ok_status();
}

static jxl_status jxl_store_huffman_tree(const uint8_t* depths, size_t num, jxl_bit_writer* writer) {
  jxl_array_u8 arena;
  jxl_array_construct_empty(&arena, jxl_bit_writer_memory_manager(writer));
  jxl_status status = jxl_store_huffman_tree_body(depths, num, writer, &arena);
  jxl_array_destroy(&arena);
  return status;
}

jxl_status jxl_build_and_store_huffman_tree(const uint32_t* histogram, const size_t length,
                                uint8_t* depth, uint16_t* bits,
                                jxl_bit_writer* writer) {
  size_t count = 0;
  size_t s4[4] = {0};
  for (size_t i = 0; i < length; i++) {
    if (histogram[i]) {
      if (count < 4) {
        s4[count] = i;
      } else if (count > 4) {
        break;
      }
      count++;
    }
  }

  size_t max_bits_counter = length - 1;
  size_t max_bits = 0;
  while (max_bits_counter) {
    max_bits_counter >>= 1;
    ++max_bits;
  }

  if (count <= 1) {
    // Output symbol bits and depths are initialized with 0, nothing to do.
    jxl_bit_writer_write(writer, 4, 1);
    jxl_bit_writer_write(writer, max_bits, s4[0]);
    return jxl_ok_status();
  }

  jxl_create_huffman_tree(jxl_bit_writer_memory_manager(writer), histogram,
                    length, 15, depth);
  jxl_convert_bit_depths_to_symbols(depth, length, bits);

  if (count <= 4) {
    jxl_store_simple_huffman_tree(depth, s4, count, max_bits, writer);
  } else {
    JXL_RETURN_IF_ERROR(jxl_store_huffman_tree(depth, length, writer));
  }
  return jxl_ok_status();
}
