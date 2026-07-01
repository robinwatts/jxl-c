# SPDX-License-Identifier: MIT OR Apache-2.0
cmake_minimum_required(VERSION 3.16)
project(jxl_oxide_c VERSION 0.0.0 LANGUAGES C)

include(FetchContent)
include(CTest)

# Match Brotli/LCMS2: C99 is the portability floor (no C11-only features).
set(CMAKE_C_STANDARD 99)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)

option(JXL_OXIDE_C_BUILD_SHARED "Build shared library" OFF)
option(JXL_OXIDE_C_BUILD_TESTS "Build C tests" ON)
option(JXL_OXIDE_C_REQUIRE_LCMS2 "Require LCMS2 at link time" ON)
option(JXL_OXIDE_C_SIMD_SSE41 "Enable SSE4.1 SIMD paths" ON)
option(JXL_OXIDE_C_SIMD_AVX2 "Enable AVX2 SIMD paths" ON)
option(JXL_OXIDE_C_SIMD_NEON "Enable NEON SIMD paths" ON)
option(JXL_OXIDE_C_SIMD_WASM128 "Enable WebAssembly SIMD128 paths" ON)
option(JXL_OXIDE_C_ENABLE_JBR "Enable JPEG bitstream reconstruction" ON)
option(JXL_OXIDE_C_ENABLE_THREADS "Enable thread pool" OFF)
option(JXL_OXIDE_C_ENABLE_SANITIZERS "Enable AddressSanitizer and UBSan" OFF)
option(JXL_OXIDE_C_BUILD_FUZZ "Build libFuzzer decode target (requires Clang)" OFF)
option(JXL_OXIDE_C_BUILD_TOOLS "Build developer tools (bench_decode)" OFF)
option(JXL_OXIDE_C_ENABLE_LTO "Enable link-time optimization in Release builds" OFF)

if(JXL_OXIDE_C_ENABLE_LTO)
    include(CheckIPOSupported)
    check_ipo_supported(RESULT JXL_OXIDE_C_IPO_SUPPORTED OUTPUT JXL_OXIDE_C_IPO_ERROR)
    if(JXL_OXIDE_C_IPO_SUPPORTED)
        set(JXL_OXIDE_C_IPO_ENABLED TRUE)
    else()
        message(WARNING "JXL_OXIDE_C_ENABLE_LTO is ON but IPO is not supported: ${JXL_OXIDE_C_IPO_ERROR}")
    endif()
endif()

function(jxl_oxide_c_enable_ipo target)
    if(JXL_OXIDE_C_IPO_ENABLED)
        set_property(TARGET ${target} PROPERTY INTERPROCEDURAL_OPTIMIZATION TRUE)
    endif()
endfunction()

if(JXL_OXIDE_C_ENABLE_THREADS)
    message(FATAL_ERROR "threads are not implemented in v1.")
endif()

if(JXL_OXIDE_C_ENABLE_SANITIZERS)
    add_compile_options(-fsanitize=address,undefined -fno-omit-frame-pointer -g)
    add_link_options(-fsanitize=address,undefined)
endif()

set(JXL_OXIDE_C_JBR_SOURCES "")
if(JXL_OXIDE_C_ENABLE_JBR)
    set(JXL_OXIDE_C_JBR_SOURCES
        src/jbr/output.c
        src/jbr/bit_writer.c
        src/jbr/huffman.c
        src/jbr/header.c
        src/jbr/data.c
        src/jbr/scan.c
        src/jbr/reconstruct.c)
endif()

set(BROTLI_DISABLE_TESTS ON CACHE BOOL "" FORCE)
set(BROTLI_BUILD_TOOLS OFF CACHE BOOL "" FORCE)
FetchContent_Declare(
    brotli
    GIT_REPOSITORY https://github.com/google/brotli.git
    GIT_TAG v1.1.0
    GIT_SHALLOW TRUE
)
FetchContent_MakeAvailable(brotli)

if(JXL_OXIDE_C_REQUIRE_LCMS2)
    find_package(PkgConfig QUIET)
    if(PkgConfig_FOUND)
        pkg_check_modules(lcms2 QUIET IMPORTED_TARGET lcms2)
    endif()
    if(NOT TARGET PkgConfig::lcms2)
        set(LCMS2_BUILD_SHARED OFF CACHE BOOL "" FORCE)
        set(LCMS2_BUILD_TESTS OFF CACHE BOOL "" FORCE)
        set(LCMS2_BUILD_TOOLS OFF CACHE BOOL "" FORCE)
        FetchContent_Declare(
            lcms2
            GIT_REPOSITORY https://github.com/mm2/Little-CMS.git
            GIT_TAG lcms2.19
            GIT_SHALLOW TRUE
        )
        FetchContent_MakeAvailable(lcms2)
    endif()
endif()

set(JXL_OXIDE_C_PUBLIC_HEADERS
    include/jxl_oxide/jxl_status.h
    include/jxl_oxide/jxl_types.h
    include/jxl_oxide/jxl_context.h
    include/jxl_oxide/jxl_oxide.h
)

set(JXL_OXIDE_C_BITSTREAM_SOURCES
    src/bitstream/consts.c
    src/bitstream/error.c
    src/bitstream/bitstream.c
    src/bitstream/container/box_header.c
    src/bitstream/container/parser.c
    src/bitstream/container/brotli_decode.c
)

set(JXL_OXIDE_C_IMAGE_SOURCES
    src/image/size_header.c
    src/image/parse_helpers.c
    src/image/image_metadata.c
    src/image/image_header.c
    src/image/icc_decode.c
    src/image/icc_parse.c
)

set(JXL_OXIDE_C_CODING_SOURCES
    src/coding/cutil.c
    src/coding/ans.c
    src/coding/prefix.c
    src/coding/cdecoder.c
    src/coding/permutation.c
)

set(JXL_OXIDE_C_GRID_SOURCES
    src/grid/alloc_tracker.c
    src/grid/aligned_grid.c
    src/grid/shared_subgrid.c
    src/grid/mutable_subgrid.c
)

set(JXL_OXIDE_C_MODULAR_SOURCES
    src/modular/mutil.c
    src/modular/sample.c
    src/modular/param.c
    src/modular/predictor.c
    src/modular/channel.c
    src/modular/transform/transform.c
    src/modular/transform/inverse.c
    src/modular/float_export_row.c
    src/modular/transform/rct.c
    src/modular/transform/squeeze.c
    src/modular/ma.c
    src/modular/modular_parse.c
    src/modular/image.c
    src/modular/transformed_grid.c
    src/modular/ma_flat.c
    src/modular/predictor_state.c
    src/modular/channel_decode.c
    src/modular/channel_decode_slow.c
    src/modular/subimage_decode.c
    src/modular/prepare_subimage.c
    src/modular/recursive_image.c
    src/modular/group_subimage.c
    src/modular/group_decode.c
    src/modular/region.c
)

set(JXL_OXIDE_C_VARDCT_SOURCES
    src/vardct/vutil.c
    src/vardct/dct_select.c
    src/vardct/lf.c
    src/vardct/dequant.c
    src/vardct/dequant_expand.c
    src/vardct/hf_pass.c
    src/vardct/hf_coeff.c
)

set(JXL_OXIDE_C_FRAME_SOURCES
    src/frame/filter.c
    src/frame/frame_header.c
    src/frame/toc.c
    src/frame/frame.c
    src/frame/pass_group.c
    src/frame/lf_global_modular.c
    src/frame/lf_global.c
    src/frame/patch.c
    src/frame/fnoise.c
    src/frame/fspline.c
    src/frame/lf_group.c
    src/frame/hf_global.c
)

set(JXL_OXIDE_C_RENDER_SOURCES
    src/render/image_buffer.c
    src/render/render_buffer.c
    src/render/render_frame.c
    src/render/render_util.c
    src/render/keyframe_render.c
    src/render/modular_encode.c
    src/render/modular_render.c
    src/render/modular_sample.c
    src/render/patch_render.c
    src/render/filter/gabor.c
    src/render/filter/epf.c
    src/render/filter/ycbcr.c
    src/render/filter/padded_f32.c
    src/render/filter/restoration.c
    src/render/features/noise.c
    src/render/features/spline.c
    src/render/blend.c
    src/render/modular_compose.c
    src/render/composite.c
    src/render/features/upsampling.c
    src/render/color_transform.c
    src/render/color_transform_apply.c
    src/render/color/fastmath.c
    src/render/color/pq.c
    src/render/color/rec2408.c
    src/render/color/gamut.c
    src/render/color/opsin.c
    src/render/color/tone_map.c
    src/render/color_encoding_util.c
    src/render/cms_lcms.c
    src/render/subgrid_f32.c
    src/render/vardct/dct_common.c
    src/render/vardct/dct.c
    src/render/vardct/dct_2d.c
    src/render/vardct/transform_common.c
    src/render/vardct/vtransform.c
    src/render/vardct/varblocks.c
    src/render/vardct/dequant_hf.c
    src/render/vardct/lf_dequant.c
    src/render/vardct/lf_smooth.c
    src/render/vardct/cfl_hf.c
    src/render/vardct/group_pipeline.c
    src/render/vardct/vardct_encode.c
    src/render/vardct/frame_render.c
    src/render/simd/features.c
)

set(JXL_OXIDE_C_SOURCES
    ${JXL_OXIDE_C_BITSTREAM_SOURCES}
    ${JXL_OXIDE_C_IMAGE_SOURCES}
    ${JXL_OXIDE_C_CODING_SOURCES}
    ${JXL_OXIDE_C_GRID_SOURCES}
    ${JXL_OXIDE_C_MODULAR_SOURCES}
    ${JXL_OXIDE_C_VARDCT_SOURCES}
    ${JXL_OXIDE_C_FRAME_SOURCES}
    ${JXL_OXIDE_C_RENDER_SOURCES}
    src/allocator.c
    src/context.c
    src/status.c
    src/deps_check.c
    src/codestream_collect.c
    src/aux_box.c
    src/container_reader.c
    src/decoder_init.c
    src/decoder.c
    ${JXL_OXIDE_C_JBR_SOURCES}
)

add_library(jxl_oxide_c_obj OBJECT ${JXL_OXIDE_C_SOURCES})
jxl_oxide_c_enable_ipo(jxl_oxide_c_obj)
target_include_directories(jxl_oxide_c_obj
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:include>
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src
)
target_compile_definitions(jxl_oxide_c_obj PRIVATE JXL_OXIDE_C_BUILDING)
if(JXL_OXIDE_C_ENABLE_JBR)
    target_compile_definitions(jxl_oxide_c_obj PUBLIC JXL_OXIDE_C_ENABLE_JBR=1)
endif()
if(JXL_OXIDE_C_BUILD_TESTS)
    target_compile_definitions(jxl_oxide_c_obj PRIVATE JXL_OXIDE_TESTING)
endif()

set(JXL_OXIDE_C_SIMD_SSE2_SOURCES "")
set(JXL_OXIDE_C_SIMD_SSE41_SOURCES "")
set(JXL_OXIDE_C_SIMD_AVX2_SOURCES "")
if(JXL_OXIDE_C_SIMD_SSE41)
    if(CMAKE_SYSTEM_PROCESSOR MATCHES "^(AMD64|amd64|x86_64|X86_64)$")
        list(APPEND JXL_OXIDE_C_SIMD_SSE2_SOURCES
            src/render/vardct/sse2_lane.c
            src/render/vardct/dct_2d_sse2.c
            src/render/vardct/transform_sse2.c)
        list(APPEND JXL_OXIDE_C_SIMD_SSE41_SOURCES
            src/render/vardct/transform_sse41.c
            src/render/filter/gabor_sse41.c
            src/render/filter/epf_sse41.c
            src/render/filter/ycbcr_sse41.c
            src/modular/transform/squeeze_sse41.c
            src/render/color/fastmath_sse41.c
            src/render/color/color_transform_sse41.c
            src/render/color/pq_sse41.c
            src/render/color/rec2408_sse41.c
            src/render/color/gamut_sse41.c
            src/render/color/tone_map_sse41.c)
        target_compile_definitions(jxl_oxide_c_obj PUBLIC JXL_HAVE_SIMD_SSE2=1)
        target_compile_definitions(jxl_oxide_c_obj PUBLIC JXL_HAVE_SIMD_SSE41=1)
        if(JXL_OXIDE_C_SIMD_AVX2)
            list(APPEND JXL_OXIDE_C_SIMD_AVX2_SOURCES
                src/modular/transform/rct_avx2.c
                src/modular/transform/squeeze_avx2.c
                src/render/modular_blit_avx2.c
                src/render/color/fastmath_avx2.c
                src/render/color/color_transform_avx2.c
                src/render/color/pq_avx2.c
                src/render/color/rec2408_avx2.c
                src/render/color/gamut_avx2.c
                src/render/color/opsin_avx2.c
                src/render/color/tone_map_avx2.c
                src/render/filter/ycbcr_avx2.c
                src/render/filter/gabor_avx2.c)
            target_compile_definitions(jxl_oxide_c_obj PUBLIC JXL_HAVE_SIMD_AVX2=1)
        endif()
    endif()
endif()
if(JXL_OXIDE_C_SIMD_SSE2_SOURCES)
    target_sources(jxl_oxide_c_obj PRIVATE ${JXL_OXIDE_C_SIMD_SSE2_SOURCES})
    set_source_files_properties(${JXL_OXIDE_C_SIMD_SSE2_SOURCES} PROPERTIES COMPILE_OPTIONS "-msse2")
endif()
if(JXL_OXIDE_C_SIMD_SSE41_SOURCES)
    target_sources(jxl_oxide_c_obj PRIVATE ${JXL_OXIDE_C_SIMD_SSE41_SOURCES})
    set_source_files_properties(${JXL_OXIDE_C_SIMD_SSE41_SOURCES} PROPERTIES COMPILE_OPTIONS "-msse4.1")
endif()
if(JXL_OXIDE_C_SIMD_AVX2_SOURCES)
    target_sources(jxl_oxide_c_obj PRIVATE ${JXL_OXIDE_C_SIMD_AVX2_SOURCES})
    set_source_files_properties(${JXL_OXIDE_C_SIMD_AVX2_SOURCES} PROPERTIES COMPILE_OPTIONS "-mavx2;-mfma;-msse4.1")
endif()

set(JXL_OXIDE_C_SIMD_NEON_SOURCES "")
if(JXL_OXIDE_C_SIMD_NEON)
    if(CMAKE_SYSTEM_PROCESSOR MATCHES "^(aarch64|arm64|ARM64)$")
        list(APPEND JXL_OXIDE_C_SIMD_NEON_SOURCES
            src/render/color/fastmath_neon.c
            src/render/color/color_transform_neon.c
            src/render/color/pq_neon.c
            src/render/color/rec2408_neon.c
            src/render/color/gamut_neon.c
            src/render/color/opsin_neon.c
            src/render/color/tone_map_neon.c
            src/render/filter/ycbcr_neon.c
            src/modular/transform/squeeze_neon.c
            src/modular/transform/rct_neon.c
            src/render/vardct/neon_lane.c
            src/render/vardct/dct_2d_neon.c
            src/render/vardct/transform_neon.c
            src/render/filter/epf_neon.c
            src/render/filter/gabor_neon.c)
        target_compile_definitions(jxl_oxide_c_obj PUBLIC JXL_HAVE_SIMD_NEON=1)
    endif()
endif()
if(JXL_OXIDE_C_SIMD_NEON_SOURCES)
    target_sources(jxl_oxide_c_obj PRIVATE ${JXL_OXIDE_C_SIMD_NEON_SOURCES})
endif()

set(JXL_OXIDE_C_SIMD_WASM128_SOURCES "")
if(JXL_OXIDE_C_SIMD_WASM128)
    if(CMAKE_SYSTEM_PROCESSOR MATCHES "^(wasm32|wasm64)$" OR EMSCRIPTEN)
        list(APPEND JXL_OXIDE_C_SIMD_WASM128_SOURCES
            src/render/vardct/wasm128_lane.c
            src/render/vardct/dct_2d_wasm128.c
            src/render/vardct/transform_wasm128.c
            src/render/filter/epf_wasm128.c
            src/modular/transform/squeeze_wasm128.c)
        target_compile_definitions(jxl_oxide_c_obj PUBLIC JXL_HAVE_SIMD_WASM128=1)
    endif()
endif()
if(JXL_OXIDE_C_SIMD_WASM128_SOURCES)
    target_sources(jxl_oxide_c_obj PRIVATE ${JXL_OXIDE_C_SIMD_WASM128_SOURCES})
    set_source_files_properties(${JXL_OXIDE_C_SIMD_WASM128_SOURCES} PROPERTIES COMPILE_OPTIONS "-msimd128")
endif()

if(TARGET brotlidec)
    set(JXL_OXIDE_C_BROTLI_TARGET brotlidec)
elseif(TARGET brotlidec-static)
    set(JXL_OXIDE_C_BROTLI_TARGET brotlidec-static)
else()
    message(FATAL_ERROR "Could not find Brotli decoder CMake target")
endif()

target_link_libraries(jxl_oxide_c_obj PUBLIC ${JXL_OXIDE_C_BROTLI_TARGET})

if(JXL_OXIDE_C_REQUIRE_LCMS2)
    if(TARGET PkgConfig::lcms2)
        message(STATUS "Using system LCMS2 from pkg-config")
        set(JXL_OXIDE_C_LCMS2_TARGET PkgConfig::lcms2)
    elseif(TARGET lcms2::lcms2)
        set(JXL_OXIDE_C_LCMS2_TARGET lcms2::lcms2)
    elseif(TARGET lcms2_static)
        set(JXL_OXIDE_C_LCMS2_TARGET lcms2_static)
    elseif(TARGET lcms2)
        set(JXL_OXIDE_C_LCMS2_TARGET lcms2)
    else()
        message(FATAL_ERROR "Could not find LCMS2 CMake target")
    endif()
    target_link_libraries(jxl_oxide_c_obj PUBLIC ${JXL_OXIDE_C_LCMS2_TARGET})
    target_compile_definitions(jxl_oxide_c_obj PUBLIC JXL_OXIDE_C_HAVE_LCMS2=1)
endif()

add_library(jxl_oxide_c STATIC)
jxl_oxide_c_enable_ipo(jxl_oxide_c)
target_sources(jxl_oxide_c PRIVATE $<TARGET_OBJECTS:jxl_oxide_c_obj>)
target_link_libraries(jxl_oxide_c PUBLIC jxl_oxide_c_obj ${JXL_OXIDE_C_BROTLI_TARGET})
if(JXL_OXIDE_C_REQUIRE_LCMS2)
    target_link_libraries(jxl_oxide_c PUBLIC ${JXL_OXIDE_C_LCMS2_TARGET})
    target_compile_definitions(jxl_oxide_c PUBLIC JXL_OXIDE_C_HAVE_LCMS2=1)
endif()
target_include_directories(jxl_oxide_c
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:include>
)

if(JXL_OXIDE_C_BUILD_SHARED)
    add_library(jxl_oxide_c_shared SHARED $<TARGET_OBJECTS:jxl_oxide_c_obj>)
    set_target_properties(jxl_oxide_c_shared PROPERTIES OUTPUT_NAME jxl_oxide_c)
    target_link_libraries(jxl_oxide_c_shared PUBLIC ${JXL_OXIDE_C_BROTLI_TARGET})
    if(JXL_OXIDE_C_REQUIRE_LCMS2)
        target_link_libraries(jxl_oxide_c_shared PUBLIC ${JXL_OXIDE_C_LCMS2_TARGET})
    endif()
    target_include_directories(jxl_oxide_c_shared
        PUBLIC
            $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
            $<INSTALL_INTERFACE:include>
    )
endif()

if(JXL_OXIDE_C_BUILD_FUZZ)
    if(NOT CMAKE_C_COMPILER_ID MATCHES "Clang")
        message(FATAL_ERROR "JXL_OXIDE_C_BUILD_FUZZ requires Clang (libFuzzer)")
    endif()
    add_library(jxl_fuzz_decode STATIC fuzz/jxl_fuzz_decode.c)
    target_include_directories(jxl_fuzz_decode PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/fuzz)
    target_link_libraries(jxl_fuzz_decode PUBLIC jxl_oxide_c)
    add_executable(decode_fuzzer fuzz/decode_fuzzer.c)
    target_link_libraries(decode_fuzzer PRIVATE jxl_fuzz_decode jxl_oxide_c)
    target_compile_options(decode_fuzzer PRIVATE -fsanitize=fuzzer,address,undefined -fno-omit-frame-pointer)
    target_link_options(decode_fuzzer PRIVATE -fsanitize=fuzzer,address,undefined)
    if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
        find_program(JXL_GXX NAMES g++ clang++)
        if(JXL_GXX)
            execute_process(
                COMMAND ${JXL_GXX} -print-file-name=libstdc++.so
                OUTPUT_VARIABLE JXL_LIBSTDCXX_SO
                OUTPUT_STRIP_TRAILING_WHITESPACE)
            if(IS_ABSOLUTE "${JXL_LIBSTDCXX_SO}" AND EXISTS "${JXL_LIBSTDCXX_SO}")
                get_filename_component(JXL_LIBSTDCXX_DIR "${JXL_LIBSTDCXX_SO}" DIRECTORY)
                target_link_directories(decode_fuzzer BEFORE PRIVATE "${JXL_LIBSTDCXX_DIR}")
            endif()
        endif()
    endif()
endif()

if(JXL_OXIDE_C_BUILD_TOOLS)
    add_executable(bench_decode tools/bench_decode.c)
    jxl_oxide_c_enable_ipo(bench_decode)
    target_link_libraries(bench_decode PRIVATE jxl_oxide_c)
    target_include_directories(bench_decode PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include)
endif()

if(JXL_OXIDE_C_BUILD_TESTS)
    enable_testing()

    # jxl-oxide Rust workspace: third_party/jxl-oxide submodule.
    if(NOT JXL_OXIDE_RUST_ROOT)
        set(JXL_OXIDE_RUST_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/third_party/jxl-oxide"
            CACHE PATH "Path to jxl-oxide submodule (fixtures + Rust oracle)")
    endif()
    if(NOT EXISTS "${JXL_OXIDE_RUST_ROOT}/crates/jxl-oxide-tests/decode")
        message(WARNING
            "jxl-oxide test fixtures not found under ${JXL_OXIDE_RUST_ROOT}; "
            "run: git submodule update --init --recursive third_party/jxl-oxide")
    endif()

    set(JXL_OXIDE_FIXTURES_DIR
        "${JXL_OXIDE_RUST_ROOT}/crates/jxl-oxide-tests/decode"
        CACHE PATH "Path to decode fixtures (input.jxl, goldens)"
    )
    set(JXL_OXIDE_DECODE_ORACLE_DIR
        "${CMAKE_CURRENT_SOURCE_DIR}/tests/oracle/decode"
        CACHE PATH "Path to local decode parity oracle text files"
    )
    set(JXL_OXIDE_CONFORMANCE_DIR
        "${JXL_OXIDE_RUST_ROOT}/crates/jxl-oxide-tests/conformance/testcases"
        CACHE PATH "Path to conformance testcases"
    )
    set(JXL_OXIDE_CACHE_DIR
        "${JXL_OXIDE_RUST_ROOT}/crates/jxl-oxide-tests/tests/cache"
        CACHE PATH "Path to conformance reference cache (.npy/.icc)"
    )

    set(JXL_OXIDE_C_TEST_INCLUDES
        ${CMAKE_CURRENT_SOURCE_DIR}/include
        ${CMAKE_CURRENT_SOURCE_DIR}/src
        ${CMAKE_CURRENT_SOURCE_DIR}/tests
        ${CMAKE_CURRENT_SOURCE_DIR}/tests/container
        ${CMAKE_CURRENT_SOURCE_DIR}/tests/header
        ${CMAKE_CURRENT_SOURCE_DIR}/tests/frame
        ${CMAKE_CURRENT_SOURCE_DIR}/tests/conformance
        ${CMAKE_CURRENT_SOURCE_DIR}/tests/unit
    )

    add_library(jxl_oxide_c_test_includes INTERFACE)
    target_include_directories(jxl_oxide_c_test_includes INTERFACE ${JXL_OXIDE_C_TEST_INCLUDES})
    target_compile_definitions(jxl_oxide_c_test_includes INTERFACE
        JXL_OXIDE_RUST_ROOT="${JXL_OXIDE_RUST_ROOT}"
        JXL_OXIDE_FIXTURES_DIR="${JXL_OXIDE_FIXTURES_DIR}"
        JXL_OXIDE_DECODE_ORACLE_DIR="${JXL_OXIDE_DECODE_ORACLE_DIR}"
        JXL_OXIDE_CONFORMANCE_DIR="${JXL_OXIDE_CONFORMANCE_DIR}"
        JXL_OXIDE_CACHE_DIR="${JXL_OXIDE_CACHE_DIR}"
    )

    add_executable(gen_decode_oracles tools/gen_decode_oracles.c)
    target_link_libraries(gen_decode_oracles PRIVATE jxl_oxide_c)
    target_include_directories(gen_decode_oracles PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/include
        ${CMAKE_CURRENT_SOURCE_DIR}/src)

    add_executable(test_decoder_stub tests/unit/test_decoder_stub.c)
    target_link_libraries(test_decoder_stub PRIVATE jxl_oxide_c jxl_oxide_c_test_includes)
    add_test(NAME c_unit_decoder_stub COMMAND test_decoder_stub)

    add_executable(test_deps tests/unit/test_deps.c)
    target_link_libraries(test_deps PRIVATE jxl_oxide_c jxl_oxide_c_test_includes)
    add_test(NAME c_unit_deps COMMAND test_deps)

    add_executable(test_allocator tests/unit/test_allocator.c)
    target_link_libraries(test_allocator PRIVATE jxl_oxide_c jxl_oxide_c_test_includes)
    target_include_directories(test_allocator PRIVATE ${JXL_OXIDE_C_TEST_INCLUDES})
    add_test(NAME c_unit_allocator COMMAND test_allocator)

    add_executable(test_cms_alloc tests/unit/test_cms_alloc.c)
    target_link_libraries(test_cms_alloc PRIVATE jxl_oxide_c jxl_oxide_c_test_includes)
    target_include_directories(test_cms_alloc PRIVATE
        ${JXL_OXIDE_C_TEST_INCLUDES}
        ${CMAKE_CURRENT_SOURCE_DIR}/src/render
    )
    add_test(NAME c_unit_cms_alloc COMMAND test_cms_alloc)

    add_executable(test_brotli_alloc tests/unit/test_brotli_alloc.c)
    target_link_libraries(test_brotli_alloc PRIVATE jxl_oxide_c jxl_oxide_c_test_includes)
    target_include_directories(test_brotli_alloc PRIVATE ${JXL_OXIDE_C_TEST_INCLUDES})
    add_test(NAME c_unit_brotli_alloc COMMAND test_brotli_alloc)

    add_executable(test_aux_box tests/unit/test_aux_box.c)
    target_link_libraries(test_aux_box PRIVATE jxl_oxide_c jxl_oxide_c_test_includes)
    target_include_directories(test_aux_box PRIVATE ${JXL_OXIDE_C_TEST_INCLUDES})
    add_test(NAME c_unit_aux_box COMMAND test_aux_box)

    add_executable(test_decoder_aux tests/unit/test_decoder_aux.c)
    target_link_libraries(test_decoder_aux PRIVATE jxl_oxide_c jxl_oxide_c_test_includes)
    target_include_directories(test_decoder_aux PRIVATE ${JXL_OXIDE_C_TEST_INCLUDES})
    add_test(NAME c_unit_decoder_aux COMMAND test_decoder_aux)

    add_executable(test_decoder_animation tests/unit/test_decoder_animation.c)
    target_link_libraries(test_decoder_animation PRIVATE jxl_oxide_c jxl_oxide_c_test_includes)
    target_include_directories(test_decoder_animation PRIVATE ${JXL_OXIDE_C_TEST_INCLUDES})
    target_compile_definitions(test_decoder_animation PRIVATE
        JXL_OXIDE_CONFORMANCE_DIR="${JXL_OXIDE_CONFORMANCE_DIR}"
    )
    add_test(NAME c_unit_decoder_animation COMMAND test_decoder_animation)

    add_executable(test_context tests/unit/test_context.c)
    target_link_libraries(test_context PRIVATE jxl_oxide_c jxl_oxide_c_test_includes)
    target_include_directories(test_context PRIVATE ${JXL_OXIDE_C_TEST_INCLUDES})
    add_test(NAME c_unit_context COMMAND test_context)

    add_executable(test_bitstream tests/unit/test_bitstream.c)
    target_link_libraries(test_bitstream PRIVATE jxl_oxide_c jxl_oxide_c_test_includes)
    target_include_directories(test_bitstream PRIVATE ${JXL_OXIDE_C_TEST_INCLUDES})
    add_test(NAME c_unit_bitstream COMMAND test_bitstream)

    add_executable(test_size_header tests/unit/test_size_header.c)
    target_link_libraries(test_size_header PRIVATE jxl_oxide_c jxl_oxide_c_test_includes)
    target_include_directories(test_size_header PRIVATE ${JXL_OXIDE_C_TEST_INCLUDES})
    target_compile_definitions(test_size_header PRIVATE
        JXL_OXIDE_FIXTURES_DIR="${JXL_OXIDE_FIXTURES_DIR}"
    )
    add_test(NAME c_unit_size_header COMMAND test_size_header)

    add_executable(test_coding_synthetic tests/unit/test_coding_synthetic.c)
    target_link_libraries(test_coding_synthetic PRIVATE jxl_oxide_c jxl_oxide_c_test_includes)
    target_include_directories(test_coding_synthetic PRIVATE
        ${JXL_OXIDE_C_TEST_INCLUDES}
        ${CMAKE_CURRENT_SOURCE_DIR}/tests/coding
    )
    add_test(NAME c_unit_coding_synthetic COMMAND test_coding_synthetic)

    add_executable(test_grid tests/unit/test_grid.c)
    target_link_libraries(test_grid PRIVATE jxl_oxide_c jxl_oxide_c_test_includes)
    target_include_directories(test_grid PRIVATE ${JXL_OXIDE_C_TEST_INCLUDES})
    add_test(NAME c_unit_grid COMMAND test_grid)

    add_executable(test_modular tests/unit/test_modular.c)
    target_link_libraries(test_modular PRIVATE jxl_oxide_c jxl_oxide_c_test_includes)
    target_include_directories(test_modular PRIVATE ${JXL_OXIDE_C_TEST_INCLUDES})
    add_test(NAME c_unit_modular COMMAND test_modular)

    add_executable(test_squeeze_simd tests/unit/test_squeeze_simd.c)
    target_link_libraries(test_squeeze_simd PRIVATE jxl_oxide_c jxl_oxide_c_test_includes)
    target_include_directories(test_squeeze_simd PRIVATE ${JXL_OXIDE_C_TEST_INCLUDES})
    add_test(NAME c_unit_squeeze_simd COMMAND test_squeeze_simd)

    add_executable(test_gabor_simd tests/unit/test_gabor_simd.c)
    target_link_libraries(test_gabor_simd PRIVATE jxl_oxide_c jxl_oxide_c_test_includes)
    target_include_directories(test_gabor_simd PRIVATE ${JXL_OXIDE_C_TEST_INCLUDES})
    add_test(NAME c_unit_gabor_simd COMMAND test_gabor_simd)

    add_executable(test_rct_simd tests/unit/test_rct_simd.c)
    target_link_libraries(test_rct_simd PRIVATE jxl_oxide_c jxl_oxide_c_test_includes)
    target_include_directories(test_rct_simd PRIVATE ${JXL_OXIDE_C_TEST_INCLUDES})
    add_test(NAME c_unit_rct_simd COMMAND test_rct_simd)

    add_executable(test_opsin_simd tests/unit/test_opsin_simd.c)
    target_link_libraries(test_opsin_simd PRIVATE jxl_oxide_c jxl_oxide_c_test_includes)
    target_include_directories(test_opsin_simd PRIVATE ${JXL_OXIDE_C_TEST_INCLUDES})
    add_test(NAME c_unit_opsin_simd COMMAND test_opsin_simd)

    add_executable(test_color_simd tests/unit/test_color_simd.c)
    target_link_libraries(test_color_simd PRIVATE jxl_oxide_c jxl_oxide_c_test_includes)
    target_include_directories(test_color_simd PRIVATE ${JXL_OXIDE_C_TEST_INCLUDES})
    add_test(NAME c_unit_color_simd COMMAND test_color_simd)

    if(JXL_OXIDE_C_ENABLE_JBR)
        add_executable(test_jbr tests/unit/test_jbr.c tests/container/sha256.c)
        target_link_libraries(test_jbr PRIVATE jxl_oxide_c jxl_oxide_c_test_includes)
        target_include_directories(test_jbr PRIVATE ${JXL_OXIDE_C_TEST_INCLUDES}
            ${CMAKE_CURRENT_SOURCE_DIR}/tests/container)
        add_test(NAME c_unit_jbr COMMAND test_jbr)
    endif()

    add_executable(test_ycbcr_simd tests/unit/test_ycbcr_simd.c)
    target_link_libraries(test_ycbcr_simd PRIVATE jxl_oxide_c jxl_oxide_c_test_includes)
    target_include_directories(test_ycbcr_simd PRIVATE ${JXL_OXIDE_C_TEST_INCLUDES})
    add_test(NAME c_unit_ycbcr_simd COMMAND test_ycbcr_simd)

    add_executable(test_modular_parse tests/unit/test_modular_parse.c)
    target_link_libraries(test_modular_parse PRIVATE jxl_oxide_c jxl_oxide_c_test_includes)
    target_include_directories(test_modular_parse PRIVATE ${JXL_OXIDE_C_TEST_INCLUDES})
    add_test(NAME c_unit_modular_parse COMMAND test_modular_parse)

    add_executable(test_modular_decode tests/unit/test_modular_decode.c)
    target_link_libraries(test_modular_decode PRIVATE jxl_oxide_c jxl_oxide_c_test_includes)
    target_include_directories(test_modular_decode PRIVATE ${JXL_OXIDE_C_TEST_INCLUDES})
    add_test(NAME c_unit_modular_decode COMMAND test_modular_decode)

    add_executable(test_modular_lf_quant tests/unit/test_modular_lf_quant.c)
    target_link_libraries(test_modular_lf_quant PRIVATE jxl_oxide_c jxl_oxide_c_test_includes)
    target_include_directories(test_modular_lf_quant PRIVATE ${JXL_OXIDE_C_TEST_INCLUDES})
    add_test(NAME c_unit_modular_lf_quant COMMAND test_modular_lf_quant)

    add_executable(test_modular_group_e2e tests/unit/test_modular_group_e2e.c)
    target_link_libraries(test_modular_group_e2e PRIVATE jxl_oxide_c jxl_oxide_c_test_includes)
    target_include_directories(test_modular_group_e2e PRIVATE ${JXL_OXIDE_C_TEST_INCLUDES})
    target_compile_definitions(test_modular_group_e2e PRIVATE
        JXL_OXIDE_FIXTURES_DIR="${JXL_OXIDE_FIXTURES_DIR}"
        JXL_OXIDE_CONFORMANCE_DIR="${JXL_OXIDE_CONFORMANCE_DIR}"
    )
    add_test(NAME c_unit_modular_group_e2e COMMAND test_modular_group_e2e)

    add_executable(test_srgb_benchmark tests/unit/test_srgb_benchmark.c)
    target_link_libraries(test_srgb_benchmark PRIVATE jxl_oxide_c jxl_oxide_c_test_includes)
    target_include_directories(test_srgb_benchmark PRIVATE ${JXL_OXIDE_C_TEST_INCLUDES})
    target_compile_definitions(test_srgb_benchmark PRIVATE
        JXL_OXIDE_FIXTURES_DIR="${JXL_OXIDE_FIXTURES_DIR}")
    add_test(NAME c_unit_srgb_benchmark COMMAND test_srgb_benchmark)

    add_executable(test_genshin_cafe tests/unit/test_genshin_cafe.c)
    target_link_libraries(test_genshin_cafe PRIVATE jxl_oxide_c jxl_oxide_c_test_includes)
    target_include_directories(test_genshin_cafe PRIVATE ${JXL_OXIDE_C_TEST_INCLUDES})
    target_compile_definitions(test_genshin_cafe PRIVATE
        JXL_OXIDE_FIXTURES_DIR="${JXL_OXIDE_FIXTURES_DIR}")
    add_test(NAME c_unit_genshin_cafe COMMAND test_genshin_cafe)

    add_executable(test_prereq_ref_composite tests/unit/test_prereq_ref_composite.c)
    target_link_libraries(test_prereq_ref_composite PRIVATE jxl_oxide_c jxl_oxide_c_test_includes)
    target_include_directories(test_prereq_ref_composite PRIVATE ${JXL_OXIDE_C_TEST_INCLUDES})
    target_compile_definitions(test_prereq_ref_composite PRIVATE
        JXL_OXIDE_CONFORMANCE_DIR="${JXL_OXIDE_CONFORMANCE_DIR}"
    )
    add_test(NAME c_unit_prereq_ref_composite COMMAND test_prereq_ref_composite)

    add_executable(test_clone_gray tests/unit/test_clone_gray.c)
    target_link_libraries(test_clone_gray PRIVATE jxl_oxide_c jxl_oxide_c_test_includes)
    target_include_directories(test_clone_gray PRIVATE ${JXL_OXIDE_C_TEST_INCLUDES})
    add_test(NAME c_unit_clone_gray COMMAND test_clone_gray)

    add_executable(test_vardct tests/unit/test_vardct.c)
    target_link_libraries(test_vardct PRIVATE jxl_oxide_c jxl_oxide_c_test_includes)
    target_include_directories(test_vardct PRIVATE ${JXL_OXIDE_C_TEST_INCLUDES})
    add_test(NAME c_unit_vardct COMMAND test_vardct)

    set(JXL_FRAME_FIELDS_INC
        ${CMAKE_CURRENT_SOURCE_DIR}/tests/frame/frame_fields.inc
    )

    add_executable(test_frame_parse tests/unit/test_frame_parse.c)
    target_link_libraries(test_frame_parse PRIVATE jxl_oxide_c jxl_oxide_c_test_includes)
    target_include_directories(test_frame_parse PRIVATE ${JXL_OXIDE_C_TEST_INCLUDES})
    target_compile_definitions(test_frame_parse PRIVATE
        JXL_OXIDE_FIXTURES_DIR="${JXL_OXIDE_FIXTURES_DIR}"
        JXL_FRAME_FIELDS_INC="${JXL_FRAME_FIELDS_INC}"
    )
    add_test(NAME c_unit_frame_parse COMMAND test_frame_parse)

    add_executable(test_pass_group tests/unit/test_pass_group.c)
    target_link_libraries(test_pass_group PRIVATE jxl_oxide_c jxl_oxide_c_test_includes)
    target_include_directories(test_pass_group PRIVATE ${JXL_OXIDE_C_TEST_INCLUDES})
    add_test(NAME c_unit_pass_group COMMAND test_pass_group)

    add_executable(test_noise_convolve tests/unit/test_noise_convolve.c)
    target_link_libraries(test_noise_convolve PRIVATE jxl_oxide_c jxl_oxide_c_test_includes)
    target_include_directories(test_noise_convolve PRIVATE ${JXL_OXIDE_C_TEST_INCLUDES})
    target_compile_definitions(test_noise_convolve PRIVATE
        JXL_OXIDE_CONFORMANCE_DIR="${JXL_OXIDE_CONFORMANCE_DIR}"
        JXL_OXIDE_TESTING
    )
    add_test(NAME c_unit_noise_convolve COMMAND test_noise_convolve)

    add_executable(test_icc_parse tests/unit/test_icc_parse.c)
    target_link_libraries(test_icc_parse PRIVATE jxl_oxide_c jxl_oxide_c_test_includes)
    target_include_directories(test_icc_parse PRIVATE ${JXL_OXIDE_C_TEST_INCLUDES}
        ${CMAKE_CURRENT_SOURCE_DIR}/src)
    target_compile_definitions(test_icc_parse PRIVATE JXL_OXIDE_CACHE_DIR="${JXL_OXIDE_CACHE_DIR}")
    add_test(NAME c_unit_icc_parse COMMAND test_icc_parse)

    add_executable(test_icc_skip tests/unit/test_icc_skip.c)
    target_link_libraries(test_icc_skip PRIVATE jxl_oxide_c jxl_oxide_c_test_includes)
    target_include_directories(test_icc_skip PRIVATE ${JXL_OXIDE_C_TEST_INCLUDES}
        ${CMAKE_CURRENT_SOURCE_DIR}/src)
    target_compile_definitions(test_icc_skip PRIVATE
        JXL_OXIDE_CONFORMANCE_DIR="${JXL_OXIDE_CONFORMANCE_DIR}")
    add_test(NAME c_unit_icc_skip COMMAND test_icc_skip)

    add_executable(test_render_dct tests/unit/test_render_dct.c)
    target_link_libraries(test_render_dct PRIVATE jxl_oxide_c jxl_oxide_c_test_includes)
    target_include_directories(test_render_dct PRIVATE ${JXL_OXIDE_C_TEST_INCLUDES})
    add_test(NAME c_unit_render_dct COMMAND test_render_dct)

    add_executable(test_render_varblocks tests/unit/test_render_varblocks.c)
    target_link_libraries(test_render_varblocks PRIVATE jxl_oxide_c jxl_oxide_c_test_includes)
    target_include_directories(test_render_varblocks PRIVATE ${JXL_OXIDE_C_TEST_INCLUDES})
    add_test(NAME c_unit_render_varblocks COMMAND test_render_varblocks)

    add_executable(test_epf_sse41 tests/unit/test_epf_sse41.c)
    target_link_libraries(test_epf_sse41 PRIVATE jxl_oxide_c jxl_oxide_c_test_includes)
    target_include_directories(test_epf_sse41 PRIVATE ${JXL_OXIDE_C_TEST_INCLUDES})
    if(JXL_OXIDE_C_SIMD_SSE41)
        target_compile_definitions(test_epf_sse41 PRIVATE JXL_HAVE_SIMD_SSE41=1)
    endif()
    add_test(NAME c_unit_epf_sse41 COMMAND test_epf_sse41)

    add_executable(test_dequant_hf tests/unit/test_dequant_hf.c)
    target_link_libraries(test_dequant_hf PRIVATE jxl_oxide_c jxl_oxide_c_test_includes)
    target_include_directories(test_dequant_hf PRIVATE ${JXL_OXIDE_C_TEST_INCLUDES})
    add_test(NAME c_unit_dequant_hf COMMAND test_dequant_hf)

    add_executable(test_group_pipeline tests/unit/test_group_pipeline.c)
    target_link_libraries(test_group_pipeline PRIVATE jxl_oxide_c jxl_oxide_c_test_includes)
    target_include_directories(test_group_pipeline PRIVATE ${JXL_OXIDE_C_TEST_INCLUDES})
    add_test(NAME c_unit_group_pipeline COMMAND test_group_pipeline)

    add_executable(test_minecraft_px440 tests/unit/test_minecraft_px440.c)
    target_link_libraries(test_minecraft_px440 PRIVATE jxl_oxide_c jxl_oxide_c_test_includes)
    target_include_directories(test_minecraft_px440 PRIVATE ${JXL_OXIDE_C_TEST_INCLUDES})
    target_compile_definitions(test_minecraft_px440 PRIVATE
        JXL_OXIDE_FIXTURES_DIR="${JXL_OXIDE_FIXTURES_DIR}"
    )

    add_executable(test_progressive_pg0 tests/unit/test_progressive_pg0.c)
    target_link_libraries(test_progressive_pg0 PRIVATE jxl_oxide_c jxl_oxide_c_test_includes)
    target_include_directories(test_progressive_pg0 PRIVATE ${JXL_OXIDE_C_TEST_INCLUDES})
    target_compile_definitions(test_progressive_pg0 PRIVATE
        JXL_OXIDE_CONFORMANCE_DIR="${JXL_OXIDE_CONFORMANCE_DIR}"
    )

    find_package(PkgConfig QUIET)
    if(PkgConfig_FOUND)
        pkg_check_modules(LIBZSTD libzstd)
    endif()
    if(LIBZSTD_FOUND)
        add_executable(test_zstd_fixture tests/unit/test_zstd_fixture.c)
        target_include_directories(test_zstd_fixture PRIVATE
            ${JXL_OXIDE_C_TEST_INCLUDES}
            ${LIBZSTD_INCLUDE_DIRS}
        )
        target_link_libraries(test_zstd_fixture PRIVATE ${LIBZSTD_LIBRARIES})
        target_compile_definitions(test_zstd_fixture PRIVATE
            JXL_OXIDE_FIXTURES_DIR="${JXL_OXIDE_FIXTURES_DIR}"
        )
        add_test(NAME c_unit_zstd_fixture COMMAND test_zstd_fixture)
    else()
        message(STATUS "libzstd not found; skipping test_zstd_fixture")
    endif()

    set(JXL_CONTAINER_DIGESTS_INC
        ${CMAKE_CURRENT_SOURCE_DIR}/tests/container/codestream_digests.inc
    )

    add_executable(test_container_gen
        tests/container/test_container_fixtures.c
        tests/container/sha256.c
        tests/container/collect_codestream.c
    )
    target_compile_definitions(test_container_gen PRIVATE _POSIX_C_SOURCE=200809L)
    target_link_libraries(test_container_gen PRIVATE jxl_oxide_c jxl_oxide_c_test_includes)
    target_include_directories(test_container_gen PRIVATE ${JXL_OXIDE_C_TEST_INCLUDES})
    target_compile_definitions(test_container_gen PRIVATE
        JXL_OXIDE_FIXTURES_DIR="${JXL_OXIDE_FIXTURES_DIR}"
    )

    add_custom_command(
        OUTPUT ${JXL_CONTAINER_DIGESTS_INC}
        COMMAND ${CMAKE_COMMAND} -E env
            JXL_UPDATE_CONTAINER_GOLDEN=${JXL_CONTAINER_DIGESTS_INC}
            $<TARGET_FILE:test_container_gen>
        DEPENDS test_container_gen
        COMMENT "Generating container codestream digests"
        VERBATIM
    )

    add_executable(test_container_verify
        tests/container/test_container_verify.c
        tests/container/sha256.c
        tests/container/collect_codestream.c
    )
    target_link_libraries(test_container_verify PRIVATE jxl_oxide_c jxl_oxide_c_test_includes)
    target_include_directories(test_container_verify PRIVATE ${JXL_OXIDE_C_TEST_INCLUDES})
    target_compile_definitions(test_container_verify PRIVATE
        JXL_OXIDE_FIXTURES_DIR="${JXL_OXIDE_FIXTURES_DIR}"
    )
    add_custom_target(container_digests_gen DEPENDS ${JXL_CONTAINER_DIGESTS_INC})
    add_dependencies(test_container_verify container_digests_gen)
    add_test(NAME c_container_fixtures COMMAND test_container_verify)

    set(JXL_HEADER_FIELDS_INC
        ${CMAKE_CURRENT_SOURCE_DIR}/tests/header/header_fields.inc
    )

    add_executable(test_header_gen tests/header/test_header_gen.c)
    target_compile_definitions(test_header_gen PRIVATE _POSIX_C_SOURCE=200809L)
    target_link_libraries(test_header_gen PRIVATE jxl_oxide_c jxl_oxide_c_test_includes)
    target_include_directories(test_header_gen PRIVATE ${JXL_OXIDE_C_TEST_INCLUDES})
    target_compile_definitions(test_header_gen PRIVATE
        JXL_OXIDE_FIXTURES_DIR="${JXL_OXIDE_FIXTURES_DIR}"
        JXL_HEADER_FIELDS_INC="${JXL_HEADER_FIELDS_INC}"
    )

    add_custom_command(
        OUTPUT ${JXL_HEADER_FIELDS_INC}
        COMMAND ${CMAKE_COMMAND} -E env
            JXL_UPDATE_HEADER_GOLDEN=${JXL_HEADER_FIELDS_INC}
            $<TARGET_FILE:test_header_gen>
        DEPENDS test_header_gen
        COMMENT "Generating header field goldens"
        VERBATIM
    )

    add_executable(test_header_verify tests/header/test_header_verify.c)
    target_link_libraries(test_header_verify PRIVATE jxl_oxide_c jxl_oxide_c_test_includes)
    target_include_directories(test_header_verify PRIVATE ${JXL_OXIDE_C_TEST_INCLUDES})
    target_compile_definitions(test_header_verify PRIVATE
        JXL_OXIDE_FIXTURES_DIR="${JXL_OXIDE_FIXTURES_DIR}"
    )
    add_custom_target(header_fields_gen DEPENDS ${JXL_HEADER_FIELDS_INC})
    add_dependencies(test_header_verify header_fields_gen)
    add_test(NAME c_header_fixtures COMMAND test_header_verify)

    function(jxl_add_oracle_fixture fixture_name)
        string(REPLACE "-" "_" fixture_target "${fixture_name}")
        set(target "test_oracle_${fixture_target}")
        set(test_name "c_oracle_${fixture_target}")

        add_executable(${target}
            tests/oracle/test_fixture_oracle.c
            tests/oracle/golden_buf_zst.c
        )
        target_link_libraries(${target} PRIVATE jxl_oxide_c jxl_oxide_c_test_includes)
        target_compile_definitions(${target} PRIVATE
            JXL_OXIDE_FIXTURES_DIR="${JXL_OXIDE_FIXTURES_DIR}"
            JXL_ORACLE_FIXTURE_NAME="${fixture_name}"
        )
        if(LIBZSTD_FOUND)
            target_compile_definitions(${target} PRIVATE JXL_HAVE_ZSTD=1)
            target_include_directories(${target} PRIVATE ${LIBZSTD_INCLUDE_DIRS})
            target_link_libraries(${target} PRIVATE ${LIBZSTD_LIBRARIES})
        endif()
        add_test(NAME ${test_name} COMMAND ${target})
        list(FIND JXL_ORACLE_PASSING_FIXTURES "${fixture_name}" _pass_idx)
        if(_pass_idx LESS 0)
            set_tests_properties(${test_name} PROPERTIES WILL_FAIL TRUE)
        endif()
    endfunction()

    set(JXL_ORACLE_PASSING_FIXTURES
        grayalpha
        squeeze_edge
        issue_311
        issue_24
        issue_26
        starrail_jpegli_xyb
        minecraft_vardct_e7
        issue_425
        genshin_ycbcr_420
        issue_32
    )
    jxl_add_oracle_fixture(grayalpha)
    jxl_add_oracle_fixture(minecraft_vardct_e7)
    jxl_add_oracle_fixture(squeeze_edge)
    jxl_add_oracle_fixture(issue_311)
    jxl_add_oracle_fixture(genshin_ycbcr_420)
    jxl_add_oracle_fixture(starrail_jpegli_xyb)
    jxl_add_oracle_fixture(issue_24)
    jxl_add_oracle_fixture(issue_26)
    jxl_add_oracle_fixture(issue_32)
    jxl_add_oracle_fixture(issue_425)

    set(JXL_CONFORMANCE_CASES_INC
        ${CMAKE_CURRENT_SOURCE_DIR}/tests/conformance/conformance_cases.inc
    )

    function(jxl_add_conformance_fixture fixture_name)
        string(REPLACE "-" "_" fixture_target "${fixture_name}")
        set(target "test_conformance_${fixture_target}")
        set(test_name "c_conformance_${fixture_target}")

        add_executable(${target}
            tests/conformance/test_conformance_fixture.c
            tests/conformance/conformance_npy.c
            tests/conformance/conformance_compare.c
        )
        target_link_libraries(${target} PRIVATE jxl_oxide_c jxl_oxide_c_test_includes m)
        target_include_directories(${target} PRIVATE ${JXL_OXIDE_C_TEST_INCLUDES})
        target_compile_definitions(${target} PRIVATE
            JXL_OXIDE_CONFORMANCE_DIR="${JXL_OXIDE_CONFORMANCE_DIR}"
            JXL_OXIDE_CACHE_DIR="${JXL_OXIDE_CACHE_DIR}"
            JXL_CONFORMANCE_CASES_INC="${JXL_CONFORMANCE_CASES_INC}"
            JXL_CONFORMANCE_FIXTURE_NAME="${fixture_name}"
        )
        add_test(NAME ${test_name} COMMAND ${target})
        list(FIND JXL_CONFORMANCE_PASSING_FIXTURES "${fixture_name}" _pass_idx)
        if(_pass_idx LESS 0)
            set_tests_properties(${test_name} PROPERTIES WILL_FAIL TRUE)
        endif()
    endfunction()

    set(JXL_CONFORMANCE_PASSING_FIXTURES
        alpha_nonpremultiplied
        alpha_premultiplied
        alpha_triangles
        animation_icos4d
        animation_icos4d_5
        animation_newtons_cradle
        animation_spline
        animation_spline_5
        bench_oriented_brg
        bench_oriented_brg_5
        bicycles
        bike
        bike_5
        blendmodes
        blendmodes_5
        cafe
        cafe_5
        cmyk_layers
        delta_palette
        grayscale
        grayscale_5
        grayscale_jpeg
        grayscale_jpeg_5
        lossless_pfm
        lz77_flower
        noise
        noise_5
        opsin_inverse
        opsin_inverse_5
        patches
        patches_5
        patches_lossless
        progressive
        progressive_5
        grayscale_public_university
        spot
        sunset_logo
        upsampling
        upsampling_5
    )
    set(JXL_CONFORMANCE_FIXTURES "")
    file(STRINGS "${JXL_OXIDE_CONFORMANCE_DIR}/main_level5.txt" JXL_CONFORMANCE_FIXTURES_L5)
    file(STRINGS "${JXL_OXIDE_CONFORMANCE_DIR}/main_level10.txt" JXL_CONFORMANCE_FIXTURES_L10)
    list(APPEND JXL_CONFORMANCE_FIXTURES ${JXL_CONFORMANCE_FIXTURES_L5} ${JXL_CONFORMANCE_FIXTURES_L10})
    list(REMOVE_DUPLICATES JXL_CONFORMANCE_FIXTURES)
    foreach(fixture_name IN LISTS JXL_CONFORMANCE_FIXTURES)
        jxl_add_conformance_fixture("${fixture_name}")
    endforeach()

    add_executable(test_crop_conformance tests/conformance/test_crop_conformance.c)
    target_link_libraries(test_crop_conformance PRIVATE jxl_oxide_c jxl_oxide_c_test_includes)
    target_include_directories(test_crop_conformance PRIVATE ${JXL_OXIDE_C_TEST_INCLUDES})
    target_compile_definitions(test_crop_conformance PRIVATE
        JXL_OXIDE_CONFORMANCE_DIR="${JXL_OXIDE_CONFORMANCE_DIR}")
    add_test(NAME c_unit_crop_conformance COMMAND test_crop_conformance)
    # ASan crop suite decodes every fixture full-frame plus crops (~64 min observed).
    set_tests_properties(c_unit_crop_conformance PROPERTIES TIMEOUT 7200)

    # Wide-buffer regression: force i32 modular storage on a passing modular-heavy fixture.
    add_test(NAME c_conformance_grayscale_force_wide
        COMMAND ${CMAKE_COMMAND} -E env JXL_FORCE_WIDE_BUFFERS=1
            $<TARGET_FILE:test_conformance_grayscale>)
endif()
