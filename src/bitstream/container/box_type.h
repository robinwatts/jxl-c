// SPDX-License-Identifier: MIT OR Apache-2.0
#ifndef JXL_CONTAINER_BOX_TYPE_H_
#define JXL_CONTAINER_BOX_TYPE_H_

#include "jxl_oxide/jxl_types.h"

typedef struct {
    uint8_t bytes[4];
} jxl_box_type;

jxl_inline int jxl_box_type_eq(jxl_box_type a, jxl_box_type b) {
    return a.bytes[0] == b.bytes[0] && a.bytes[1] == b.bytes[1] && a.bytes[2] == b.bytes[2] &&
           a.bytes[3] == b.bytes[3];
}

jxl_inline jxl_box_type jxl_box_type_of(const char *s) {
    jxl_box_type t;
    t.bytes[0] = (uint8_t)s[0];
    t.bytes[1] = (uint8_t)s[1];
    t.bytes[2] = (uint8_t)s[2];
    t.bytes[3] = (uint8_t)s[3];
    return t;
}

#define JXL_BOX_JXL jxl_box_type_of("JXL ")
#define JXL_BOX_FILE_TYPE jxl_box_type_of("ftyp")
#define JXL_BOX_JXL_LEVEL jxl_box_type_of("jxll")
#define JXL_BOX_JUMBF jxl_box_type_of("jumb")
#define JXL_BOX_EXIF jxl_box_type_of("Exif")
#define JXL_BOX_XML jxl_box_type_of("xml ")
#define JXL_BOX_BROTLI_COMPRESSED jxl_box_type_of("brob")
#define JXL_BOX_FRAME_INDEX jxl_box_type_of("jxli")
#define JXL_BOX_CODESTREAM jxl_box_type_of("jxlc")
#define JXL_BOX_PARTIAL_CODESTREAM jxl_box_type_of("jxlp")
#define JXL_BOX_JPEG_RECONSTRUCTION jxl_box_type_of("jbrd")
#define JXL_BOX_HDR_GAIN_MAP jxl_box_type_of("jhgm")

#endif /* JXL_CONTAINER_BOX_TYPE_H_ */
