// SPDX-License-Identifier: MIT OR Apache-2.0
#include "icc_decode.h"

#include "coding/decoder.h"
#include "coding/error.h"

#include <string.h>

static jxl_bs_status_t coding_to_bs(jxl_coding_status_t st) {
    switch (st) {
    case JXL_CODING_OK:
        return JXL_BS_OK;
    case JXL_CODING_EOF:
        return JXL_BS_EOF;
    case JXL_CODING_OUT_OF_MEMORY:
        return JXL_BS_EOF;
    default:
        return JXL_BS_VALIDATION_FAILED;
    }
}

static uint32_t icc_ctx(size_t idx, uint8_t b1, uint8_t b2) {
    uint32_t p1;
    uint32_t p2;
    if (idx <= 128) {
        return 0;
    }

    if ((b1 >= 'a' && b1 <= 'z') || (b1 >= 'A' && b1 <= 'Z')) {
        p1 = 0;
    } else if ((b1 >= '0' && b1 <= '9') || b1 == '.' || b1 == ',') {
        p1 = 1;
    } else if (b1 <= 1) {
        p1 = 2 + (uint32_t)b1;
    } else if (b1 <= 15) {
        p1 = 4;
    } else if (b1 >= 241 && b1 <= 254) {
        p1 = 5;
    } else if (b1 == 255) {
        p1 = 6;
    } else {
        p1 = 7;
    }

    if ((b2 >= 'a' && b2 <= 'z') || (b2 >= 'A' && b2 <= 'Z')) {
        p2 = 0;
    } else if ((b2 >= '0' && b2 <= '9') || b2 == '.' || b2 == ',') {
        p2 = 1;
    } else if (b2 <= 15) {
        p2 = 2;
    } else if (b2 >= 241) {
        p2 = 3;
    } else {
        p2 = 4;
    }

    return 1 + p1 + 8 * p2;
}

typedef struct {
    const uint8_t *data;
    size_t len;
    size_t pos;
} icc_mem_stream;

static jxl_bs_status_t icc_varint(icc_mem_stream *stream, uint64_t *value_out) {
    uint64_t value = 0;
    uint32_t shift = 0;
    while (shift < 63) {
        uint8_t b;
        if (stream->pos >= stream->len) {
            return JXL_BS_VALIDATION_FAILED;
        }
        b = stream->data[stream->pos++];
        value |= (uint64_t)(b & 0x7f) << shift;
        if ((b & 0x80) == 0) {
            *value_out = value;
            return JXL_BS_OK;
        }
        shift += 7;
    }
    *value_out = value;
    return JXL_BS_OK;
}

static uint8_t predict_header(size_t idx, uint32_t output_size, const uint8_t *header) {
    switch (idx) {
    case 0:
        return (uint8_t)(output_size >> 24);
    case 1:
        return (uint8_t)(output_size >> 16);
    case 2:
        return (uint8_t)(output_size >> 8);
    case 3:
        return (uint8_t)output_size;
    case 8:
        return 4;
    case 12:
        return 'm';
    case 13:
        return 'n';
    case 14:
        return 't';
    case 15:
        return 'r';
    case 16:
        return 'R';
    case 17:
        return 'G';
    case 18:
        return 'B';
    case 19:
        return ' ';
    case 20:
        return 'X';
    case 21:
        return 'Y';
    case 22:
        return 'Z';
    case 23:
        return ' ';
    case 36:
        return 'a';
    case 37:
        return 'c';
    case 38:
        return 's';
    case 39:
        return 'p';
    case 41:
        if (header[40] == 'A') {
            return 'P';
        }
        if (header[40] == 'M') {
            return 'S';
        }
        return 0;
    case 42:
        if (header[40] == 'A') {
            return 'P';
        }
        if (header[40] == 'M') {
            return 'F';
        }
        if (header[40] == 'S' && header[41] == 'G') {
            return 'I';
        }
        if (header[40] == 'S' && header[41] == 'U') {
            return 'N';
        }
        return 0;
    case 43:
        if (header[40] == 'A') {
            return 'L';
        }
        if (header[40] == 'M') {
            return 'T';
        }
        if (header[40] == 'S' && header[41] == 'G') {
            return ' ';
        }
        if (header[40] == 'S' && header[41] == 'U') {
            return 'W';
        }
        return 0;
    case 70:
        return 246;
    case 71:
        return 214;
    case 73:
        return 1;
    case 78:
        return 211;
    case 79:
        return 45;
    case 80:
    case 81:
    case 82:
    case 83:
        return header[idx - 76];
    default:
        return 0;
    }
}

static void shuffle2(const uint8_t *bytes, size_t len, uint8_t *out) {
    size_t idx;
    size_t height = len / 2;
    size_t odd = len % 2;
    size_t o = 0;
    for (idx = 0; idx < height; ++idx) {
        out[o++] = bytes[idx];
        out[o++] = bytes[idx + height + odd];
    }
    if (odd != 0) {
        out[o++] = bytes[height];
    }
}

static void shuffle4(const uint8_t *bytes, size_t len, uint8_t *out) {
    size_t idx;
    size_t step = len / 4;
    size_t wide_count = len % 4;
    size_t o = 0;
    for (idx = 0; idx < step; ++idx) {
        size_t j;
        size_t base = idx;
        for (j = 0; j < wide_count; ++j) {
            out[o++] = bytes[base];
            base += step + 1;
        }
        for (j = wide_count; j < 4; ++j) {
            out[o++] = bytes[base];
            base += step;
        }
    }
    for (idx = 1; idx <= wide_count; ++idx) {
        out[o++] = bytes[(step + 1) * idx - 1];
    }
}

static jxl_bs_status_t out_append(uint8_t *out, size_t out_cap, size_t *out_len, const uint8_t *src,
                                  size_t len) {
    if (*out_len > out_cap || len > out_cap - *out_len) {
        return JXL_BS_VALIDATION_FAILED;
    }
    memcpy(out + *out_len, src, len);
    *out_len += len;
    return JXL_BS_OK;
}

static jxl_bs_status_t out_append_u32_be(uint8_t *out, size_t out_cap, size_t *out_len, uint32_t v) {
    uint8_t bytes[4];
    bytes[0] = (uint8_t)(v >> 24);
    bytes[1] = (uint8_t)(v >> 16);
    bytes[2] = (uint8_t)(v >> 8);
    bytes[3] = (uint8_t)v;
    return out_append(out, out_cap, out_len, bytes, 4);
}

static uint32_t read_be_u32_width(const uint8_t *src, size_t width) {
    size_t i;
    uint32_t v = 0;
    for (i = 0; i < width; ++i) {
        v |= (uint32_t)src[i] << (8 * (width - 1 - i));
    }
    return v;
}

static jxl_bs_status_t icc_stream_decode(jxl_allocator_state *alloc, const uint8_t *stream,
                                         size_t stream_len, uint8_t **out_data, size_t *out_len) {
                                             size_t idx;
    uint64_t output_size_u64;
    uint64_t commands_size_u64;
    size_t commands_len;
    size_t header_size;
    size_t out_pos;
    uint64_t v;
    static const uint8_t common_tags[19][4] = {
        {'r', 'T', 'R', 'C'}, {'r', 'X', 'Y', 'Z'}, {'c', 'p', 'r', 't'}, {'w', 't', 'p', 't'},
        {'b', 'k', 'p', 't'}, {'r', 'X', 'Y', 'Z'}, {'g', 'X', 'Y', 'Z'}, {'b', 'X', 'Y', 'Z'},
        {'k', 'X', 'Y', 'Z'}, {'r', 'T', 'R', 'C'}, {'g', 'T', 'R', 'C'}, {'b', 'T', 'R', 'C'},
        {'k', 'T', 'R', 'C'}, {'c', 'h', 'a', 'd'}, {'d', 'e', 's', 'c'}, {'c', 'h', 'r', 'm'},
        {'d', 'm', 'n', 'd'}, {'d', 'm', 'd', 'd'}, {'l', 'u', 'm', 'i'},
    };
    static const uint8_t common_data[8][4] = {
        {'X', 'Y', 'Z', ' '}, {'d', 'e', 's', 'c'}, {'t', 'e', 'x', 't'}, {'m', 'l', 'u', 'c'},
        {'p', 'a', 'r', 'a'}, {'c', 'u', 'r', 'v'}, {'s', 'f', '3', '2'}, {'g', 'b', 'd', ' '},
    };
    icc_mem_stream input;
    jxl_bs_status_t st;
    size_t output_size;
    size_t commands_size;
    const uint8_t *commands;
    const uint8_t *data;
    size_t data_len;
    uint8_t *out;
    const uint8_t *header_data;

    if (alloc == NULL || stream == NULL || out_data == NULL || out_len == NULL) {
        return JXL_BS_VALIDATION_FAILED;
    }
    *out_data = NULL;
    *out_len = 0;

    input.data = stream;
    input.len = stream_len;
    input.pos = 0;

    output_size_u64 = 0;
    commands_size_u64 = 0;
    st = icc_varint(&input, &output_size_u64);
    if (st != JXL_BS_OK) {
        return st;
    }
    st = icc_varint(&input, &commands_size_u64);
    if (st != JXL_BS_OK) {
        return st;
    }

    if (output_size_u64 > (1ull << 28)) {
        return JXL_BS_VALIDATION_FAILED;
    }
    if (commands_size_u64 > stream_len - input.pos) {
        return JXL_BS_VALIDATION_FAILED;
    }
    output_size = (size_t)output_size_u64;
    commands_size = (size_t)commands_size_u64;

    commands = stream + input.pos;
    commands_len = commands_size;
    data = commands + commands_size;
    data_len = stream_len - input.pos - commands_size;

    header_size = output_size < 128 ? output_size : 128;
    if (data_len < header_size) {
        return JXL_BS_VALIDATION_FAILED;
    }

    out = NULL;
    if (output_size != 0) {
        out = jxl_alloc(alloc, output_size);
        if (out == NULL) {
            return JXL_BS_EOF;
        }
    }
    out_pos = 0;

    header_data = data;
    data += header_size;
    data_len -= header_size;
    for (idx = 0; idx < header_size; ++idx) {
        uint8_t p = predict_header(idx, (uint32_t)output_size_u64, header_data);
        out[out_pos++] = (uint8_t)(p + header_data[idx]);
    }
    if (output_size <= 128) {
        *out_data = out;
        *out_len = out_pos;
        return JXL_BS_OK;
    }

    icc_mem_stream commands_stream;
    commands_stream.data = commands;
    commands_stream.len = commands_len;
    commands_stream.pos = 0;

    v = 0;
    st = icc_varint(&commands_stream, &v);
    if (st != JXL_BS_OK) {
        jxl_free(alloc, out);
        return st;
    }

    if (v != 0) {
        uint64_t num_tags_u64 = v - 1;
        uint32_t prev_tagstart;
        uint32_t prev_tagsize;
        uint32_t num_tags;
	if (((output_size_u64 - 128) / 12) < num_tags_u64) {
            jxl_free(alloc, out);
            return JXL_BS_VALIDATION_FAILED;
        }

        num_tags = (uint32_t)num_tags_u64;
        st = out_append_u32_be(out, output_size, &out_pos, num_tags);
        if (st != JXL_BS_OK) {
            jxl_free(alloc, out);
            return st;
        }

        prev_tagstart = num_tags * 12u + 128u;
        prev_tagsize = 0;
        for (;;) {
            uint8_t tagcode;
            uint32_t tagstart;
            uint32_t tagsize;
            uint8_t command;
            const uint8_t *tag;
	    if (commands_stream.pos >= commands_stream.len) {
                *out_data = out;
                *out_len = out_pos;
                return JXL_BS_OK;
            }
            command = commands_stream.data[commands_stream.pos++];
            tagcode = command & 63u;

            tag = NULL;
            switch (tagcode) {
            case 0:
                tag = NULL;
                break;
            case 1:
                if (data_len < 4) {
                    jxl_free(alloc, out);
                    return JXL_BS_VALIDATION_FAILED;
                }
                tag = data;
                data += 4;
                data_len -= 4;
                break;
            default:
                if (tagcode >= 2 && tagcode <= 20) {
                    tag = common_tags[tagcode - 2];
                } else {
                    jxl_free(alloc, out);
                    return JXL_BS_VALIDATION_FAILED;
                }
                break;
            }
            if (tagcode == 0) {
                break;
            }

            tagstart = 0;
            if ((command & 64u) == 0) {
                tagstart = prev_tagstart + prev_tagsize;
            } else {
                uint64_t raw = 0;
                st = icc_varint(&commands_stream, &raw);
                if (st != JXL_BS_OK) {
                    jxl_free(alloc, out);
                    return st;
                }
                tagstart = (uint32_t)raw;
            }

            tagsize = 0;
            if ((command & 128u) != 0) {
                uint64_t raw = 0;
                st = icc_varint(&commands_stream, &raw);
                if (st != JXL_BS_OK) {
                    jxl_free(alloc, out);
                    return st;
                }
                tagsize = (uint32_t)raw;
            } else if ((memcmp(tag, "rXYZ", 4) == 0) || (memcmp(tag, "gXYZ", 4) == 0) ||
                       (memcmp(tag, "bXYZ", 4) == 0) || (memcmp(tag, "kXYZ", 4) == 0) ||
                       (memcmp(tag, "wtpt", 4) == 0) || (memcmp(tag, "bkpt", 4) == 0) ||
                       (memcmp(tag, "lumi", 4) == 0)) {
                tagsize = 20;
            } else {
                tagsize = prev_tagsize;
            }

            if ((uint64_t)tagstart + (uint64_t)tagsize > output_size_u64) {
                jxl_free(alloc, out);
                return JXL_BS_VALIDATION_FAILED;
            }

            prev_tagstart = tagstart;
            prev_tagsize = tagsize;

            st = out_append(out, output_size, &out_pos, tag, 4);
            if (st != JXL_BS_OK) {
                jxl_free(alloc, out);
                return st;
            }
            st = out_append_u32_be(out, output_size, &out_pos, tagstart);
            if (st != JXL_BS_OK) {
                jxl_free(alloc, out);
                return st;
            }
            st = out_append_u32_be(out, output_size, &out_pos, tagsize);
            if (st != JXL_BS_OK) {
                jxl_free(alloc, out);
                return st;
            }

            if (tagcode == 2) {
                st = out_append(out, output_size, &out_pos, (const uint8_t *)"gTRC", 4);
                if (st != JXL_BS_OK) {
                    jxl_free(alloc, out);
                    return st;
                }
                st = out_append_u32_be(out, output_size, &out_pos, tagstart);
                if (st != JXL_BS_OK) {
                    jxl_free(alloc, out);
                    return st;
                }
                st = out_append_u32_be(out, output_size, &out_pos, tagsize);
                if (st != JXL_BS_OK) {
                    jxl_free(alloc, out);
                    return st;
                }

                st = out_append(out, output_size, &out_pos, (const uint8_t *)"bTRC", 4);
                if (st != JXL_BS_OK) {
                    jxl_free(alloc, out);
                    return st;
                }
                st = out_append_u32_be(out, output_size, &out_pos, tagstart);
                if (st != JXL_BS_OK) {
                    jxl_free(alloc, out);
                    return st;
                }
                st = out_append_u32_be(out, output_size, &out_pos, tagsize);
                if (st != JXL_BS_OK) {
                    jxl_free(alloc, out);
                    return st;
                }
            } else if (tagcode == 3) {
                st = out_append(out, output_size, &out_pos, (const uint8_t *)"gXYZ", 4);
                if (st != JXL_BS_OK) {
                    jxl_free(alloc, out);
                    return st;
                }
                st = out_append_u32_be(out, output_size, &out_pos, tagstart + tagsize);
                if (st != JXL_BS_OK) {
                    jxl_free(alloc, out);
                    return st;
                }
                st = out_append_u32_be(out, output_size, &out_pos, tagsize);
                if (st != JXL_BS_OK) {
                    jxl_free(alloc, out);
                    return st;
                }

                st = out_append(out, output_size, &out_pos, (const uint8_t *)"bXYZ", 4);
                if (st != JXL_BS_OK) {
                    jxl_free(alloc, out);
                    return st;
                }
                st = out_append_u32_be(out, output_size, &out_pos, tagstart + tagsize * 2u);
                if (st != JXL_BS_OK) {
                    jxl_free(alloc, out);
                    return st;
                }
                st = out_append_u32_be(out, output_size, &out_pos, tagsize);
                if (st != JXL_BS_OK) {
                    jxl_free(alloc, out);
                    return st;
                }
            }
        }
    }

    while (commands_stream.pos < commands_stream.len) {
        uint8_t command = commands_stream.data[commands_stream.pos++];
        if (command == 1 || command == 2 || command == 3) {
            uint64_t num_u64 = 0;
            size_t num;
            st = icc_varint(&commands_stream, &num_u64);
            if (st != JXL_BS_OK) {
                jxl_free(alloc, out);
                return st;
            }
            num = (size_t)num_u64;
            if (num > data_len) {
                jxl_free(alloc, out);
                return JXL_BS_VALIDATION_FAILED;
            }
            if (num > output_size - out_pos) {
                jxl_free(alloc, out);
                return JXL_BS_VALIDATION_FAILED;
            }

            if (command == 1) {
                st = out_append(out, output_size, &out_pos, data, num);
                if (st != JXL_BS_OK) {
                    jxl_free(alloc, out);
                    return st;
                }
            } else if (command == 2) {
                shuffle2(data, num, out + out_pos);
                out_pos += num;
            } else {
                shuffle4(data, num, out + out_pos);
                out_pos += num;
            }
            data += num;
            data_len -= num;
        } else if (command == 4) {
            size_t i;
            uint8_t order;
            size_t stride;
            uint64_t num_u64;
            uint8_t flags;
            size_t width;
            size_t stride4;
            size_t num;
            const uint8_t *src;
            uint8_t *tmp;
            if (commands_stream.pos >= commands_stream.len) {
                jxl_free(alloc, out);
                return JXL_BS_VALIDATION_FAILED;
            }
            flags = commands_stream.data[commands_stream.pos++];
            width = (size_t)(flags & 3u) + 1u;
            order = (flags >> 2) & 3u;
            if (width == 3 || order == 3) {
                jxl_free(alloc, out);
                return JXL_BS_VALIDATION_FAILED;
            }

            stride = width;
            if ((flags & 16u) != 0) {
                uint64_t stride_u64 = 0;
                st = icc_varint(&commands_stream, &stride_u64);
                if (st != JXL_BS_OK) {
                    jxl_free(alloc, out);
                    return st;
                }
                stride = (size_t)stride_u64;
                if (stride < width) {
                    jxl_free(alloc, out);
                    return JXL_BS_VALIDATION_FAILED;
                }
            }
            stride4 = (stride > SIZE_MAX / 4) ? SIZE_MAX : stride * 4;
            if (stride4 >= out_pos) {
                jxl_free(alloc, out);
                return JXL_BS_VALIDATION_FAILED;
            }

            num_u64 = 0;
            st = icc_varint(&commands_stream, &num_u64);
            if (st != JXL_BS_OK) {
                jxl_free(alloc, out);
                return st;
            }
            num = (size_t)num_u64;
            if (num > data_len) {
                jxl_free(alloc, out);
                return JXL_BS_VALIDATION_FAILED;
            }
            if (num > output_size - out_pos) {
                jxl_free(alloc, out);
                return JXL_BS_VALIDATION_FAILED;
            }

            src = data;
            tmp = NULL;
            if (width == 2 || width == 4) {
                tmp = jxl_alloc(alloc, num);
                if (tmp == NULL && num != 0) {
                    jxl_free(alloc, out);
                    return JXL_BS_EOF;
                }
                if (width == 2) {
                    shuffle2(data, num, tmp);
                } else {
                    shuffle4(data, num, tmp);
                }
                src = tmp;
            }

            for (i = 0; i < num; i += width) {
                size_t j;
                uint32_t prev[3] = {0, 0, 0};
                uint32_t p;
                size_t block;
                for (j = 0; j <= order; ++j) {
                    size_t offset = out_pos - stride * (j + 1);
                    prev[j] = read_be_u32_width(out + offset, width);
                }

                p = prev[0];
                if (order == 1) {
                    p = (uint32_t)(2u * prev[0] - prev[1]);
                } else if (order == 2) {
                    p = (uint32_t)(3u * (prev[0] - prev[1]) + prev[2]);
                }

                block = width;
                if (block > num - i) {
                    block = num - i;
                }
                for (j = 0; j < block; ++j) {
                    uint32_t val = (uint32_t)src[i + j] + (p >> (8 * (width - 1 - j)));
                    out[out_pos++] = (uint8_t)val;
                }
            }

            jxl_free(alloc, tmp);
            data += num;
            data_len -= num;
        } else if (command == 10) {
            static const uint8_t xyz_prefix[8] = {'X', 'Y', 'Z', ' ', 0, 0, 0, 0};
            if (data_len < 12) {
                jxl_free(alloc, out);
                return JXL_BS_VALIDATION_FAILED;
            }
            if (20 > output_size - out_pos) {
                jxl_free(alloc, out);
                return JXL_BS_VALIDATION_FAILED;
            }

            st = out_append(out, output_size, &out_pos, xyz_prefix, 8);
            if (st != JXL_BS_OK) {
                jxl_free(alloc, out);
                return st;
            }
            st = out_append(out, output_size, &out_pos, data, 12);
            if (st != JXL_BS_OK) {
                jxl_free(alloc, out);
                return st;
            }
            data += 12;
            data_len -= 12;
        } else if (command >= 16 && command <= 23) {
            if (8 > output_size - out_pos) {
                jxl_free(alloc, out);
                return JXL_BS_VALIDATION_FAILED;
            }
            st = out_append(out, output_size, &out_pos, common_data[command - 16], 4);
            if (st != JXL_BS_OK) {
                jxl_free(alloc, out);
                return st;
            }
            st = out_append(out, output_size, &out_pos, (const uint8_t *)"\0\0\0\0", 4);
            if (st != JXL_BS_OK) {
                jxl_free(alloc, out);
                return st;
            }
        } else {
            jxl_free(alloc, out);
            return JXL_BS_VALIDATION_FAILED;
        }
    }

    if (out_pos != output_size) {
        jxl_free(alloc, out);
        return JXL_BS_VALIDATION_FAILED;
    }

    *out_data = out;
    *out_len = out_pos;
    return JXL_BS_OK;
}

static jxl_bs_status_t icc_decode_bytes(jxl_allocator_state *alloc, jxl_bs *bs, uint8_t **out_data,
                                      size_t *out_len) {
                                          uint64_t idx;
    uint64_t enc_size;
    uint8_t b1;
    uint8_t b2;
    jxl_bs_status_t st;
    jxl_coding_decoder *dec;
    uint8_t *data;
    if (alloc == NULL || bs == NULL || out_data == NULL || out_len == NULL) {
        return JXL_BS_VALIDATION_FAILED;
    }
    *out_data = NULL;
    *out_len = 0;

    enc_size = 0;
    st = jxl_bs_read_u64(bs, &enc_size);
    if (st != JXL_BS_OK) {
        return st;
    }
    if (enc_size > (1ull << 28)) {
        return JXL_BS_VALIDATION_FAILED;
    }

    dec = NULL;
    st = coding_to_bs(jxl_coding_decoder_parse(alloc, bs, 41, &dec));
    if (st != JXL_BS_OK) {
        return st;
    }

    st = coding_to_bs(jxl_coding_decoder_begin(dec, bs));
    if (st != JXL_BS_OK) {
        jxl_coding_decoder_destroy(alloc, dec);
        return st;
    }

    data = jxl_alloc(alloc, (size_t)enc_size);
    if (data == NULL) {
        jxl_coding_decoder_destroy(alloc, dec);
        return JXL_BS_EOF;
    }

    b1 = 0;
    b2 = 0;
    for (idx = 0; idx < enc_size; ++idx) {
        uint32_t sym = 0;
        st = coding_to_bs(
            jxl_coding_decoder_read_varint(dec, bs, icc_ctx((size_t)idx, b1, b2), &sym));
        if (st != JXL_BS_OK) {
            jxl_free(alloc, data);
            jxl_coding_decoder_destroy(alloc, dec);
            return st;
        }
        if (sym >= 256) {
            jxl_free(alloc, data);
            jxl_coding_decoder_destroy(alloc, dec);
            return JXL_BS_VALIDATION_FAILED;
        }
        data[idx] = (uint8_t)sym;
        b2 = b1;
        b1 = (uint8_t)sym;
    }

    st = coding_to_bs(jxl_coding_decoder_finalize(dec));
    jxl_coding_decoder_destroy(alloc, dec);
    if (st != JXL_BS_OK) {
        jxl_free(alloc, data);
        return st;
    }

    *out_data = data;
    *out_len = (size_t)enc_size;
    return JXL_BS_OK;
}

jxl_bs_status_t jxl_icc_decode(jxl_allocator_state *alloc, jxl_bs *bs, uint8_t **out_data,
                               size_t *out_len) {
    size_t encoded_len = 0;
    uint8_t *encoded = NULL;
    jxl_bs_status_t st = icc_decode_bytes(alloc, bs, &encoded, &encoded_len);
    if (st != JXL_BS_OK) {
        return st;
    }

    st = icc_stream_decode(alloc, encoded, encoded_len, out_data, out_len);
    jxl_free(alloc, encoded);
    return st;
}

jxl_bs_status_t jxl_icc_skip(jxl_allocator_state *alloc, jxl_bs *bs) {
    size_t len = 0;
    uint8_t *data = NULL;
    jxl_bs_status_t st = icc_decode_bytes(alloc, bs, &data, &len);
    jxl_free(alloc, data);
    return st;
}
