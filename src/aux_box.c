// SPDX-License-Identifier: MIT OR Apache-2.0
#include "aux_box.h"

#include "bitstream/container/brotli_decode.h"

#if defined(JXL_OXIDE_C_ENABLE_JBR) && JXL_OXIDE_C_ENABLE_JBR
#include "jbr/data.h"
#endif

#include <string.h>

typedef enum {
    AUX_READER_INIT = 0,
    AUX_READER_RAW,
    AUX_READER_BROTLI,
    AUX_READER_NO_DATA,
} aux_reader_kind;

typedef struct {
    aux_reader_kind kind;
    int done;
    uint8_t *raw;
    size_t raw_len;
    size_t raw_cap;
    jxl_brotli_decoder *brotli;
    jxl_allocator_state *alloc;
} jxl_aux_box_reader;

typedef struct {
    jxl_box_type ty;
    uint8_t *data;
    size_t data_len;
} aux_box_entry;

struct jxl_aux_box_list {
    jxl_allocator_state *alloc;
    aux_box_entry *boxes;
    size_t box_count;
    size_t box_cap;
    jxl_aux_box_reader current_box;
    jxl_box_type current_box_ty;
    int has_current_box_ty;
    int last_box;
#if defined(JXL_OXIDE_C_ENABLE_JBR) && JXL_OXIDE_C_ENABLE_JBR
    jxl_jbr_data *jbrd;
    uint8_t *jbrd_staging;
    size_t jbrd_staging_len;
    size_t jbrd_staging_cap;
#endif
};

static jxl_bs_status_t grow_raw(jxl_aux_box_reader *reader, size_t need) {
    size_t new_cap;
    uint8_t *grown;
    if (need <= reader->raw_cap) {
        return JXL_BS_OK;
    }
    new_cap = reader->raw_cap == 0 ? 4096 : reader->raw_cap;
    while (new_cap < need) {
        if (new_cap > (SIZE_MAX / 2)) {
            return JXL_BS_VALIDATION_FAILED;
        }
        new_cap *= 2;
    }
    grown = jxl_realloc(reader->alloc, reader->raw, new_cap);
    if (grown == NULL) {
        return JXL_BS_EOF;
    }
    reader->raw = grown;
    reader->raw_cap = new_cap;
    return JXL_BS_OK;
}

static void aux_box_reader_reset(jxl_aux_box_reader *reader) {
    jxl_allocator_state *alloc = reader->alloc;
    jxl_free(alloc, reader->raw);
    if (reader->brotli != NULL) {
        jxl_brotli_decoder_destroy(alloc, reader->brotli);
    }
    memset(reader, 0, sizeof(*reader));
    reader->kind = AUX_READER_INIT;
    reader->alloc = alloc;
}

static void aux_box_reader_init(jxl_aux_box_reader *reader, jxl_allocator_state *alloc) {
    memset(reader, 0, sizeof(*reader));
    reader->kind = AUX_READER_INIT;
    reader->alloc = alloc;
}

static jxl_bs_status_t aux_box_reader_ensure_raw(jxl_aux_box_reader *reader) {
    if (reader->done) {
        return JXL_BS_OK;
    }
    if (reader->kind == AUX_READER_INIT) {
        reader->kind = AUX_READER_RAW;
    }
    return JXL_BS_OK;
}

static jxl_bs_status_t aux_box_reader_ensure_brotli(jxl_aux_box_reader *reader) {
    if (reader->done) {
        return JXL_BS_OK;
    }
    if (reader->kind == AUX_READER_INIT) {
        reader->brotli = jxl_brotli_decoder_create(reader->alloc);
        if (reader->brotli == NULL) {
            return JXL_BS_EOF;
        }
        reader->kind = AUX_READER_BROTLI;
    }
    return JXL_BS_OK;
}

static jxl_bs_status_t aux_box_reader_feed(jxl_aux_box_reader *reader, const uint8_t *data,
                                           size_t len) {
    if (reader->done) {
        return JXL_BS_VALIDATION_FAILED;
    }

    if (reader->kind == AUX_READER_INIT) {
        jxl_bs_status_t st = grow_raw(reader, len);
        if (st != JXL_BS_OK) {
            return st;
        }
        memcpy(reader->raw, data, len);
        reader->raw_len = len;
        reader->kind = AUX_READER_RAW;
        return JXL_BS_OK;
    }

    if (reader->kind == AUX_READER_RAW) {
        size_t need = reader->raw_len + len;
        jxl_bs_status_t st = grow_raw(reader, need);
        if (st != JXL_BS_OK) {
            return st;
        }
        memcpy(reader->raw + reader->raw_len, data, len);
        reader->raw_len = need;
        return JXL_BS_OK;
    }

    if (reader->kind == AUX_READER_BROTLI) {
        return jxl_brotli_decoder_feed(reader->brotli, data, len);
    }

    return JXL_BS_VALIDATION_FAILED;
}

static jxl_bs_status_t aux_box_reader_finalize(jxl_aux_box_reader *reader) {
    if (reader->done) {
        return JXL_BS_OK;
    }

    if (reader->kind == AUX_READER_BROTLI) {
        jxl_bs_status_t st = jxl_brotli_decoder_finish(reader->brotli);
        size_t out_len;
        const uint8_t *out;
        jxl_bs_status_t grow_st;
        if (st != JXL_BS_OK) {
            return st;
        }
        out_len = 0;
        out = jxl_brotli_decoder_output(reader->brotli, &out_len);
        grow_st = grow_raw(reader, out_len);
        if (grow_st != JXL_BS_OK) {
            return grow_st;
        }
        if (out_len > 0) {
            memcpy(reader->raw, out, out_len);
        }
        reader->raw_len = out_len;
        jxl_brotli_decoder_destroy(reader->alloc, reader->brotli);
        reader->brotli = NULL;
        reader->kind = AUX_READER_RAW;
    } else if (reader->kind == AUX_READER_INIT) {
        reader->kind = AUX_READER_NO_DATA;
    }

    reader->done = 1;
    return JXL_BS_OK;
}

static int is_jbrd_box(jxl_box_type ty) {
    return jxl_box_type_eq(ty, JXL_BOX_JPEG_RECONSTRUCTION);
}

#if defined(JXL_OXIDE_C_ENABLE_JBR) && JXL_OXIDE_C_ENABLE_JBR
static jxl_bs_status_t jbr_status_to_bs(jxl_jbr_status st) {
    switch (st) {
    case JXL_JBR_OK:
    case JXL_JBR_NEED_MORE_DATA:
        return JXL_BS_OK;
    case JXL_JBR_OUT_OF_MEMORY:
        return JXL_BS_EOF;
    default:
        return JXL_BS_VALIDATION_FAILED;
    }
}

static jxl_bs_status_t grow_jbrd_staging(jxl_aux_box_list *list, size_t need) {
    size_t new_cap;
    uint8_t *grown;
    if (need <= list->jbrd_staging_cap) {
        return JXL_BS_OK;
    }
    new_cap = list->jbrd_staging_cap == 0 ? 4096 : list->jbrd_staging_cap;
    while (new_cap < need) {
        if (new_cap > (SIZE_MAX / 2)) {
            return JXL_BS_VALIDATION_FAILED;
        }
        new_cap *= 2;
    }
    grown = jxl_realloc(list->alloc, list->jbrd_staging, new_cap);
    if (grown == NULL) {
        return JXL_BS_EOF;
    }
    list->jbrd_staging = grown;
    list->jbrd_staging_cap = new_cap;
    return JXL_BS_OK;
}

static jxl_bs_status_t jbrd_feed_bytes(jxl_aux_box_list *list, const uint8_t *data, size_t len) {
    size_t need;
    jxl_bs_status_t st;
    jxl_jbr_status jst;
    if (list->jbrd != NULL) {
        return jbr_status_to_bs(jxl_jbr_data_feed(list->alloc, list->jbrd, data, len));
    }

    need = list->jbrd_staging_len + len;
    st = grow_jbrd_staging(list, need);
    if (st != JXL_BS_OK) {
        return st;
    }
    memcpy(list->jbrd_staging + list->jbrd_staging_len, data, len);
    list->jbrd_staging_len = need;

    jxl_jbr_data *parsed = NULL;
    jst =
        jxl_jbr_data_try_parse(list->alloc, list->jbrd_staging, list->jbrd_staging_len, &parsed);
    if (jst == JXL_JBR_NEED_MORE_DATA) {
        return JXL_BS_OK;
    }
    if (jst != JXL_JBR_OK) {
        return JXL_BS_VALIDATION_FAILED;
    }
    jxl_free(list->alloc, list->jbrd_staging);
    list->jbrd_staging = NULL;
    list->jbrd_staging_len = 0;
    list->jbrd_staging_cap = 0;
    list->jbrd = parsed;
    return JXL_BS_OK;
}

static jxl_bs_status_t jbrd_finalize(jxl_aux_box_list *list) {
    if (list->jbrd == NULL) {
        return JXL_BS_VALIDATION_FAILED;
    }
    return jbr_status_to_bs(jxl_jbr_data_finalize(list->alloc, list->jbrd));
}
#endif

static jxl_bs_status_t grow_box_list(jxl_aux_box_list *list, size_t need) {
    size_t new_cap;
    aux_box_entry *grown;
    if (need <= list->box_cap) {
        return JXL_BS_OK;
    }
    new_cap = list->box_cap == 0 ? 4 : list->box_cap;
    while (new_cap < need) {
        if (new_cap > (SIZE_MAX / 2)) {
            return JXL_BS_VALIDATION_FAILED;
        }
        new_cap *= 2;
    }
    grown = jxl_realloc(list->alloc, list->boxes, new_cap * sizeof(*grown));
    if (grown == NULL) {
        return JXL_BS_EOF;
    }
    list->boxes = grown;
    list->box_cap = new_cap;
    return JXL_BS_OK;
}

static jxl_bs_status_t aux_box_list_finalize(jxl_aux_box_list *list) {
    jxl_bs_status_t st;
    aux_box_entry *entry;
    if (!list->has_current_box_ty) {
        return JXL_BS_OK;
    }

    if (is_jbrd_box(list->current_box_ty)) {
#if defined(JXL_OXIDE_C_ENABLE_JBR) && JXL_OXIDE_C_ENABLE_JBR
        jxl_bs_status_t st = jbrd_finalize(list);
        if (st != JXL_BS_OK) {
            return st;
        }
#else
        /* JBR disabled: discard jbrd payload. */
#endif
        list->has_current_box_ty = 0;
        return JXL_BS_OK;
    }

    st = aux_box_reader_finalize(&list->current_box);
    if (st != JXL_BS_OK) {
        return st;
    }

    st = grow_box_list(list, list->box_count + 1);
    if (st != JXL_BS_OK) {
        return st;
    }

    entry = &list->boxes[list->box_count++];
    entry->ty = list->current_box_ty;
    entry->data = list->current_box.raw;
    entry->data_len = list->current_box.raw_len;
    list->current_box.raw = NULL;
    list->current_box.raw_len = 0;
    list->current_box.raw_cap = 0;
    list->current_box.done = 0;
    list->current_box.kind = AUX_READER_INIT;
    list->has_current_box_ty = 0;
    return JXL_BS_OK;
}

jxl_aux_box_list *jxl_aux_box_list_create(jxl_allocator_state *alloc) {
    jxl_aux_box_list *list = jxl_calloc(alloc, 1, sizeof(*list));
    if (list == NULL) {
        return NULL;
    }
    list->alloc = alloc;
    aux_box_reader_init(&list->current_box, alloc);
    return list;
}

void jxl_aux_box_list_destroy(jxl_allocator_state *alloc, jxl_aux_box_list *list) {
    size_t i;
    jxl_allocator_state *a;
    if (list == NULL) {
        return;
    }
    a = alloc != NULL ? alloc : list->alloc;
    for (i = 0; i < list->box_count; ++i) {
        jxl_free(a, list->boxes[i].data);
    }
    jxl_free(a, list->boxes);
    aux_box_reader_reset(&list->current_box);
#if defined(JXL_OXIDE_C_ENABLE_JBR) && JXL_OXIDE_C_ENABLE_JBR
    jxl_jbr_data_destroy(a, list->jbrd);
    jxl_free(a, list->jbrd_staging);
#endif
    jxl_free(a, list);
}

jxl_bs_status_t jxl_aux_box_list_handle_event(jxl_aux_box_list *list,
                                              const jxl_parse_event *event) {
    switch (event->type) {
    case JXL_PARSE_EVENT_BITSTREAM_KIND:
    case JXL_PARSE_EVENT_CODESTREAM:
        return JXL_BS_OK;
    case JXL_PARSE_EVENT_NO_MORE_AUX_BOX:
        list->has_current_box_ty = 0;
        list->last_box = 1;
        return JXL_BS_OK;
    case JXL_PARSE_EVENT_AUX_BOX_START:
        list->has_current_box_ty = 1;
        list->current_box_ty = event->box_type;
        if (!is_jbrd_box(event->box_type)) {
            jxl_bs_status_t st;
            if (event->brotli_compressed) {
                st = aux_box_reader_ensure_brotli(&list->current_box);
            } else {
                st = aux_box_reader_ensure_raw(&list->current_box);
            }
            if (st != JXL_BS_OK) {
                return st;
            }
        }
        list->last_box = event->last_box;
        return JXL_BS_OK;
    case JXL_PARSE_EVENT_AUX_BOX_DATA:
        list->has_current_box_ty = 1;
        list->current_box_ty = event->box_type;
        if (is_jbrd_box(event->box_type)) {
#if defined(JXL_OXIDE_C_ENABLE_JBR) && JXL_OXIDE_C_ENABLE_JBR
            return jbrd_feed_bytes(list, event->data, event->data_len);
#else
            return JXL_BS_OK;
#endif
        }
        return aux_box_reader_feed(&list->current_box, event->data, event->data_len);
    case JXL_PARSE_EVENT_AUX_BOX_END:
        list->has_current_box_ty = 1;
        list->current_box_ty = event->box_type;
        return aux_box_list_finalize(list);
    }
    return JXL_BS_OK;
}

jxl_bs_status_t jxl_aux_box_list_eof(jxl_aux_box_list *list) {
    jxl_bs_status_t st = aux_box_list_finalize(list);
    if (st != JXL_BS_OK) {
        return st;
    }
    list->last_box = 1;
    return JXL_BS_OK;
}

static jxl_aux_box_data first_of_type(const jxl_aux_box_list *list, jxl_box_type ty) {
    size_t i;
    jxl_aux_box_data result;

    memset(&result, 0, sizeof(result));
    for (i = 0; i < list->box_count; ++i) {
        if (jxl_box_type_eq(list->boxes[i].ty, ty)) {
            result.tag = JXL_AUX_BOX_HAS_DATA;
            result.data = list->boxes[i].data;
            result.data_len = list->boxes[i].data_len;
            return result;
        }
    }

    if (list->last_box && (!list->has_current_box_ty || !jxl_box_type_eq(list->current_box_ty, ty))) {
        result.tag = JXL_AUX_BOX_NOT_FOUND;
        return result;
    }
    result.tag = JXL_AUX_BOX_DECODING;
    return result;
}

jxl_aux_box_data jxl_aux_box_list_first_of_type(const jxl_aux_box_list *list, jxl_box_type ty) {
    return first_of_type(list, ty);
}

jxl_aux_box_data jxl_aux_box_list_first_exif(const jxl_aux_box_list *list) {
    return first_of_type(list, JXL_BOX_EXIF);
}

jxl_aux_box_data jxl_aux_box_list_first_xml(const jxl_aux_box_list *list) {
    return first_of_type(list, JXL_BOX_XML);
}

#if defined(JXL_OXIDE_C_ENABLE_JBR) && JXL_OXIDE_C_ENABLE_JBR
jxl_aux_jbrd_data jxl_aux_box_list_jbrd(const jxl_aux_box_list *list) {
    jxl_aux_jbrd_data result;

    memset(&result, 0, sizeof(result));
    if (list == NULL) {
        result.tag = JXL_AUX_JBRD_NOT_FOUND;
        return result;
    }
    if (list->jbrd != NULL && jxl_jbr_data_header(list->jbrd) != NULL) {
        result.tag = JXL_AUX_JBRD_HAS_DATA;
        result.data = list->jbrd;
        return result;
    }
    if (list->last_box &&
        (!list->has_current_box_ty || !is_jbrd_box(list->current_box_ty))) {
        result.tag = JXL_AUX_JBRD_NOT_FOUND;
        return result;
    }
    result.tag = JXL_AUX_JBRD_DECODING;
    return result;
}
#endif
