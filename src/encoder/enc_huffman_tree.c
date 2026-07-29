// Copyright (c) the JPEG XL Project Authors. All rights reserved.
//
// Use of this source code is governed by a BSD-style
// license that can be found in LICENSE-BSD.

#include "enc_huffman_tree.h"

#include <stddef.h>
#include <stdint.h>

#include "base/array.h"
#include "base/compiler_specific.h"
#include "base/enc_status.h"
#include "base/common.h"


static void jxl_set_depth(jxl_huffman_tree p, jxl_huffman_tree* pool, uint8_t* depth,
                    uint8_t level) {
  if (p.index_left >= 0) {
    ++level;
    jxl_set_depth(pool[p.index_left], pool, depth, level);
    jxl_set_depth(pool[p.index_right_or_value], pool, depth, level);
  } else {
    depth[p.index_right_or_value] = level;
  }
}

// Compare the root nodes, least popular first; indices are in decreasing order
// before sorting is applied.
JXL_INLINE static bool jxl_huffman_tree_less(jxl_huffman_tree v0, jxl_huffman_tree v1) {
  return v0.total_count != v1.total_count
             ? v0.total_count < v1.total_count
             : v0.index_right_or_value > v1.index_right_or_value;
}

static void jxl_huffman_tree_swap_at(jxl_huffman_tree* a, size_t i, size_t j) {
  jxl_huffman_tree tmp = a[i];
  a[i] = a[j];
  a[j] = tmp;
}

static void jxl_huffman_tree_sift_down(jxl_huffman_tree* a, size_t len, size_t hole) {
  for (;;) {
    size_t child = 2 * hole + 1;
    if (child >= len) break;
    size_t right = child + 1;
    if (right < len && jxl_huffman_tree_less(a[child], a[right])) child = right;
    if (!jxl_huffman_tree_less(a[hole], a[child])) break;
    jxl_huffman_tree_swap_at(a, hole, child);
    hole = child;
  }
}

static void jxl_huffman_tree_sort(jxl_array_huffman_tree* tree) {
  jxl_huffman_tree* a = jxl_array_data(tree);
  size_t n = jxl_array_len(tree);
  if (n < 2) return;
  for (size_t i = n / 2; i > 0; --i) {
    jxl_huffman_tree_sift_down(a, n, i - 1);
  }
  while (n > 1) {
    jxl_huffman_tree_swap_at(a, 0, n - 1);
    --n;
    jxl_huffman_tree_sift_down(a, n, 0);
  }
}

// This function will create a Huffman tree.
//
// The catch here is that the tree cannot be arbitrarily deep.
// Brotli specifies a maximum depth of 15 bits for "code trees"
// and 7 bits for "code length code trees."
//
// count_limit is the value that is to be faked as the minimum value
// and this minimum value is raised until the tree matches the
// maximum length requirement.
//
// This algorithm is not of excellent performance for very long data blocks,
// especially when population counts are longer than 2**tree_limit, but
// we are not planning to use this with extremely long blocks.
//
// See http://en.wikipedia.org/wiki/Huffman_coding
void jxl_create_huffman_tree(jxl_context* mm, const uint32_t* data,
                       const size_t length, const int tree_limit,
                       uint8_t* depth) {
  // For block sizes below 64 kB, we never need to do a second iteration
  // of this loop. Probably all of our block sizes will be smaller than
  // that, so this loop is mostly of academic interest. If we actually
  // would need this, we would be better off with the Katajainen algorithm.
  for (uint32_t count_limit = 1;; count_limit *= 2) {
    jxl_array_huffman_tree tree;
    jxl_array_construct_empty(&tree, mm);
    if (!jxl_enc_status_ok(jxl_array_init(&tree, mm))) {
      jxl_array_destroy(&tree);
      return;
    }
    if (!jxl_enc_status_ok(jxl_array_reserve(&tree, 2 * length + 1))) {
      jxl_array_destroy(&tree);
      return;
    }

    for (size_t i = length; i != 0;) {
      --i;
      if (data[i]) {
        const uint32_t count = JXL_MAX(data[i], count_limit - 1);
        if (!jxl_enc_status_ok(jxl_array_huffman_tree_push_back(&tree,
                           jxl_huffman_tree_make(count, -1, (int16_t)(i))))) {
          jxl_array_destroy(&tree);
          return;
        }
      }
    }

    const size_t n = jxl_array_len(&tree);
    if (n == 1) {
      // Fake value; will be fixed on upper level.
      depth[jxl_array_at(&tree, 0)->index_right_or_value] = 1;
      jxl_array_destroy(&tree);
      break;
    }

    jxl_huffman_tree_sort(&tree);

    // The nodes are:
    // [0, n): the sorted leaf nodes that we start with.
    // [n]: we add a sentinel here.
    // [n + 1, 2n): new parent nodes are added here, starting from
    //              (n+1). These are naturally in ascending order.
    // [2n]: we add a sentinel at the end as well.
    // There will be (2n+1) elements at the end.
    const jxl_huffman_tree sentinel = jxl_huffman_tree_make(UINT32_MAX, -1, -1);
    if (!jxl_enc_status_ok(jxl_array_huffman_tree_push_back(&tree, sentinel)) || !jxl_enc_status_ok(jxl_array_huffman_tree_push_back(&tree, sentinel))) {
      jxl_array_destroy(&tree);
      return;
    }

    size_t i = 0;      // Points to the next leaf node.
    size_t j = n + 1;  // Points to the next non-leaf node.
    for (size_t k = n - 1; k != 0; --k) {
      size_t left;
      size_t right;
      if (jxl_array_at(&tree, i)->total_count <= jxl_array_at(&tree, j)->total_count) {
        left = i;
        ++i;
      } else {
        left = j;
        ++j;
      }
      if (jxl_array_at(&tree, i)->total_count <= jxl_array_at(&tree, j)->total_count) {
        right = i;
        ++i;
      } else {
        right = j;
        ++j;
      }

      // The sentinel node becomes the parent node.
      size_t j_end = jxl_array_len(&tree) - 1;
      jxl_array_at(&tree, j_end)->total_count =
          jxl_array_at(&tree, left)->total_count + jxl_array_at(&tree, right)->total_count;
      jxl_array_at(&tree, j_end)->index_left = (int16_t)(left);
      jxl_array_at(&tree, j_end)->index_right_or_value = (int16_t)(right);

      // Add back the last sentinel node.
      if (!jxl_enc_status_ok(jxl_array_huffman_tree_push_back(&tree, sentinel))) {
        jxl_array_destroy(&tree);
        return;
      }
    }
    JXL_DASSERT(jxl_array_len(&tree) == 2 * n + 1);
    jxl_set_depth(*jxl_array_at(&tree, 2 * n - 1), jxl_array_data(&tree), depth, 0);

    // We need to pack the Huffman tree in tree_limit bits.
    // If this was not successful, add fake entities to the lowest values
    // and retry.
    uint8_t max_depth = depth[0];
    for (size_t i = 1; i < length; ++i) {
      if (depth[i] > max_depth) max_depth = depth[i];
    }
    if (max_depth <= tree_limit) {
      jxl_array_destroy(&tree);
      break;
    }
    jxl_array_destroy(&tree);
  }
}

static void jxl_reverse(uint8_t* v, size_t start, size_t end) {
  --end;
  while (start < end) {
    uint8_t tmp = v[start];
    v[start] = v[end];
    v[end] = tmp;
    ++start;
    --end;
  }
}

static void jxl_write_huffman_tree_repetitions(const uint8_t previous_value,
                                 const uint8_t value, size_t repetitions,
                                 size_t* tree_size, uint8_t* tree,
                                 uint8_t* extra_bits_data) {
  JXL_DASSERT(repetitions > 0);
  if (previous_value != value) {
    tree[*tree_size] = value;
    extra_bits_data[*tree_size] = 0;
    ++(*tree_size);
    --repetitions;
  }
  if (repetitions == 7) {
    tree[*tree_size] = value;
    extra_bits_data[*tree_size] = 0;
    ++(*tree_size);
    --repetitions;
  }
  if (repetitions < 3) {
    for (size_t i = 0; i < repetitions; ++i) {
      tree[*tree_size] = value;
      extra_bits_data[*tree_size] = 0;
      ++(*tree_size);
    }
  } else {
    repetitions -= 3;
    size_t start = *tree_size;
    while (true) {
      tree[*tree_size] = 16;
      extra_bits_data[*tree_size] = repetitions & 0x3;
      ++(*tree_size);
      repetitions >>= 2;
      if (repetitions == 0) {
        break;
      }
      --repetitions;
    }
    jxl_reverse(tree, start, *tree_size);
    jxl_reverse(extra_bits_data, start, *tree_size);
  }
}

static void jxl_write_huffman_tree_repetitions_zeros(size_t repetitions, size_t* tree_size,
                                      uint8_t* tree, uint8_t* extra_bits_data) {
  if (repetitions == 11) {
    tree[*tree_size] = 0;
    extra_bits_data[*tree_size] = 0;
    ++(*tree_size);
    --repetitions;
  }
  if (repetitions < 3) {
    for (size_t i = 0; i < repetitions; ++i) {
      tree[*tree_size] = 0;
      extra_bits_data[*tree_size] = 0;
      ++(*tree_size);
    }
  } else {
    repetitions -= 3;
    size_t start = *tree_size;
    while (true) {
      tree[*tree_size] = 17;
      extra_bits_data[*tree_size] = repetitions & 0x7;
      ++(*tree_size);
      repetitions >>= 3;
      if (repetitions == 0) {
        break;
      }
      --repetitions;
    }
    jxl_reverse(tree, start, *tree_size);
    jxl_reverse(extra_bits_data, start, *tree_size);
  }
}

static void jxl_decide_over_rle_use(const uint8_t* depth, const size_t length,
                             bool* use_rle_for_non_zero,
                             bool* use_rle_for_zero) {
  size_t total_reps_zero = 0;
  size_t total_reps_non_zero = 0;
  size_t count_reps_zero = 1;
  size_t count_reps_non_zero = 1;
  for (size_t i = 0; i < length;) {
    const uint8_t value = depth[i];
    size_t reps = 1;
    for (size_t k = i + 1; k < length && depth[k] == value; ++k) {
      ++reps;
    }
    if (reps >= 3 && value == 0) {
      total_reps_zero += reps;
      ++count_reps_zero;
    }
    if (reps >= 4 && value != 0) {
      total_reps_non_zero += reps;
      ++count_reps_non_zero;
    }
    i += reps;
  }
  *use_rle_for_non_zero = total_reps_non_zero > count_reps_non_zero * 2;
  *use_rle_for_zero = total_reps_zero > count_reps_zero * 2;
}

void jxl_write_huffman_tree(const uint8_t* depth, size_t length, size_t* tree_size,
                      uint8_t* tree, uint8_t* extra_bits_data) {
  uint8_t previous_value = 8;

  // Throw away trailing zeros.
  size_t new_length = length;
  for (size_t i = 0; i < length; ++i) {
    if (depth[length - i - 1] == 0) {
      --new_length;
    } else {
      break;
    }
  }

  // First gather statistics on if it is a good idea to do rle.
  bool use_rle_for_non_zero = false;
  bool use_rle_for_zero = false;
  if (length > 50) {
    // Find rle coding for longer codes.
    // Shorter codes seem not to benefit from rle.
    jxl_decide_over_rle_use(depth, new_length, &use_rle_for_non_zero,
                     &use_rle_for_zero);
  }

  // Actual rle coding.
  for (size_t i = 0; i < new_length;) {
    const uint8_t value = depth[i];
    size_t reps = 1;
    if ((value != 0 && use_rle_for_non_zero) ||
        (value == 0 && use_rle_for_zero)) {
      for (size_t k = i + 1; k < new_length && depth[k] == value; ++k) {
        ++reps;
      }
    }
    if (value == 0) {
      jxl_write_huffman_tree_repetitions_zeros(reps, tree_size, tree, extra_bits_data);
    } else {
      jxl_write_huffman_tree_repetitions(previous_value, value, reps, tree_size, tree,
                                  extra_bits_data);
      previous_value = value;
    }
    i += reps;
  }
}

static uint16_t jxl_reverse_bits(int num_bits, uint16_t bits) {
  static const size_t kLut[16] = {// Pre-reversed 4-bit values.
                                  0x0, 0x8, 0x4, 0xc, 0x2, 0xa, 0x6, 0xe,
                                  0x1, 0x9, 0x5, 0xd, 0x3, 0xb, 0x7, 0xf};
  size_t retval = kLut[bits & 0xf];
  for (int i = 4; i < num_bits; i += 4) {
    retval <<= 4;
    bits = (uint16_t)(bits >> 4);
    retval |= kLut[bits & 0xf];
  }
  retval >>= (-num_bits & 0x3);
  return (uint16_t)(retval);
}

void jxl_convert_bit_depths_to_symbols(const uint8_t* depth, size_t len,
                               uint16_t* bits) {
  // In Brotli, all bit depths are [1..15]
  // 0 bit depth means that the symbol does not exist.
  enum { kMaxBits = 16 };  // 0..15 are values for bits
  uint16_t bl_count[kMaxBits] = {0};
  {
    for (size_t i = 0; i < len; ++i) {
      ++bl_count[depth[i]];
    }
    bl_count[0] = 0;
  }
  uint16_t next_code[kMaxBits];
  next_code[0] = 0;
  {
    int code = 0;
    for (size_t i = 1; i < kMaxBits; ++i) {
      code = (code + bl_count[i - 1]) << 1;
      next_code[i] = (uint16_t)(code);
    }
  }
  for (size_t i = 0; i < len; ++i) {
    if (depth[i]) {
      bits[i] = jxl_reverse_bits(depth[i], next_code[depth[i]]++);
    }
  }
}
