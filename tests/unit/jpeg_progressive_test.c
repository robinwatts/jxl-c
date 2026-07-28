/* Copyright (c) the JPEG XL Project Authors. All rights reserved.
 *
 * Progressive JPEG → JXL: SOF2+refine fixtures must encode deterministically.
 * Bitstream goldens are checked by jpeg_progressive_parity_test.
 */

#include <jxl/context.h>
#include <jxl/encode.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifndef JPEG_PROGRESSIVE_DATA_DIR
#define JPEG_PROGRESSIVE_DATA_DIR "testdata/jpeg"
#endif

static const char* kFixtures[] = {
    "progressive_smoke.jpg",
    "progressive_gray.jpg",
    "progressive_rgb444.jpg",
    "progressive_yuv420.jpg",
};

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

/* Returns 1 if the JPEG has SOF2 and at least one SOS with Ah != 0 (refine). */
static int is_progressive_with_refine(const uint8_t* data, size_t size) {
  int has_sof2 = 0;
  int has_refine = 0;
  size_t i = 0;
  while (i + 1 < size) {
    if (data[i] != 0xff) {
      ++i;
      continue;
    }
    uint8_t marker = data[i + 1];
    if (marker == 0x00 || marker == 0xff) {
      ++i;
      continue;
    }
    if (marker == 0xd8) {
      i += 2;
      continue;
    }
    if (marker == 0xd9) break;
    if (i + 3 >= size) return 0;
    size_t seglen = ((size_t)data[i + 2] << 8) | data[i + 3];
    if (seglen < 2 || i + 2 + seglen > size) return 0;

    if (marker == 0xc2) has_sof2 = 1;
    if (marker == 0xda && seglen >= 8) {
      uint8_t ns = data[i + 4];
      size_t ahal_index = i + 4 + 1 + (size_t)ns * 2 + 2;
      if (ahal_index < i + 2 + seglen) {
        uint8_t ahal = data[ahal_index];
        if ((ahal >> 4) != 0) has_refine = 1;
      }
    }

    if (marker == 0xda) {
      i += 2 + seglen;
      while (i + 1 < size) {
        if (data[i] == 0xff && data[i + 1] != 0x00 && data[i + 1] != 0xff) {
          break;
        }
        ++i;
      }
      continue;
    }
    i += 2 + seglen;
  }
  return has_sof2 && has_refine;
}

static int contains_box_type(const uint8_t* data, size_t size,
                             const char box_type[4]) {
  if (size < 8) return 0;
  for (size_t i = 0; i + 8 <= size; ++i) {
    if (memcmp(data + i + 4, box_type, 4) == 0) return 1;
  }
  return 0;
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

static int check_deterministic(const char* name, const uint8_t* jpeg,
                               size_t jpeg_size, int effort, int store_jbrd) {
  uint8_t* a = NULL;
  uint8_t* b = NULL;
  size_t a_size = 0;
  size_t b_size = 0;

  if (!encode_jpeg(jpeg, jpeg_size, effort, store_jbrd, &a, &a_size) ||
      !encode_jpeg(jpeg, jpeg_size, effort, store_jbrd, &b, &b_size)) {
    fprintf(stderr, "progressive encode failed: %s effort=%d jbrd=%d\n", name,
            effort, store_jbrd);
    free(a);
    free(b);
    return 0;
  }

  int ok = (a_size == b_size) && (memcmp(a, b, a_size) == 0);
  if (ok) {
    printf("OK: %s deterministic effort=%2d jbrd=%d (%zu bytes)\n", name,
           effort, store_jbrd, a_size);
    if (store_jbrd && !contains_box_type(a, a_size, "jbrd")) {
      fprintf(stderr, "progressive: missing jbrd box: %s effort=%d\n", name,
              effort);
      ok = 0;
    }
  } else {
    fprintf(stderr,
            "progressive non-deterministic: %s effort=%d jbrd=%d\n", name,
            effort, store_jbrd);
  }

  free(a);
  free(b);
  return ok;
}

static int stem_from_fixture(const char* filename, char* stem, size_t stem_sz) {
  /* progressive_smoke.jpg -> smoke, progressive_rgb444.jpg -> rgb444 */
  const char* prefix = "progressive_";
  size_t plen = strlen(prefix);
  if (strncmp(filename, prefix, plen) != 0) return 0;
  const char* base = filename + plen;
  const char* dot = strrchr(base, '.');
  if (!dot || (size_t)(dot - base) + 1 > stem_sz) return 0;
  memcpy(stem, base, (size_t)(dot - base));
  stem[dot - base] = '\0';
  return 1;
}

int main(int argc, char** argv) {
  const char* data_dir =
      argc > 1 ? argv[1] : JPEG_PROGRESSIVE_DATA_DIR;

  int failed = 0;
  const int efforts[] = {1, 7, 10};

  for (size_t fi = 0; fi < sizeof(kFixtures) / sizeof(kFixtures[0]); ++fi) {
    char path[512];
    snprintf(path, sizeof(path), "%s/%s", data_dir, kFixtures[fi]);

    uint8_t* jpeg = NULL;
    size_t jpeg_size = 0;
    if (!read_file(path, &jpeg, &jpeg_size)) {
      fprintf(stderr, "failed to read JPEG: %s\n", path);
      failed = 1;
      continue;
    }

    char stem[64];
    if (!stem_from_fixture(kFixtures[fi], stem, sizeof(stem))) {
      fprintf(stderr, "bad fixture name: %s\n", kFixtures[fi]);
      free(jpeg);
      failed = 1;
      continue;
    }

    if (!is_progressive_with_refine(jpeg, jpeg_size)) {
      fprintf(stderr,
              "not progressive with refine (need SOF2 + Ah!=0): %s\n", path);
      free(jpeg);
      failed = 1;
      continue;
    }
    printf("OK: %s is progressive with refine scan(s)\n", kFixtures[fi]);

    for (size_t e = 0; e < sizeof(efforts) / sizeof(efforts[0]); ++e) {
      for (int jbrd = 0; jbrd <= 1; ++jbrd) {
        if (!check_deterministic(stem, jpeg, jpeg_size, efforts[e], jbrd)) {
          failed = 1;
        }
      }
    }
    free(jpeg);
  }

  return failed ? 1 : 0;
}
