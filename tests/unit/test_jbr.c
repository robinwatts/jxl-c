// SPDX-License-Identifier: MIT OR Apache-2.0
#include "jxl_oxide/jxl_oxide.h"

#include "allocator.h"
#include "context.h"
#include "sha256.h"
#include "test_helpers.h"
#include "test_paths.h"

#include <stdio.h>
#include <assert.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
    const char *name;
    int use_decode_fixtures;
    const char *expected_sha256;
} jbr_case;

static const jbr_case k_jbr_cases[] = {
    {"grayscale_jpeg", 0,
     "a170600cc02b2b029dc79c5ee72dbf9107e8de5a64f1d8b1732c759e09a3d41d"},
    {"cafe", 0, "b6f6e4f820ac69234184434e5b77156401fb782bb46b96e26255ff51be1ec290"},
    {"bench_oriented_brg", 0,
     "cad665c67d74e3e5cf775ef618c73b0e70dfece33db7dbe0130bc889f2214e1b"},
    {"genshin_ycbcr_420", 1,
     "24e05ce200df019710eb56cad43e4349bd6edee05865b1eb0eee1581406535b5"},
    {"issue_425", 1, "c00df4d5b151556cfe92ac4b13d81e2a1ee20fa066cd5fddd3cdd1ae52f0fe00"},
    {"starrail_jpegli_xyb", 1,
     "21ddc2688c89ec279a2e9415bc2e82c4d4b56362316d1332131b6fb97b0d1ea0"},
};

static void jbr_fixture_path(const jbr_case *tc, char *buf, size_t len) {
    const char *base =
        tc->use_decode_fixtures ? JXL_OXIDE_FIXTURES_DIR : JXL_OXIDE_CONFORMANCE_DIR;
    snprintf(buf, len, "%s/%s/input.jxl", base, tc->name);
}

static int read_file(const char *path, uint8_t **out, size_t *out_len) {
    long sz;
    FILE *f = fopen(path, "rb");
    uint8_t *buf;
    if (f == NULL) {
        return 0;
    }
    if (fseek(f, 0, SEEK_END) != 0) {
        fclose(f);
        return 0;
    }
    sz = ftell(f);
    if (sz < 0) {
        fclose(f);
        return 0;
    }
    if (fseek(f, 0, SEEK_SET) != 0) {
        fclose(f);
        return 0;
    }
    buf = malloc((size_t)sz);
    if (buf == NULL) {
        fclose(f);
        return 0;
    }
    if (fread(buf, 1, (size_t)sz, f) != (size_t)sz) {
        free(buf);
        fclose(f);
        return 0;
    }
    fclose(f);
    *out = buf;
    *out_len = (size_t)sz;
    return 1;
}

static void test_jbr_reconstruction(const jbr_case *tc) {
    size_t len;
    size_t jpeg_len;
    char digest[65];
    char path[512];
    jxl_jpeg_reconstruction_status jbr_st;
    uint8_t *data = NULL;
    jxl_context *ctx = NULL;
    jxl_decoder *dec = NULL;
    uint8_t *jpeg = NULL;

    jbr_fixture_path(tc, path, sizeof(path));
    len = 0;
    JXL_TEST_REQUIRE(read_file(path, &data, &len));

    JXL_TEST_REQUIRE(jxl_context_create(NULL, &ctx) == JXL_OK);
    JXL_TEST_REQUIRE(jxl_decoder_create(ctx, NULL, &dec) == JXL_OK);
    JXL_TEST_REQUIRE(jxl_decoder_feed(dec, data, len) == JXL_OK);
    JXL_TEST_REQUIRE(jxl_decoder_try_init(dec) == JXL_OK);

    jbr_st = jxl_decoder_jpeg_reconstruction_status(dec);
    if (jbr_st != JXL_JPEG_RECONSTRUCTION_AVAILABLE) {
        fprintf(stderr, "%s: expected JPEG reconstruction available, got %d\n", tc->name,
                (int)jbr_st);
        assert(0);
    }

    JXL_TEST_REQUIRE(jxl_decoder_reconstruct_jpeg(dec, &jpeg, &jpeg_len) == JXL_OK);
    jxl_test_sha256_hex(jpeg, jpeg_len, digest);
    if (strcmp(digest, tc->expected_sha256) != 0) {
        fprintf(stderr, "%s: digest mismatch\n  got %s\n  exp %s\n", tc->name, digest,
                tc->expected_sha256);
        assert(0);
    }

    jxl_free(jxl_context_alloc_state(ctx), jpeg);
    jxl_decoder_destroy(ctx, dec);
    jxl_context_destroy(ctx);
    free(data);
}

int main(void) {
    size_t i;
    for (i = 0; i < sizeof(k_jbr_cases) / sizeof(k_jbr_cases[0]); ++i) {
        test_jbr_reconstruction(&k_jbr_cases[i]);
    }
    printf("test_jbr: ok\n");
    return 0;
}
