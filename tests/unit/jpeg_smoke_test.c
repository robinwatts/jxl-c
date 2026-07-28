/* Copyright (c) the JPEG XL Project Authors. All rights reserved.
 *
 * Use of this source code is governed by a BSD-style
 * license that can be found in the LICENSE file.
 */

#include <jxl/context.h>
#include <jxl/encode.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifndef JPEG_SMOKE_JPEG_DEFAULT
#define JPEG_SMOKE_JPEG_DEFAULT "testdata/jpeg/smoke.jpg"
#endif

static int read_file(const char* path, uint8_t** data, size_t* size) {
  FILE* f = fopen(path, "rb");
  if (!f) return 0;
  if (fseek(f, 0, SEEK_END) != 0) {
    fclose(f);
    return 0;
  }
  long len = ftell(f);
  if (len <= 0) {
    fclose(f);
    return 0;
  }
  if (fseek(f, 0, SEEK_SET) != 0) {
    fclose(f);
    return 0;
  }
  *data = (uint8_t*)malloc((size_t)len);
  if (!*data) {
    fclose(f);
    return 0;
  }
  if (fread(*data, 1, (size_t)len, f) != (size_t)len) {
    free(*data);
    fclose(f);
    return 0;
  }
  fclose(f);
  *size = (size_t)len;
  return 1;
}

static int contains_box_type(const uint8_t* data, size_t size,
                             const char box_type[4]) {
  if (size < 8) return 0;
  for (size_t i = 0; i + 8 <= size; ++i) {
    if (memcmp(data + i + 4, box_type, 4) == 0) return 1;
  }
  return 0;
}

static int encode_jpeg(const uint8_t* jpeg, size_t jpeg_size, int store_metadata,
                       uint8_t** out, size_t* out_size) {
  jxl_context* ctx = NULL;
  if (jxl_context_create(NULL, &ctx) != JXL_OK) return 0;
  jxl_encoder* enc = jxl_encoder_create(ctx);
  if (!enc) {
    jxl_context_destroy(ctx);
    return 0;
  }

  if (jxl_encoder_use_container(enc, JXL_TRUE) != JXL_OK) {
    jxl_encoder_destroy(enc);
    jxl_context_destroy(ctx);
    return 0;
  }

  if (store_metadata &&
      jxl_encoder_store_jpeg_metadata(enc, JXL_TRUE) != JXL_OK) {
    jxl_encoder_destroy(enc);
    jxl_context_destroy(ctx);
    return 0;
  }

  jxl_encoder_frame_settings* fs = jxl_encoder_frame_settings_create(enc, NULL);
  if (!fs) {
    jxl_encoder_destroy(enc);
    jxl_context_destroy(ctx);
    return 0;
  }

  if (jxl_encoder_add_jpeg_frame(fs, jpeg, jpeg_size) != JXL_OK) {
    jxl_encoder_destroy(enc);
    jxl_context_destroy(ctx);
    return 0;
  }

  jxl_encoder_close_input(enc);

  size_t cap = 1024 * 1024;
  *out = (uint8_t*)malloc(cap);
  if (!*out) {
    jxl_encoder_destroy(enc);
    jxl_context_destroy(ctx);
    return 0;
  }
  uint8_t* next = *out;
  size_t avail = cap;

  jxl_status_t st;
  while ((st = jxl_encoder_process_output(enc, &next, &avail)) ==
         JXL_NEED_MORE_OUTPUT) {
    size_t used = (size_t)(next - *out);
    cap *= 2;
    uint8_t* grown = (uint8_t*)realloc(*out, cap);
    if (!grown) {
      free(*out);
      jxl_encoder_destroy(enc);
    jxl_context_destroy(ctx);
      return 0;
    }
    *out = grown;
    next = *out + used;
    avail = cap - used;
  }

  if (st != JXL_OK) {
    free(*out);
    jxl_encoder_destroy(enc);
    jxl_context_destroy(ctx);
    return 0;
  }

  *out_size = (size_t)(next - *out);
  jxl_encoder_destroy(enc);
    jxl_context_destroy(ctx);
  return 1;
}

static int check_jxl_container(const uint8_t* out, size_t out_size) {
  if (out_size < 12) return 0;
  static const uint8_t kSig[] = {0, 0, 0, 0x0c, 'J', 'X', 'L', ' ',
                                 0x0d, 0x0a};
  return memcmp(out, kSig, sizeof(kSig)) == 0;
}

int main(int argc, char** argv) {
  const char* jpeg_path =
      argc > 1 ? argv[1] : JPEG_SMOKE_JPEG_DEFAULT;

  uint8_t* jpeg = NULL;
  size_t jpeg_size = 0;
  if (!read_file(jpeg_path, &jpeg, &jpeg_size)) {
    fprintf(stderr, "failed to read JPEG: %s\n", jpeg_path);
    return 1;
  }

  uint8_t* out = NULL;
  size_t out_size = 0;
  if (!encode_jpeg(jpeg, jpeg_size, /*store_metadata=*/0, &out, &out_size)) {
    fprintf(stderr, "basic JPEG encode failed\n");
    free(jpeg);
    return 1;
  }
  if (!check_jxl_container(out, out_size)) {
    fprintf(stderr, "basic encode: bad JXL container signature\n");
    free(out);
    free(jpeg);
    return 1;
  }
  printf("OK: encoded %s -> %zu byte JXL\n", jpeg_path, out_size);
  free(out);

  if (!encode_jpeg(jpeg, jpeg_size, /*store_metadata=*/1, &out, &out_size)) {
    fprintf(stderr, "JPEG metadata encode failed\n");
    free(jpeg);
    return 1;
  }
  free(jpeg);
  if (!check_jxl_container(out, out_size)) {
    fprintf(stderr, "metadata encode: bad JXL container signature\n");
    free(out);
    return 1;
  }
  if (!contains_box_type(out, out_size, "jbrd")) {
    fprintf(stderr, "metadata encode: missing jbrd box\n");
    free(out);
    return 1;
  }
  printf("OK: encoded %s with jbrd metadata -> %zu byte JXL\n", jpeg_path,
         out_size);
  free(out);
  return 0;
}
