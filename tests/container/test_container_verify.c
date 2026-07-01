// SPDX-License-Identifier: MIT OR Apache-2.0
#include "test_paths.h"

#include "collect_codestream.h"
#include "sha256.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>


#include "codestream_digests.inc"

static int read_file(const char *path, uint8_t **out_data, size_t *out_len) {
    FILE *f = fopen(path, "rb");
    if (f == NULL) {
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

int main(void) {
    size_t i;
    int failed = 0;
    for (i = 0; i < k_digest_count; ++i) {
        char path[1024];
        size_t file_len;
        size_t cs_len;
        char digest[65];
        int n = snprintf(path, sizeof(path), "%s/%s/input.jxl", JXL_OXIDE_FIXTURES_DIR,
                         k_digests[i].name);
        if (n < 0 || (size_t)n >= sizeof(path)) {
            fprintf(stderr, "path too long for %s\n", k_digests[i].name);
            failed = 1;
            continue;
        }

        uint8_t *file_data = NULL;
        file_len = 0;
        if (read_file(path, &file_data, &file_len) != 0) {
            fprintf(stderr, "missing %s\n", path);
            failed = 1;
            continue;
        }

        uint8_t *codestream = NULL;
        cs_len = 0;
        jxl_bs_status_t st = jxl_test_extract_codestream(file_data, file_len, &codestream, &cs_len);
        free(file_data);
        if (st != JXL_BS_OK) {
            fprintf(stderr, "%s: extract failed (%s)\n", k_digests[i].name,
                    jxl_bs_status_string(st));
            free(codestream);
            failed = 1;
            continue;
        }

        jxl_test_sha256_hex(codestream, cs_len, digest);
        free(codestream);

        if (cs_len != k_digests[i].len || strcmp(digest, k_digests[i].digest) != 0) {
            fprintf(stderr, "%s: digest mismatch (got len %zu %s, expected len %zu %s)\n",
                    k_digests[i].name, cs_len, digest, k_digests[i].len, k_digests[i].digest);
            failed = 1;
        }
    }

    if (failed) {
        return 1;
    }
    printf("test_container_verify: ok (%zu fixtures)\n", k_digest_count);
    return 0;
}
