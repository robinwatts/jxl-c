// SPDX-License-Identifier: MIT OR Apache-2.0
#include "test_paths.h"

/*
 * Crop vs full-render equivalence on conformance fixtures (Rust tests/crop/mod.rs).
 *
 * Compares every keyframe on animated fixtures; still images use keyframe 0 only.
 * Fixed crops mirror testcase_with_crop!; random crops mirror testcase! (4 regions).
 * crop_upsampling_0 is omitted — Rust marks it #[ignore] on emulated aarch64 CI.
 */
#include "jxl_oxide/jxl_oxide.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>


#define JXL_CROP(W, H, L, T)                                                                 \
    {                                                                                          \
        (W), (H), (L), (T)                                                                     \
    }

typedef struct {
    const char *label;
    const char *fixture;
    jxl_crop crop;
} fixed_crop_case;

static const fixed_crop_case k_fixed_cases[] = {
    {"crop_progressive_0", "progressive", JXL_CROP(315, 571, 1711, 800)},
    {"crop_progressive_1", "progressive", JXL_CROP(1159, 359, 776, 1745)},
    {"crop_noise_0", "noise", JXL_CROP(195, 162, 169, 194)},
    {"crop_blendmodes_0", "blendmodes", JXL_CROP(242, 163, 81, 302)},
    {"crop_alpha_triangles_triple_0", "alpha_triangles", JXL_CROP(460, 325, 468, 356)},
    {"crop_alpha_triangles_triple_1", "alpha_triangles", JXL_CROP(361, 147, 524, 475)},
    {"crop_progressive_triple_0", "progressive", JXL_CROP(941, 659, 1893, 35)},
    {"crop_progressive_triple_1", "progressive", JXL_CROP(1847, 1220, 850, 929)},
    {"crop_progressive_triple_2", "progressive", JXL_CROP(1421, 814, 1568, 1460)},
    {"crop_bike_0", "bike", JXL_CROP(936, 137, 877, 2353)},
    {"crop_sunset_logo_0", "sunset_logo", JXL_CROP(179, 258, 527, 298)},
};

static const char *const k_random_fixtures[] = {
    "bicycles",       "bike",           "alpha_triangles", "lz77_flower",     "sunset_logo",
    "blendmodes",     "progressive",    "animation_icos4d", "animation_spline", "animation_newtons_cradle",
    "lossless_pfm",   "noise",          "cafe",            "upsampling",      "delta_palette",
    "patches_lossless", "grayscale",    "grayscale_jpeg",  "grayscale_public_university", "spot",
};

typedef struct {
    uint64_t state;
} pcg32_t;

static void pcg32_init(pcg32_t *rng, uint64_t seed) {
    rng->state = seed;
}

static uint32_t pcg32_next(pcg32_t *rng) {
    uint64_t old = rng->state;
    rng->state = old * 6364136223846793005ULL + 1ULL;
    uint32_t xorshifted = (uint32_t)(((old >> 18u) ^ old) >> 27u);
    uint32_t rot = (uint32_t)(old >> 59u);
    return (xorshifted >> rot) | (xorshifted << ((~rot + 1u) & 31u));
}

static uint32_t rand_range_inclusive(pcg32_t *rng, uint32_t lo, uint32_t hi) {
    if (hi <= lo) {
        return lo;
    }
    return lo + (pcg32_next(rng) % (hi - lo + 1u));
}

static uint64_t hash_fixture_seed(const char *name) {
    uint64_t h = 0xcbf29ce484222325ULL;
    for (const char *p = name; *p != '\0'; ++p) {
        h ^= (unsigned char)*p;
        h *= 0x100000001b3ULL;
    }
    return h;
}

static int read_file(const char *path, uint8_t **out_data, size_t *out_len) {
    FILE *f = fopen(path, "rb");
    if (f == NULL) {
        fprintf(stderr, "failed to open %s\n", path);
        return -1;
    }
    if (fseek(f, 0, SEEK_END) != 0) {
        fclose(f);
        return -1;
    }
    long size = ftell(f);
    if (size < 0) {
        fclose(f);
        return -1;
    }
    if (fseek(f, 0, SEEK_SET) != 0) {
        fclose(f);
        return -1;
    }
    uint8_t *data = malloc((size_t)size);
    if (data == NULL) {
        fclose(f);
        return -1;
    }
    if (fread(data, 1, (size_t)size, f) != (size_t)size) {
        free(data);
        fclose(f);
        return -1;
    }
    fclose(f);
    *out_data = data;
    *out_len = (size_t)size;
    return 0;
}

static jxl_status_t open_decoder(jxl_context *ctx, const uint8_t *data, size_t len,
                                 const jxl_crop *crop, jxl_decoder **out_dec) {
    jxl_decoder *dec = NULL;
    jxl_status_t status = jxl_decoder_create(ctx, NULL, &dec);
    if (status != JXL_OK) {
        return status;
    }
    status = jxl_decoder_feed(dec, data, len);
    if (status != JXL_OK) {
        jxl_decoder_destroy(ctx, dec);
        return status;
    }
    status = jxl_decoder_try_init(dec);
    if (status != JXL_OK) {
        jxl_decoder_destroy(ctx, dec);
        return status;
    }
    if (crop != NULL) {
        status = jxl_decoder_set_crop(dec, crop);
        if (status != JXL_OK) {
            jxl_decoder_destroy(ctx, dec);
            return status;
        }
    }
    *out_dec = dec;
    return JXL_OK;
}

static jxl_status_t get_image_dimensions(jxl_context *ctx, const uint8_t *data, size_t len,
                                         uint32_t *out_w, uint32_t *out_h) {
    jxl_decoder *dec = NULL;
    jxl_status_t status = jxl_decoder_create(ctx, NULL, &dec);
    if (status != JXL_OK) {
        return status;
    }
    status = jxl_decoder_feed(dec, data, len);
    if (status != JXL_OK) {
        jxl_decoder_destroy(ctx, dec);
        return status;
    }
    status = jxl_decoder_try_init(dec);
    if (status != JXL_OK) {
        jxl_decoder_destroy(ctx, dec);
        return status;
    }
    const jxl_image_header *hdr = jxl_decoder_header(dec);
    if (hdr == NULL) {
        jxl_decoder_destroy(ctx, dec);
        return JXL_ERROR_INVALID_INPUT;
    }
    *out_w = hdr->width;
    *out_h = hdr->height;
    jxl_decoder_destroy(ctx, dec);
    return JXL_OK;
}

static int compare_crop_to_full(const jxl_render *full, const jxl_render *cropped,
                                const jxl_crop *crop, const char *name) {
                                    uint32_t p;
    uint32_t fw = jxl_render_width(full);
    uint32_t fh = jxl_render_height(full);
    uint32_t cw = jxl_render_width(cropped);
    uint32_t ch = jxl_render_height(cropped);
    uint32_t planes = jxl_render_num_planes(full);

    float max_diff;
    uint32_t max_p;
    uint32_t max_x;
    uint32_t max_y;
    float max_expected;
    float max_actual;
    if (cw != crop->width || ch != crop->height) {
        fprintf(stderr, "%s: cropped size %ux%u != expected %ux%u\n", name, cw, ch, crop->width,
                crop->height);
        return -1;
    }
    if (crop->left + crop->width > fw || crop->top + crop->height > fh) {
        fprintf(stderr, "%s: crop out of full image bounds\n", name);
        return -1;
    }
    if (jxl_render_num_planes(cropped) != planes) {
        fprintf(stderr, "%s: plane count mismatch\n", name);
        return -1;
    }

    /* EPF padding at crop edges can differ slightly vs full decode (Rust uses 1e-6). */
    const float tol = 1e-4f;
    max_diff = 0.0f;
    max_p = 0;
    max_x = 0;
    max_y = 0;
    max_expected = 0.0f;
    max_actual = 0.0f;
    for (p = 0; p < planes; ++p) {
        uint32_t y;
        const float *full_plane = jxl_render_plane(full, p);
        const float *crop_plane = jxl_render_plane(cropped, p);
        if (full_plane == NULL || crop_plane == NULL) {
            return -1;
        }
        for (y = 0; y < ch; ++y) {
            uint32_t x;
            for (x = 0; x < cw; ++x) {
                size_t full_idx = (size_t)(crop->top + y) * fw + (size_t)(crop->left + x);
                size_t crop_idx = (size_t)y * cw + (size_t)x;
                float expected = full_plane[full_idx];
                float actual = crop_plane[crop_idx];
                float diff = fabsf(actual - expected);
                if (diff > max_diff) {
                    max_diff = diff;
                    max_p = p;
                    max_x = x;
                    max_y = y;
                    max_expected = expected;
                    max_actual = actual;
                }
            }
        }
    }
    if (max_diff > tol) {
        fprintf(stderr,
                "%s: max diff %g exceeds tol %g at plane=%u x=%u y=%u expected=%g actual=%g\n",
                name, max_diff, tol, max_p, max_x, max_y, max_expected, max_actual);
        return 1;
    }
    return 0;
}

static int run_crop_compare(jxl_context *ctx, const uint8_t *data, size_t len,
                            const jxl_crop *crop, const char *label) {
                                uint32_t kf;
    jxl_decoder *dec_full = NULL;
    jxl_decoder *dec_crop = NULL;
    jxl_status_t st = open_decoder(ctx, data, len, NULL, &dec_full);
    if (st != JXL_OK) {
        fprintf(stderr, "%s: full decoder open failed: %s\n", label, jxl_status_string(st));
        return 1;
    }
    st = open_decoder(ctx, data, len, crop, &dec_crop);
    if (st != JXL_OK) {
        fprintf(stderr, "%s: cropped decoder open failed: %s\n", label, jxl_status_string(st));
        jxl_decoder_destroy(ctx, dec_full);
        return 1;
    }

    uint32_t num_keyframes = jxl_decoder_num_keyframes(dec_full);
    if (num_keyframes == 0) {
        fprintf(stderr, "%s: no keyframes\n", label);
        jxl_decoder_destroy(ctx, dec_full);
        jxl_decoder_destroy(ctx, dec_crop);
        return 1;
    }

    for (kf = 0; kf < num_keyframes; ++kf) {
        char kf_label[160];
        jxl_render *full = NULL;
        jxl_render *cropped = NULL;
        if (num_keyframes > 1) {
            snprintf(kf_label, sizeof(kf_label), "%s kf%u", label, kf);
        } else {
            snprintf(kf_label, sizeof(kf_label), "%s", label);
        }

        st = jxl_decoder_render_keyframe(ctx, dec_full, kf, &full);
        if (st != JXL_OK) {
            fprintf(stderr, "%s: full decode failed: %s\n", kf_label, jxl_status_string(st));
            jxl_decoder_destroy(ctx, dec_full);
            jxl_decoder_destroy(ctx, dec_crop);
            return 1;
        }
        st = jxl_decoder_render_keyframe(ctx, dec_crop, kf, &cropped);
        if (st != JXL_OK) {
            fprintf(stderr, "%s: cropped decode failed: %s\n", kf_label, jxl_status_string(st));
            jxl_render_destroy(ctx, full);
            jxl_decoder_destroy(ctx, dec_full);
            jxl_decoder_destroy(ctx, dec_crop);
            return 1;
        }

        int cmp = compare_crop_to_full(full, cropped, crop, kf_label);
        jxl_render_destroy(ctx, full);
        jxl_render_destroy(ctx, cropped);
        if (cmp != 0) {
            jxl_decoder_destroy(ctx, dec_full);
            jxl_decoder_destroy(ctx, dec_crop);
            return 1;
        }
    }

    jxl_decoder_destroy(ctx, dec_full);
    jxl_decoder_destroy(ctx, dec_crop);
    return 0;
}

static const fixed_crop_case *find_fixed_case(const char *label) {
    size_t i;
    for (i = 0; i < sizeof(k_fixed_cases) / sizeof(k_fixed_cases[0]); ++i) {
        if (strcmp(k_fixed_cases[i].label, label) == 0) {
            return &k_fixed_cases[i];
        }
    }
    return NULL;
}

static int run_fixed_case(jxl_context *ctx, const fixed_crop_case *tc) {
    char path[1024];
    int n = snprintf(path, sizeof(path), "%s/%s/input.jxl", JXL_OXIDE_CONFORMANCE_DIR, tc->fixture);
    size_t len;
    if (n < 0 || (size_t)n >= sizeof(path)) {
        return -1;
    }

    uint8_t *data = NULL;
    len = 0;
    if (read_file(path, &data, &len) != 0) {
        return -1;
    }

    int rc = run_crop_compare(ctx, data, len, &tc->crop, tc->label);
    free(data);
    if (rc == 0) {
        printf("%s: ok\n", tc->label);
    }
    return rc;
}

static int run_random_fixture(jxl_context *ctx, const char *fixture, int only_index) {
    int i;
    char path[1024];
    int n = snprintf(path, sizeof(path), "%s/%s/input.jxl", JXL_OXIDE_CONFORMANCE_DIR, fixture);
    size_t len;
    uint32_t width;
    uint32_t height;
    uint32_t max_crop_w;
    uint32_t max_crop_h;
    if (n < 0 || (size_t)n >= sizeof(path)) {
        return -1;
    }

    uint8_t *data = NULL;
    len = 0;
    if (read_file(path, &data, &len) != 0) {
        return -1;
    }

    width = 0;
    height = 0;
    jxl_status_t st = get_image_dimensions(ctx, data, len, &width, &height);
    if (st != JXL_OK) {
        fprintf(stderr, "%s: dimension probe failed: %s\n", fixture, jxl_status_string(st));
        free(data);
        return 1;
    }

    pcg32_t rng;
    pcg32_init(&rng, hash_fixture_seed(fixture));

    max_crop_w = width / 2u;
    if (max_crop_w < 128u) {
        max_crop_w = 128u;
    }
    max_crop_h = height / 2u;
    if (max_crop_h < 128u) {
        max_crop_h = 128u;
    }

    for (i = 0; i < 4; ++i) {
        uint32_t crop_w;
        uint32_t crop_h;
        uint32_t crop_left;
        uint32_t crop_top;
        jxl_crop crop;
        char label[128];

        crop_w = rand_range_inclusive(&rng, 128u, max_crop_w);
        crop_h = rand_range_inclusive(&rng, 128u, max_crop_h);
        if (crop_w > width || crop_h > height) {
            fprintf(stderr, "%s: random crop %d size %ux%u exceeds image %ux%u\n", fixture, i,
                    crop_w, crop_h, width, height);
            free(data);
            return 1;
        }
        crop_left = rand_range_inclusive(&rng, 0u, width - crop_w);
        crop_top = rand_range_inclusive(&rng, 0u, height - crop_h);
        crop.width = crop_w;
        crop.height = crop_h;
        crop.left = crop_left;
        crop.top = crop_top;
        if (only_index >= 0 && i != only_index) {
            continue;
        }

        snprintf(label, sizeof(label), "%s_random_%d", fixture, i);
        if (run_crop_compare(ctx, data, len, &crop, label) != 0) {
            free(data);
            return 1;
        }
    }

    free(data);
    printf("%s: random crops ok\n", fixture);
    return 0;
}

int main(int argc, char **argv) {
    int failed;
    jxl_context *ctx = NULL;
    if (jxl_context_create(NULL, &ctx) != JXL_OK) {
        fprintf(stderr, "jxl_context_create failed\n");
        return 1;
    }

    if (argc > 1 && strcmp(argv[1], "sunset") == 0) {
        static const fixed_crop_case k_case = {
            "crop_sunset_logo_0", "sunset_logo", JXL_CROP(179, 258, 527, 298),
        };
        int rc = run_fixed_case(ctx, &k_case) != 0 ? 1 : 0;
        jxl_context_destroy(ctx);
        return rc;
    }
    if (argc > 1 && strcmp(argv[1], "case") == 0 && argc > 2) {
        const fixed_crop_case *tc = find_fixed_case(argv[2]);
        if (tc == NULL) {
            fprintf(stderr, "unknown crop case %s\n", argv[2]);
            jxl_context_destroy(ctx);
            return 1;
        }
        int rc = run_fixed_case(ctx, tc) != 0 ? 1 : 0;
        jxl_context_destroy(ctx);
        return rc;
    }
    if (argc > 1 && strcmp(argv[1], "fixture") == 0 && argc > 2) {
        int only_index = -1;
        if (argc > 3) {
            only_index = atoi(argv[3]);
        }
        int rc = run_random_fixture(ctx, argv[2], only_index) != 0 ? 1 : 0;
        jxl_context_destroy(ctx);
        return rc;
    }

    int fixed_only = argc > 1 && strcmp(argv[1], "fixed") == 0;
    int random_only = argc > 1 && strcmp(argv[1], "random") == 0;

    failed = 0;
    if (!random_only) {
        size_t i;
        for (i = 0; i < sizeof(k_fixed_cases) / sizeof(k_fixed_cases[0]); ++i) {
            if (run_fixed_case(ctx, &k_fixed_cases[i]) != 0) {
                failed = 1;
            }
        }
    }

    if (!fixed_only) {
        size_t i;
        for (i = 0; i < sizeof(k_random_fixtures) / sizeof(k_random_fixtures[0]); ++i) {
            if (run_random_fixture(ctx, k_random_fixtures[i], -1) != 0) {
                failed = 1;
            }
        }
    }

    jxl_context_destroy(ctx);

    if (failed) {
        return 1;
    }
    printf("test_crop_conformance: ok\n");
    return 0;
}
