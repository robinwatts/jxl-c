// SPDX-License-Identifier: MIT OR Apache-2.0
#ifndef JXL_CODING_CDECODER_HOISTED_INLINE_H_
#define JXL_CODING_CDECODER_HOISTED_INLINE_H_

#include "coding/ans_read_inline.h"
#include "coding/cdecoder_modular_internal.h"
#include "coding/cdecoder_private.h"
#include "coding/integer_read_inline.h"

#include "jxl_oxide/jxl_types.h"

#define JXL_HOIST_READ_VARINT_LZ77(dec, bs, slot, cluster, dist_multiplier, out_token, on_err)   \
    do {                                                                                         \
        jxl_coding_decoder *_jxl_hdec = (dec);                                                   \
        jxl_coding_hoist_slot *_jxl_hslot = (slot);                                              \
        jxl_bs *_jxl_hbs = (bs);                                                                 \
        uint32_t _jxl_hr = 0;                                                                    \
        jxl_coding_status_t _jxl_hst;                                                            \
        if (_jxl_hslot->lz_num_to_copy > 0) {                                                    \
            _jxl_hr = _jxl_hdec->lz.window[_jxl_hslot->lz_copy_pos & 0xfffffu];                  \
            _jxl_hslot->lz_copy_pos += 1;                                                        \
            _jxl_hslot->lz_num_to_copy -= 1;                                                     \
            goto _jxl_hoist_lz77_store;                                                          \
        }                                                                                        \
        {                                                                                        \
            const jxl_ans_histogram *_jxl_hhist;                                                 \
            uint32_t _jxl_htok = 0;                                                              \
            if ((size_t)(cluster) >= _jxl_hdec->code.u.ans.count) {                              \
                on_err;                                                                          \
            }                                                                                    \
            _jxl_hhist = &_jxl_hdec->code.u.ans.histograms[cluster];                             \
            _jxl_hst = jxl_ans_histogram_read_symbol_inline(_jxl_hhist, _jxl_hbs,                \
                                                            &_jxl_hslot->ans_state, &_jxl_htok);  \
            if (_jxl_hst != JXL_CODING_OK) {                                                     \
                on_err;                                                                          \
            }                                                                                    \
            if (_jxl_htok >= _jxl_hdec->lz_min_symbol) {                                         \
                _jxl_hst = jxl_coding_decoder_lz77_from_repeat_token_hoisted(                  \
                    _jxl_hdec, _jxl_hbs, _jxl_hslot, (dist_multiplier), _jxl_htok,               \
                    &(out_token));                                                               \
                if (_jxl_hst != JXL_CODING_OK) {                                                 \
                    on_err;                                                                      \
                }                                                                                \
                break;                                                                           \
            }                                                                                    \
            if ((size_t)(cluster) >= _jxl_hdec->num_clusters) {                                \
                on_err;                                                                          \
            }                                                                                    \
            _jxl_hr = jxl_integer_read_uint_config(_jxl_hbs, &_jxl_hdec->configs[cluster],       \
                                                   _jxl_htok);                                   \
        }                                                                                        \
    _jxl_hoist_lz77_store:                                                                       \
        {                                                                                        \
            const size_t _jxl_hoff = (size_t)(_jxl_hslot->lz_num_decoded & 0xfffffu);            \
            if (_jxl_hoff >= _jxl_hdec->lz.window_cap) {                                         \
                _jxl_hst = jxl_coding_decoder_lz77_store_at(_jxl_hdec, &_jxl_hslot->lz_num_decoded, \
                                                            _jxl_hr);                            \
                if (_jxl_hst != JXL_CODING_OK) {                                                 \
                    on_err;                                                                      \
                }                                                                                \
            } else {                                                                             \
                _jxl_hdec->lz.window[_jxl_hoff] = _jxl_hr;                                       \
                _jxl_hslot->lz_num_decoded += 1;                                                 \
            }                                                                                    \
            (out_token) = _jxl_hr;                                                               \
        }                                                                                        \
    } while (0)

#endif /* JXL_CODING_CDECODER_HOISTED_INLINE_H_ */
