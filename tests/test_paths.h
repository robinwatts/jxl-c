// SPDX-License-Identifier: MIT OR Apache-2.0
#ifndef JXL_TEST_PATHS_H_
#define JXL_TEST_PATHS_H_

/*
 * Test fixture paths for jxl-c.
 * CMake injects absolute paths; fallbacks match the repo layout with
 * third_party/jxl-oxide as the Rust oracle submodule.
 */
#ifndef JXL_OXIDE_RUST_ROOT
#define JXL_OXIDE_RUST_ROOT "third_party/jxl-oxide"
#endif

#ifndef JXL_OXIDE_FIXTURES_DIR
#define JXL_OXIDE_FIXTURES_DIR JXL_OXIDE_RUST_ROOT "/crates/jxl-oxide-tests/decode"
#endif

#ifndef JXL_OXIDE_DECODE_ORACLE_DIR
#define JXL_OXIDE_DECODE_ORACLE_DIR "tests/oracle/decode"
#endif

#ifndef JXL_OXIDE_CONFORMANCE_DIR
#define JXL_OXIDE_CONFORMANCE_DIR JXL_OXIDE_RUST_ROOT "/crates/jxl-oxide-tests/conformance/testcases"
#endif

#ifndef JXL_OXIDE_CACHE_DIR
#define JXL_OXIDE_CACHE_DIR JXL_OXIDE_RUST_ROOT "/crates/jxl-oxide-tests/tests/cache"
#endif

#endif /* JXL_TEST_PATHS_H_ */
