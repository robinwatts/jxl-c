// SPDX-License-Identifier: MIT OR Apache-2.0
#ifndef JXL_AUX_BOX_H_
#define JXL_AUX_BOX_H_

#include "allocator.h"
#include "bitstream/container/box_type.h"
#include "bitstream/container/parser.h"

#include <stddef.h>
#include "jxl_oxide/jxl_types.h"

typedef enum {
    JXL_AUX_BOX_DECODING = 0,
    JXL_AUX_BOX_NOT_FOUND,
    JXL_AUX_BOX_HAS_DATA,
} jxl_aux_box_data_tag;

typedef struct {
    jxl_aux_box_data_tag tag;
    const uint8_t *data;
    size_t data_len;
} jxl_aux_box_data;

typedef struct jxl_aux_box_list jxl_aux_box_list;

jxl_aux_box_list *jxl_aux_box_list_create(jxl_allocator_state *alloc);
void jxl_aux_box_list_destroy(jxl_allocator_state *alloc, jxl_aux_box_list *list);

jxl_bs_status_t jxl_aux_box_list_handle_event(jxl_aux_box_list *list, const jxl_parse_event *event);
jxl_bs_status_t jxl_aux_box_list_eof(jxl_aux_box_list *list);

jxl_aux_box_data jxl_aux_box_list_first_of_type(const jxl_aux_box_list *list, jxl_box_type ty);
jxl_aux_box_data jxl_aux_box_list_first_exif(const jxl_aux_box_list *list);
jxl_aux_box_data jxl_aux_box_list_first_xml(const jxl_aux_box_list *list);

#if defined(JXL_OXIDE_C_ENABLE_JBR) && JXL_OXIDE_C_ENABLE_JBR
typedef struct jxl_jbr_data jxl_jbr_data;

typedef enum {
    JXL_AUX_JBRD_DECODING = 0,
    JXL_AUX_JBRD_NOT_FOUND,
    JXL_AUX_JBRD_HAS_DATA,
} jxl_aux_jbrd_tag;

typedef struct {
    jxl_aux_jbrd_tag tag;
    const jxl_jbr_data *data;
} jxl_aux_jbrd_data;

jxl_aux_jbrd_data jxl_aux_box_list_jbrd(const jxl_aux_box_list *list);
#endif

#endif /* JXL_AUX_BOX_H_ */
