# jxl-c

Manual C port of the [jxl-oxide](https://github.com/tirr-c/jxl-oxide) JPEG XL **decode** pipeline.

The Rust workspace lives in a git submodule at `third_party/jxl-oxide`. C tests use Rust fixtures, goldens, and conformance references from that submodule; CI and local workflows are driven entirely from this repository.

## Quick start

```bash
git clone --recursive https://github.com/tirr-c/jxl-c.git
cd jxl-c

# If you cloned without --recursive:
./scripts/bootstrap.sh

cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j$(nproc)
ctest --test-dir build --output-on-failure
```

## Rust oracle (fixtures + cross-checks)

| Path | Purpose |
|------|---------|
| `third_party/jxl-oxide/crates/jxl-oxide-tests/decode/` | Decode fixtures + `output.buf.zst` goldens |
| `third_party/jxl-oxide/crates/jxl-oxide-tests/conformance/testcases/` | Conformance `input.jxl` + `test.json` |
| `third_party/jxl-oxide/crates/jxl-oxide-tests/tests/cache/` | Downloaded `.npy` / `.icc` references |

Override the Rust workspace root (e.g. monorepo checkout) with:

```bash
export JXL_OXIDE_RUST_ROOT=/path/to/jxl-oxide
```

CMake auto-detects `third_party/jxl-oxide` or a parent monorepo layout when `c/` is still nested inside jxl-oxide.

## Parity preflight

Runs Rust `grayalpha_toc` parity tests, then the full C test suite:

```bash
make parity-preflight
```

## Benchmarks (C vs Rust)

Single-threaded decode comparison using fixtures from the submodule:

```bash
scripts/bench_st_compare.sh --iters 40 --rounds 3
```

## Layout

See [PLAN.md](PLAN.md) for the port roadmap. High-level structure:

```
jxl-c/
  include/jxl_oxide/     # public C API
  src/                   # library sources
  tests/                 # unit, oracle, conformance (driven from C)
  tools/                 # bench_decode, oracle CLIs
  third_party/jxl-oxide/ # git submodule (Rust oracle)
```

## CMake options

| Option | Default | Notes |
|--------|---------|-------|
| `JXL_OXIDE_RUST_ROOT` | auto | Path to jxl-oxide workspace |
| `JXL_OXIDE_C_ENABLE_JBR` | ON | JPEG bitstream reconstruction |
| `JXL_OXIDE_C_BUILD_TOOLS` | OFF | `bench_decode` |
| `JXL_OXIDE_C_BUILD_FUZZ` | OFF | libFuzzer target (Clang) |

Dual-licensed under MIT and Apache 2.0 (same as jxl-oxide).
