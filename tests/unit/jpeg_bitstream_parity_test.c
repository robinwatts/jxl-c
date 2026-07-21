/* Copyright (c) the JPEG XL Project Authors. All rights reserved.
 *
 * Byte-for-byte parity test: minimal encoder output must match golden files
 * captured from the scalar minimal encoder for JPEG lossless recompression.
 */

#include <jxl_oxide/jxl_context.h>
#include <jxl/encode.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifndef JPEG_PARITY_JPEG_DEFAULT
#define JPEG_PARITY_JPEG_DEFAULT "testdata/jpeg/smoke.jpg"
#endif

#ifndef JPEG_PARITY_GOLDEN_DIR
#define JPEG_PARITY_GOLDEN_DIR "testdata/jxl"
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

static int encode_jpeg_at_effort(const uint8_t* jpeg, size_t jpeg_size,
                                 int effort, int store_jbrd,
                                 uint8_t** out, size_t* out_size) {
  jxl_context* ctx = NULL;
  if (jxl_context_create(NULL, &ctx) != JXL_OK) return 0;
  jxl_encoder* enc = jxl_encoder_create(ctx);
  if (!enc) {
    jxl_context_destroy(ctx);
    return 0;
  }

  if (jxl_encoder_use_container(enc, JXL_TRUE) != JXL_ENCODER_SUCCESS) {
    jxl_encoder_destroy(enc);
    jxl_context_destroy(ctx);
    return 0;
  }
  if (store_jbrd &&
      jxl_encoder_store_jpeg_metadata(enc, JXL_TRUE) != JXL_ENCODER_SUCCESS) {
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
  if (jxl_encoder_frame_settings_set_option(fs, JXL_ENCODER_FRAME_SETTING_EFFORT,
                                       effort) != JXL_ENCODER_SUCCESS) {
    jxl_encoder_destroy(enc);
    jxl_context_destroy(ctx);
    return 0;
  }
  if (jxl_encoder_add_jpeg_frame(fs, jpeg, jpeg_size) != JXL_ENCODER_SUCCESS) {
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

  jxl_encoder_status st;
  while ((st = jxl_encoder_process_output(enc, &next, &avail)) ==
         JXL_ENCODER_NEED_MORE_OUTPUT) {
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

  if (st != JXL_ENCODER_SUCCESS) {
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

static int check_case(const uint8_t* jpeg, size_t jpeg_size, int effort,
                      int store_jbrd, const char* golden_dir) {
  char golden_path[512];
  if (store_jbrd) {
    snprintf(golden_path, sizeof(golden_path), "%s/smoke_e%02d_jbrd.jxl",
             golden_dir, effort);
  } else {
    snprintf(golden_path, sizeof(golden_path), "%s/smoke_e%02d.jxl", golden_dir,
             effort);
  }

  uint8_t* golden = NULL;
  size_t golden_size = 0;
  if (!read_file(golden_path, &golden, &golden_size)) {
    fprintf(stderr, "failed to read golden: %s\n", golden_path);
    return 0;
  }

  uint8_t* out = NULL;
  size_t out_size = 0;
  if (!encode_jpeg_at_effort(jpeg, jpeg_size, effort, store_jbrd, &out,
                             &out_size)) {
    fprintf(stderr, "encode failed: effort=%d jbrd=%d\n", effort, store_jbrd);
    free(golden);
    return 0;
  }

  int ok = (out_size == golden_size) && (memcmp(out, golden, out_size) == 0);
  if (!ok) {
    fprintf(stderr,
            "bitstream mismatch: effort=%d jbrd=%d golden=%s sizes %zu vs %zu\n",
            effort, store_jbrd, golden_path, golden_size, out_size);
    size_t n = golden_size < out_size ? golden_size : out_size;
    for (size_t i = 0; i < n; ++i) {
      if (out[i] != golden[i]) {
        fprintf(stderr, "  first diff at byte %zu: golden=0x%02x got=0x%02x\n",
                i, golden[i], out[i]);
        break;
      }
    }
    if (out_size != golden_size) {
      fprintf(stderr, "  size differs (truncated compare above)\n");
    }
  } else {
    printf("OK: effort=%2d jbrd=%d (%zu bytes)\n", effort, store_jbrd,
           out_size);
  }

  free(out);
  free(golden);
  return ok;
}

int main(int argc, char** argv) {
  const char* jpeg_path =
      argc > 1 ? argv[1] : JPEG_PARITY_JPEG_DEFAULT;
  const char* golden_dir =
      argc > 2 ? argv[2] : JPEG_PARITY_GOLDEN_DIR;

  uint8_t* jpeg = NULL;
  size_t jpeg_size = 0;
  if (!read_file(jpeg_path, &jpeg, &jpeg_size)) {
    fprintf(stderr, "failed to read JPEG: %s\n", jpeg_path);
    return 1;
  }

  int failed = 0;
  for (int effort = 1; effort <= 10; ++effort) {
    for (int jbrd = 0; jbrd <= 1; ++jbrd) {
      if (!check_case(jpeg, jpeg_size, effort, jbrd, golden_dir)) {
        failed = 1;
      }
    }
  }

  free(jpeg);
  return failed ? 1 : 0;
}
