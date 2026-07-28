// SPDX-License-Identifier: MIT OR Apache-2.0

#include "encoder/jpeg_context.h"

#include <jxl/cms.h>

#include <string.h>

#include "allocator.h"
#include "context.h"
#include "lib/jxl/base/status.h"
#include "lib/jxl/memory_manager.h"

static void* jxl_ctx_mm_alloc(void* opaque, size_t size) {
  return jxl_alloc((jxl_context *)opaque, size);
}

static void jxl_ctx_mm_free(void* opaque, void* address) {
  jxl_free((jxl_context *)opaque, address);
}

void jxl_context_bind_memory_manager(jxl_context* ctx) {
  if (ctx == NULL) return;
  ctx->mm.opaque = ctx;
  ctx->mm.alloc = jxl_ctx_mm_alloc;
  ctx->mm.free = jxl_ctx_mm_free;
}

jxl_memory_manager* jxl_context_memory_manager(jxl_context* ctx) {
  if (ctx == NULL) return NULL;
  return &ctx->mm;
}

int jxl_jpeg_encoder_context_init(jxl_context* ctx) {
  jxl_jpeg_encoder_context* je;
  jxl_memory_manager* mm;
  if (ctx == NULL) return 0;
  jxl_context_bind_memory_manager(ctx);
  mm = &ctx->mm;
  je = (jxl_jpeg_encoder_context*)jxl_alloc(ctx, sizeof(*je));
  if (je == NULL) return 0;
  memset(je, 0, sizeof(*je));
  jxl_enc_color_encoding_construct_empty(&je->srgb[0], mm);
  jxl_enc_color_encoding_construct_empty(&je->srgb[1], mm);
  je->lcms = jxl_cms_create_lcms_context(ctx);
  if (je->lcms == NULL) {
    jxl_enc_color_encoding_destroy(&je->srgb[0]);
    jxl_enc_color_encoding_destroy(&je->srgb[1]);
    jxl_free(ctx, je);
    return 0;
  }
  ctx->jpeg_enc = je;
  return 1;
}

void jxl_jpeg_encoder_context_fini(jxl_context* ctx) {
  jxl_jpeg_encoder_context* je;
  if (ctx == NULL || ctx->jpeg_enc == NULL) return;
  je = ctx->jpeg_enc;
  if (je->srgb_ready) {
    jxl_enc_color_encoding_destroy(&je->srgb[0]);
    jxl_enc_color_encoding_destroy(&je->srgb[1]);
    je->srgb_ready = 0;
  }
  jxl_cms_destroy_lcms_context(je->lcms);
  je->lcms = NULL;
  jxl_free(ctx, je);
  ctx->jpeg_enc = NULL;
}

void* jxl_context_lcms(jxl_context* ctx) {
  if (ctx == NULL || ctx->jpeg_enc == NULL) return NULL;
  return ctx->jpeg_enc->lcms;
}

const jxl_enc_color_encoding* jxl_context_srgb(jxl_context* ctx, int is_gray) {
  jxl_jpeg_encoder_context* je;
  jxl_memory_manager* mm;
  if (ctx == NULL || ctx->jpeg_enc == NULL) return NULL;
  je = ctx->jpeg_enc;
  mm = jxl_context_memory_manager(ctx);
  if (!je->srgb_ready) {
    jxl_enc_color_encoding_create_c2(kPrimariesSRGB, kTFSRGB, mm, je->srgb);
    je->srgb_ready = 1;
  }
  return &je->srgb[is_gray ? 1 : 0];
}
