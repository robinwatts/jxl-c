/* Copyright (c) the JPEG XL Project Authors. All rights reserved.
 *
 * Verifies scalar JPEG encoder output is deterministic and jbrd containers are
 * well-formed.
 */

#include <jxl/context.h>
#include <jxl/encode.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifndef JPEG_ROUNDTRIP_JPEG_DEFAULT
#define JPEG_ROUNDTRIP_JPEG_DEFAULT "testdata/jpeg/smoke.jpg"
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

static int encode_jpeg(const uint8_t* jpeg, size_t jpeg_size, int effort,
                       int store_jbrd, uint8_t** out, size_t* out_size) {
  jxl_context* ctx = NULL;
  if (jxl_context_create(NULL, &ctx) != JXL_OK) return 0;
  jxl_encoder* enc = jxl_encoder_create(ctx);
  if (!enc) {
    jxl_context_destroy(ctx);
    return 0;
  }

  if (jxl_encoder_use_container(enc, JXL_TRUE) != JXL_OK ||
      (store_jbrd &&
       jxl_encoder_store_jpeg_metadata(enc, JXL_TRUE) != JXL_OK)) {
    jxl_encoder_destroy(enc);
    jxl_context_destroy(ctx);
    return 0;
  }

  jxl_encoder_frame_settings* fs = jxl_encoder_frame_settings_create(enc, NULL);
  if (!fs ||
      jxl_encoder_frame_settings_set_option(fs, JXL_ENCODER_FRAME_SETTING_EFFORT,
                                       effort) != JXL_OK ||
      jxl_encoder_add_jpeg_frame(fs, jpeg, jpeg_size) != JXL_OK) {
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

static int contains_box_type(const uint8_t* data, size_t size,
                             const char box_type[4]) {
  if (size < 8) return 0;
  for (size_t i = 0; i + 8 <= size; ++i) {
    if (memcmp(data + i + 4, box_type, 4) == 0) return 1;
  }
  return 0;
}

static int check_deterministic(const uint8_t* jpeg, size_t jpeg_size,
                               int effort, int store_jbrd) {
  uint8_t* a = NULL;
  uint8_t* b = NULL;
  size_t a_size = 0;
  size_t b_size = 0;

  if (!encode_jpeg(jpeg, jpeg_size, effort, store_jbrd, &a, &a_size) ||
      !encode_jpeg(jpeg, jpeg_size, effort, store_jbrd, &b, &b_size)) {
    fprintf(stderr, "encode failed: effort=%d jbrd=%d\n", effort, store_jbrd);
    free(a);
    free(b);
    return 0;
  }

  int ok = (a_size == b_size) && (memcmp(a, b, a_size) == 0);
  if (ok) {
    printf("OK: deterministic effort=%2d jbrd=%d (%zu bytes)\n", effort,
           store_jbrd, a_size);
    if (store_jbrd && !contains_box_type(a, a_size, "jbrd")) {
      fprintf(stderr, "missing jbrd box: effort=%d\n", effort);
      ok = 0;
    }
  } else {
    fprintf(stderr, "non-deterministic encode: effort=%d jbrd=%d\n", effort,
            store_jbrd);
  }

  free(a);
  free(b);
  return ok;
}

int main(int argc, char** argv) {
  const char* jpeg_path =
      argc > 1 ? argv[1] : JPEG_ROUNDTRIP_JPEG_DEFAULT;

  uint8_t* jpeg = NULL;
  size_t jpeg_size = 0;
  if (!read_file(jpeg_path, &jpeg, &jpeg_size)) {
    fprintf(stderr, "failed to read JPEG: %s\n", jpeg_path);
    return 1;
  }

  int failed = 0;
  const int efforts[] = {1, 7, 10};
  for (size_t e = 0; e < sizeof(efforts) / sizeof(efforts[0]); ++e) {
    for (int jbrd = 0; jbrd <= 1; ++jbrd) {
      if (!check_deterministic(jpeg, jpeg_size, efforts[e], jbrd)) failed = 1;
    }
  }

  free(jpeg);
  return failed ? 1 : 0;
}
