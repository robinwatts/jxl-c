// SPDX-License-Identifier: MIT OR Apache-2.0
/*
 * Example CLI: encode PPM/PAM to lossless JPEG XL using jxl_simple_lossless_encode.
 *
 * Usage: simple_lossless_cli in.ppm out.jxl [effort]
 */

#include "jxl/decode.h"

#include <errno.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
    const uint8_t *pos;
    const uint8_t *end;
} pnm_parser;

typedef struct {
    size_t xsize;
    size_t ysize;
    int is_gray;
    int has_alpha;
    size_t bits_per_sample;
} pnm_header;

static int pnm_is_digit(uint8_t c) { return '0' <= c && c <= '9'; }
static int pnm_is_linebreak(uint8_t c) { return c == '\r' || c == '\n'; }
static int pnm_is_whitespace(uint8_t c) {
    return pnm_is_linebreak(c) || c == '\t' || c == ' ';
}

static int pnm_error(const char *message) {
    fprintf(stderr, "%s\n", message);
    return 0;
}

static size_t pnm_log2_u32(uint32_t value) { return 31u - (size_t)__builtin_clz(value); }

static int pnm_parse_unsigned(pnm_parser *p, size_t *number) {
    if (p->pos == p->end) return pnm_error("PNM: reached end before number");
    if (!pnm_is_digit(*p->pos)) return pnm_error("PNM: expected unsigned number");
    *number = 0;
    while (p->pos < p->end && *p->pos >= '0' && *p->pos <= '9') {
        *number *= 10;
        *number += (size_t)(*p->pos - '0');
        ++p->pos;
    }
    return 1;
}

static int pnm_skip_whitespace(pnm_parser *p) {
    if (p->pos == p->end) return pnm_error("PNM: reached end before whitespace");
    if (!pnm_is_whitespace(*p->pos) && *p->pos != '#')
        return pnm_error("PNM: expected whitespace/comment");
    while (p->pos < p->end && pnm_is_whitespace(*p->pos)) ++p->pos;
    while (p->pos != p->end && *p->pos == '#') {
        while (p->pos != p->end && !pnm_is_linebreak(*p->pos)) ++p->pos;
        while (p->pos != p->end && pnm_is_linebreak(*p->pos)) p->pos++;
    }
    while (p->pos < p->end && pnm_is_whitespace(*p->pos)) ++p->pos;
    return 1;
}

static int pnm_skip_single_whitespace(pnm_parser *p) {
    if (p->pos == p->end) return pnm_error("PNM: reached end before whitespace");
    if (!pnm_is_whitespace(*p->pos)) return pnm_error("PNM: expected whitespace");
    ++p->pos;
    return 1;
}

static int pnm_match_string(pnm_parser *p, const char *keyword) {
    const uint8_t *ppos = p->pos;
    const uint8_t *kw = (const uint8_t *)keyword;
    while (*kw) {
        if (ppos >= p->end) return pnm_error("PAM: unexpected end of input");
        if (*kw != *ppos) return 0;
        ppos++;
        kw++;
    }
    p->pos = ppos;
    return pnm_skip_whitespace(p);
}

static int pnm_parse_header_pnm(pnm_parser *p, pnm_header *header, const uint8_t **pos);
static int pnm_parse_header_pam(pnm_parser *p, pnm_header *header, const uint8_t **pos);

static int pnm_parse_header(pnm_parser *p, pnm_header *header, const uint8_t **pos) {
    uint8_t type;
    if (p->pos[0] != 'P') return 0;
    type = p->pos[1];
    p->pos += 2;
    switch (type) {
    case '5': header->is_gray = 1; return pnm_parse_header_pnm(p, header, pos);
    case '6': header->is_gray = 0; return pnm_parse_header_pnm(p, header, pos);
    case '7': return pnm_parse_header_pam(p, header, pos);
    default: return 0;
    }
}

static int pnm_parse_header_pam(pnm_parser *p, pnm_header *header, const uint8_t **pos) {
    size_t num_channels = 3;
    size_t max_val = 255;
    while (!pnm_match_string(p, "ENDHDR")) {
        if (!pnm_skip_whitespace(p)) return 0;
        if (pnm_match_string(p, "WIDTH")) {
            if (!pnm_parse_unsigned(p, &header->xsize)) return 0;
        } else if (pnm_match_string(p, "HEIGHT")) {
            if (!pnm_parse_unsigned(p, &header->ysize)) return 0;
        } else if (pnm_match_string(p, "DEPTH")) {
            if (!pnm_parse_unsigned(p, &num_channels)) return 0;
        } else if (pnm_match_string(p, "MAXVAL")) {
            if (!pnm_parse_unsigned(p, &max_val)) return 0;
        } else if (pnm_match_string(p, "TUPLTYPE")) {
            if (pnm_match_string(p, "RGB_ALPHA")) header->has_alpha = 1;
            else if (pnm_match_string(p, "RGB")) {}
            else if (pnm_match_string(p, "GRAYSCALE_ALPHA")) {
                header->has_alpha = 1;
                header->is_gray = 1;
            } else if (pnm_match_string(p, "GRAYSCALE")) header->is_gray = 1;
            else if (pnm_match_string(p, "BLACKANDWHITE_ALPHA")) {
                header->has_alpha = 1;
                header->is_gray = 1;
                max_val = 1;
            } else if (pnm_match_string(p, "BLACKANDWHITE")) {
                header->is_gray = 1;
                max_val = 1;
            } else return pnm_error("PAM: unknown TUPLTYPE");
        } else return pnm_error("PAM: unknown header keyword");
    }
    if (num_channels != (size_t)((header->has_alpha ? 1 : 0) + (header->is_gray ? 1 : 3)))
        return pnm_error("PAM: bad DEPTH");
    if (max_val == 0 || max_val >= 65536) return pnm_error("PAM: bad MAXVAL");
    header->bits_per_sample = pnm_log2_u32((uint32_t)(max_val + 1));
    *pos = p->pos;
    return 1;
}

static int pnm_parse_header_pnm(pnm_parser *p, pnm_header *header, const uint8_t **pos) {
    size_t max_val;
    if (!pnm_skip_whitespace(p)) return 0;
    if (!pnm_parse_unsigned(p, &header->xsize)) return 0;
    if (!pnm_skip_whitespace(p)) return 0;
    if (!pnm_parse_unsigned(p, &header->ysize)) return 0;
    if (!pnm_skip_whitespace(p)) return 0;
    if (!pnm_parse_unsigned(p, &max_val)) return 0;
    if (max_val == 0 || max_val >= 65536) return pnm_error("PNM: bad MaxVal");
    header->bits_per_sample = pnm_log2_u32((uint32_t)(max_val + 1));
    if (!pnm_skip_single_whitespace(p)) return 0;
    *pos = p->pos;
    return 1;
}

static int load_file(const char *filename, unsigned char **out, size_t *outsize) {
    FILE *file = fopen(filename, "rb");
    size_t readsize;
    if (!file) return 0;
    if (fseek(file, 0, SEEK_END) != 0) {
        fclose(file);
        return 0;
    }
    *outsize = (size_t)ftell(file);
    if (*outsize == LONG_MAX || *outsize < 9 || fseek(file, 0, SEEK_SET) != 0) {
        fclose(file);
        return 0;
    }
    *out = (unsigned char *)malloc(*outsize);
    if (!*out) {
        fclose(file);
        return 0;
    }
    readsize = fread(*out, 1, *outsize, file);
    fclose(file);
    return readsize == *outsize;
}

static int decode_pam(const char *filename, uint8_t **buffer, size_t *w, size_t *h, size_t *nb_chans,
                      size_t *bitdepth) {
    unsigned char *in_file;
    size_t in_size;
    pnm_parser parser;
    pnm_header header;
    const uint8_t *pos = NULL;
    size_t pnm_remaining_size, buffer_size;

    memset(&header, 0, sizeof(header));
    if (!load_file(filename, &in_file, &in_size)) return pnm_error("Could not read input file");
    parser.pos = in_file;
    parser.end = in_file + in_size;
    if (!pnm_parse_header(&parser, &header, &pos)) return 0;
    if (header.bits_per_sample == 0 || header.bits_per_sample > 16)
        return pnm_error("PNM: bits_per_sample invalid (can do at most 16-bit)");
    *w = header.xsize;
    *h = header.ysize;
    *bitdepth = header.bits_per_sample;
    *nb_chans = (header.is_gray ? 1u : 3u) + (header.has_alpha ? 1u : 0u);
    pnm_remaining_size = (size_t)(in_file + in_size - pos);
    buffer_size = *w * *h * *nb_chans * (*bitdepth > 8 ? 2 : 1);
    if (pnm_remaining_size < buffer_size) return pnm_error("PNM file too small");
    *buffer = (uint8_t *)malloc(buffer_size);
    if (!*buffer) {
        free(in_file);
        return 0;
    }
    memcpy(*buffer, pos, buffer_size);
    free(in_file);
    return 1;
}

int main(int argc, char **argv) {
    const char *in_path;
    const char *out_path;
    int effort;
    jxl_context *ctx = NULL;
    jxl_simple_lossless_image_desc desc;
    uint8_t *image = NULL;
    uint8_t *encoded = NULL;
    size_t nb_chans, bitdepth, width, height, stride, encoded_len;
    jxl_status_t status;
    FILE *out_file;

    if (argc < 3) {
        fprintf(stderr, "Usage: %s in.ppm out.jxl [effort]\n", argv[0]);
        return 1;
    }
    in_path = argv[1];
    out_path = argv[2];
    effort = argc >= 4 ? atoi(argv[3]) : 2;
    if (effort < 0 || effort > 127) {
        fprintf(stderr, "Effort should be between 0 and 127 (default is 2)\n");
        return 1;
    }
    if (!decode_pam(in_path, &image, &width, &height, &nb_chans, &bitdepth)) {
        fprintf(stderr, "input error, could not load PPM/PAM file %s\n", in_path);
        return 1;
    }
    status = jxl_context_create(NULL, &ctx);
    if (status != JXL_OK) {
        fprintf(stderr, "jxl_context_create: %s\n", jxl_status_string(status));
        free(image);
        return 1;
    }
    memset(&desc, 0, sizeof(desc));
    desc.width = (uint32_t)width;
    desc.height = (uint32_t)height;
    desc.num_channels = (uint32_t)nb_chans;
    desc.bits_per_sample = (uint32_t)bitdepth;
    desc.big_endian = 1;
    desc.effort = effort;
    stride = width * nb_chans * (bitdepth > 8 ? 2 : 1);
    status = jxl_simple_lossless_encode(ctx, &desc, image, stride, &encoded, &encoded_len);
    free(image);
    if (status != JXL_OK) {
        fprintf(stderr, "encode failed: %s\n", jxl_status_string(status));
        jxl_context_destroy(ctx);
        return 1;
    }
    out_file = fopen(out_path, "wb");
    if (!out_file) {
        fprintf(stderr, "error opening %s: %s\n", out_path, strerror(errno));
        jxl_ctx_free(ctx, encoded);
        jxl_context_destroy(ctx);
        return 1;
    }
    if (fwrite(encoded, 1, encoded_len, out_file) != encoded_len)
        fprintf(stderr, "error writing to %s: %s\n", out_path, strerror(errno));
    fclose(out_file);
    jxl_ctx_free(ctx, encoded);
    jxl_context_destroy(ctx);
    return 0;
}
