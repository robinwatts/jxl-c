// SPDX-License-Identifier: MIT OR Apache-2.0

#include "encoder/jpeg_context.h"

#include <jxl/cms.h>

#include <string.h>

#include "allocator.h"
#include "context.h"
#include "lib/jxl/base/status.h"

static void* jpeg_ctx_mm_alloc(void* opaque, size_t size) {
  return jxl_alloc((jxl_allocator_state*)opaque, size);
}

static void jpeg_ctx_mm_free(void* opaque, void* address) {
  jxl_free((jxl_allocator_state*)opaque, address);
}

static void jpeg_ctx_init_mm(jxl_jpeg_encoder_context* je, jxl_allocator_state* alloc) {
  je->memory_manager.opaque = alloc;
  je->memory_manager.alloc = jpeg_ctx_mm_alloc;
  je->memory_manager.free = jpeg_ctx_mm_free;
}

int jxl_jpeg_encoder_context_init(jxl_context* ctx) {
  jxl_jpeg_encoder_context* je;
  if (ctx == NULL) return 0;
  je = (jxl_jpeg_encoder_context*)jxl_alloc(&ctx->alloc, sizeof(*je));
  if (je == NULL) return 0;
  memset(je, 0, sizeof(*je));
  jpeg_ctx_init_mm(je, &ctx->alloc);
  jxl_enc_color_encoding_construct_empty(&je->srgb[0], &je->memory_manager);
  jxl_enc_color_encoding_construct_empty(&je->srgb[1], &je->memory_manager);
  je->lcms = jxl_cms_create_lcms_context(&je->memory_manager);
  if (je->lcms == NULL) {
    jxl_enc_color_encoding_destroy(&je->srgb[0]);
    jxl_enc_color_encoding_destroy(&je->srgb[1]);
    jxl_free(&ctx->alloc, je);
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
  jxl_free(&ctx->alloc, je);
  ctx->jpeg_enc = NULL;
}

jxl_memory_manager* jxl_context_memory_manager(jxl_context* ctx) {
  if (ctx == NULL || ctx->jpeg_enc == NULL) return NULL;
  return &ctx->jpeg_enc->memory_manager;
}

void* jxl_context_lcms(jxl_context* ctx) {
  if (ctx == NULL || ctx->jpeg_enc == NULL) return NULL;
  return ctx->jpeg_enc->lcms;
}

const jxl_enc_color_encoding* jxl_context_srgb(jxl_context* ctx, int is_gray) {
  jxl_jpeg_encoder_context* je;
  if (ctx == NULL || ctx->jpeg_enc == NULL) return NULL;
  je = ctx->jpeg_enc;
  if (!je->srgb_ready) {
    jxl_enc_color_encoding_create_c2(kPrimariesSRGB, kTFSRGB, &je->memory_manager,
                                     je->srgb);
    je->srgb_ready = 1;
  }
  return &je->srgb[is_gray ? 1 : 0];
}
