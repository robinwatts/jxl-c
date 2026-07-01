# jxl-c

Manual C port of the [jxl-oxide](https://github.com/tirr-c/jxl-oxide) JPEG XL **decode** pipeline.

Fixtures and goldens come from the `third_party/jxl-oxide` git submodule. Decode parity oracle text files live in this repo under `tests/oracle/decode/`. All build and test workflows run from **jxl-c** only.

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
```

If bootstrap reports missing oracle files (unlikely — they are vendored):

```bash
./scripts/gen_decode_oracles.sh
```

## Paths

| Path | Purpose |
|------|---------|
| `third_party/jxl-oxide/crates/jxl-oxide-tests/decode/` | `input.jxl` fixtures + `output.buf.zst` goldens |
| `third_party/jxl-oxide/crates/jxl-oxide-tests/conformance/testcases/` | Conformance cases |
| `third_party/jxl-oxide/crates/jxl-oxide-tests/tests/cache/` | Downloaded `.npy` / `.icc` references |
| `tests/oracle/decode/` | Local C parity oracles (`modular_pass_group_offsets.txt`, etc.) |

Override the submodule location:

```bash
export JXL_OXIDE_RUST_ROOT=/path/to/jxl-oxide
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
  include/jxl_oxide/       # public C API
  src/                     # library sources
  tests/oracle/decode/     # vendored/regenerated parity oracles
  tools/                   # bench_decode, gen_decode_oracles
  third_party/jxl-oxide/   # git submodule (fixtures + Rust cross-checks)
```

See [PLAN.md](PLAN.md) for the port roadmap.

## CMake options

| Option | Default | Notes |
|--------|---------|-------|
| `JXL_OXIDE_RUST_ROOT` | `third_party/jxl-oxide` | Submodule root |
| `JXL_OXIDE_DECODE_ORACLE_DIR` | `tests/oracle/decode` | Local parity oracle files |
| `JXL_OXIDE_C_ENABLE_JBR` | ON | JPEG bitstream reconstruction |
| `JXL_OXIDE_C_BUILD_TOOLS` | OFF | `bench_decode` |
| `JXL_OXIDE_C_BUILD_FUZZ` | OFF | libFuzzer target (Clang) |

Dual-licensed under MIT and Apache 2.0 (same as jxl-oxide).
