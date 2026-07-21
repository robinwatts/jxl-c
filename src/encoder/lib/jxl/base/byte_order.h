// Copyright (c) the JPEG XL Project Authors. All rights reserved.
//
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file.

#ifndef LIB_JXL_BASE_BYTE_ORDER_H_
#define LIB_JXL_BASE_BYTE_ORDER_H_

#include <stdint.h>
#include <string.h>  // memcpy

#include "lib/jxl/base/compiler_specific.h"

#if JXL_COMPILER_MSVC
#include <intrin.h>  // _byteswap_*
#endif

#if (defined(__BYTE_ORDER__) && (__BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__))
#define JXL_BYTE_ORDER_LITTLE 1
#else
// This means that we don't know that the byte order is little endian, in
// this case we use endian-neutral code that works for both little- and
// big-endian.
#define JXL_BYTE_ORDER_LITTLE 0
#endif

#if JXL_COMPILER_MSVC
#define JXL_BSWAP32(x) _byteswap_ulong(x)
#else
#define JXL_BSWAP32(x) __builtin_bswap32(x)
#endif

static JXL_INLINE uint32_t jxl_load_be16(const uint8_t* p) {
  const uint32_t byte1 = p[0];
  const uint32_t byte0 = p[1];
  return (byte1 << 8) | byte0;
}

static JXL_INLINE uint32_t jxl_load_le16(const uint8_t* p) {
  const uint32_t byte0 = p[0];
  const uint32_t byte1 = p[1];
  return (byte1 << 8) | byte0;
}

static JXL_INLINE uint32_t jxl_load_be32(const uint8_t* p) {
#if JXL_BYTE_ORDER_LITTLE
  uint32_t big;
  memcpy(&big, p, 4);
  return JXL_BSWAP32(big);
#else
  // Byte-order-independent - can't assume this machine is big endian.
  const uint32_t byte3 = p[0];
  const uint32_t byte2 = p[1];
  const uint32_t byte1 = p[2];
  const uint32_t byte0 = p[3];
  return (byte3 << 24) | (byte2 << 16) | (byte1 << 8) | byte0;
#endif
}

static JXL_INLINE uint32_t jxl_load_le32(const uint8_t* p) {
#if JXL_BYTE_ORDER_LITTLE
  uint32_t little;
  memcpy(&little, p, 4);
  return little;
#else
  // Byte-order-independent - can't assume this machine is big endian.
  const uint32_t byte0 = p[0];
  const uint32_t byte1 = p[1];
  const uint32_t byte2 = p[2];
  const uint32_t byte3 = p[3];
  return (byte3 << 24) | (byte2 << 16) | (byte1 << 8) | byte0;
#endif
}

static JXL_INLINE uint64_t jxl_load_le64(const uint8_t* p) {
#if JXL_BYTE_ORDER_LITTLE
  uint64_t little;
  memcpy(&little, p, 8);
  return little;
#else
  // Byte-order-independent - can't assume this machine is big endian.
  const uint64_t byte0 = p[0];
  const uint64_t byte1 = p[1];
  const uint64_t byte2 = p[2];
  const uint64_t byte3 = p[3];
  const uint64_t byte4 = p[4];
  const uint64_t byte5 = p[5];
  const uint64_t byte6 = p[6];
  const uint64_t byte7 = p[7];
  return (byte7 << 56) | (byte6 << 48) | (byte5 << 40) | (byte4 << 32) |
         (byte3 << 24) | (byte2 << 16) | (byte1 << 8) | byte0;
#endif
}

static JXL_INLINE void jxl_store_be32(const uint32_t native, uint8_t* p) {
#if JXL_BYTE_ORDER_LITTLE
  const uint32_t big = JXL_BSWAP32(native);
  memcpy(p, &big, 4);
#else
  // Byte-order-independent - can't assume this machine is big endian.
  p[0] = native >> 24;
  p[1] = (native >> 16) & 0xFF;
  p[2] = (native >> 8) & 0xFF;
  p[3] = native & 0xFF;
#endif
}

#endif  // LIB_JXL_BASE_BYTE_ORDER_H_
