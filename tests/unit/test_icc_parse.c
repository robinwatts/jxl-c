// SPDX-License-Identifier: MIT OR Apache-2.0
#include "test_paths.h"

#include "image/icc_parse.h"
#include "jxl_oxide/jxl_status.h"
#include "jxl_oxide/jxl_types.h"

#include <stdio.h>
#include <stdlib.h>


int main(void) {
    char path[512];
    jxl_color_encoding enc;
    const char *hash = getenv("JXL_ICC_HASH");
    if (hash == NULL || hash[0] == '\0') {
        hash = "80a1d9ea2892c89ab10a05fcbd1d752069557768fac3159ecd91c33be0d74a19";
    }
    snprintf(path, sizeof(path), "%s/%s.icc", JXL_OXIDE_CACHE_DIR, hash);
    FILE *f = fopen(path, "rb");
    if (f == NULL) {
        return 1;
    }
    fseek(f, 0, SEEK_END);
    long sz = ftell(f);
    fseek(f, 0, SEEK_SET);
    uint8_t *icc = malloc((size_t)sz);
    if (icc == NULL) {
        fclose(f);
        return 1;
    }
    fread(icc, 1, (size_t)sz, f);
    fclose(f);

    if (!jxl_icc_maps_to_srgb_display(icc, (size_t)sz)) {
        fprintf(stderr, "expected reference ICC to map to sRGB display path\n");
        free(icc);
        return 1;
    }

    if (jxl_icc_parse_color_encoding(icc, (size_t)sz, &enc) != JXL_OK) {
        fprintf(stderr, "expected jxl_icc_parse_color_encoding to succeed\n");
        free(icc);
        return 1;
    }
    if (enc.colour_space != JXL_COLOUR_SPACE_RGB || enc.transfer != JXL_TRANSFER_SRGB ||
        enc.white_point != JXL_WHITE_POINT_D65 || enc.primaries != JXL_PRIMARIES_SRGB) {
        fprintf(stderr, "unexpected parsed ICC colour encoding\n");
        free(icc);
        return 1;
    }

    free(icc);
    return 0;
}
