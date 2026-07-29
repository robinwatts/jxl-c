# jxl-c

C library for JPEG XL: decode, simple lossless encode, and JPEG→JXL recompression, sharing one public `jxl_context` and header surface under `include/jxl/`.

The decode pipeline is a manual port of [jxl-oxide](https://github.com/tirr-c/jxl-oxide). The JPEG encoder and CMS pieces are adapted from the JPEG XL Project (libjxl). Fixtures and goldens come from the `third_party/jxl-oxide` git submodule. Decode parity oracle text files live under `tests/oracle/decode/`. All build and test workflows run from **jxl-c** only.

## Quick start

```bash
git clone --recursive https://github.com/tirr-c/jxl-c.git
cd jxl-c

# If you cloned without --recursive:
./scripts/bootstrap.sh

cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j$(nproc)
python3 tests/conformance/gen_conformance_cases.py   # prefetch .npy cache
ctest --test-dir build --output-on-failure

# Optional: install headers + libjxl_c.a (+ pkg-config / CMake package) into a prefix.
# System Brotli and LCMS2 are still required to link.
cmake --install build --prefix /tmp/jxl-c-prefix

# Consume via pkg-config:
#   pkg-config --cflags --libs jxl_c
# or CMake: find_package(jxl_c CONFIG REQUIRED) then target_link_libraries(... jxl_c::jxl_c)
```

If bootstrap reports missing oracle files (unlikely — they are vendored):

```bash
./scripts/gen_decode_oracles.sh
```

## Public API

Umbrella header: `<jxl/jxl.h>`. Leaf headers for embedders:

| Header | Role |
|--------|------|
| `context.h` | Session + allocator (`jxl_context`) |
| `decode.h` / `types.h` | Decode + shared types |
| `encode.h` | JPEG→JXL encoder |
| `simple_lossless.h` | Simple modular lossless encode |
| `cms.h` / `cms_interface.h` | Color management |
| `status.h` | Public `jxl_status_t` |

## Paths

| Path | Purpose |
|------|---------|
| `third_party/jxl-oxide/crates/jxl-oxide-tests/decode/` | `input.jxl` fixtures + `output.buf.zst` goldens |
| `third_party/jxl-oxide/crates/jxl-oxide-tests/conformance/testcases/` | Conformance cases |
| `third_party/jxl-oxide/crates/jxl-oxide-tests/tests/cache/` | Downloaded `.npy` / `.icc` references |
| `tests/oracle/decode/` | Local C parity oracles (`modular_pass_group_offsets.txt`, etc.) |

Override the submodule location:

```bash
export JXL_C_RUST_ROOT=/path/to/jxl-oxide
```

Regenerate oracle files after decoder changes (C-only, no Rust toolchain):

```bash
./scripts/gen_decode_oracles.sh
```

## Parity preflight

```bash
make parity-preflight
```

## Benchmarks (C vs Rust)

```bash
scripts/bench_st_compare.sh --iters 40 --rounds 3
```

## Layout

```
jxl-c/
  include/jxl/             # public C API (decode + encode + CMS)
  src/                     # decode + shared internals
  src/encoder/             # JPEG→JXL + simple lossless (+ CMS helpers)
  tests/oracle/decode/     # vendored/regenerated parity oracles
  tools/                   # bench_decode, gen_decode_oracles
  third_party/jxl-oxide/   # git submodule (fixtures + Rust cross-checks)
```

See [PLAN.md](PLAN.md) for the decode port roadmap and [NOTICE](NOTICE) for licensing origins.

## CMake options

| Option | Default | Notes |
|--------|---------|-------|
| `JXL_C_RUST_ROOT` | `third_party/jxl-oxide` | Submodule root |
| `JXL_C_DECODE_ORACLE_DIR` | `tests/oracle/decode` | Local parity oracle files |
| `JXL_C_ENABLE_JBR` | ON | JPEG bitstream reconstruction |
| `JXL_C_ENABLE_SIMPLE_LOSSLESS` | ON | Simple lossless encoder |
| `JXL_C_ENABLE_JPEG_ENCODER` | ON | JPEG→JXL recompression encoder |
| `JXL_C_BUILD_TOOLS` | OFF | `bench_decode` |
| `JXL_C_BUILD_FUZZ` | OFF | libFuzzer target (Clang) |

## License

This repository combines code from more than one origin. See [NOTICE](NOTICE).

- Decode / oxide-derived sources: [LICENSE-MIT](LICENSE-MIT) **or** [LICENSE-APACHE](LICENSE-APACHE)
- libjxl-derived encoder / CMS sources: [LICENSE-BSD](LICENSE-BSD) (and [src/encoder/PATENTS](src/encoder/PATENTS) where applicable)
