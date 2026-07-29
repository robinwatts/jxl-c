// SPDX-License-Identifier: MIT OR Apache-2.0
#ifndef JXL_COMMON_ANS_PARAMS_H_
#define JXL_COMMON_ANS_PARAMS_H_

/* Shared ANS / prefix wire constants (ISO/IEC 18181-1). Used by decode and
 * the JPEG→JXL encoder. */

/* Valid range for the log table size is up to 16; 12 matches the shipped
 * encoder Huffman tables and decode alias buckets. */
#define ANS_LOG_TAB_SIZE 12u
#define ANS_TAB_SIZE (1u << ANS_LOG_TAB_SIZE)
#define ANS_TAB_MASK (ANS_TAB_SIZE - 1)

/* Largest possible symbol to be encoded by either ANS or prefix coding. */
#define PREFIX_MAX_ALPHABET_SIZE 4096
#define ANS_MAX_ALPHABET_SIZE 256

/* Max number of bits for prefix coding. */
#define PREFIX_MAX_BITS 15

/* Initial ANS state / stream signature (also used as a CRC seed). */
#define ANS_SIGNATURE 0x13

#endif /* JXL_COMMON_ANS_PARAMS_H_ */
