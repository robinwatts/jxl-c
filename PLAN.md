# jxl-oxide C Port Plan

Manual port of the jxl-oxide decode pipeline to C, with Rust tests as the correctness oracle during development.

## Decisions (locked)

| Decision | Choice |
|----------|--------|
| Conversion strategy | **Option A** — manual port, crate-by-crate |
| Brotli | [google/brotli](https://github.com/google/brotli) C library |
| Color management | **LCMS2** (required link dependency) |
| Correctness oracle | Rust `jxl-oxide` + existing test fixtures |
| **v1 scope** | **Still images + animation** (multi-keyframe render; no timing/streaming API) |
| Threading | Deferred (single-threaded v1) |
| JBR (JPEG reconstruction) | **In v1** — `src/jbr/`; `JXL_OXIDE_C_ENABLE_JBR=ON` (CMake option to disable) |
| **`jxl_context`** | Explicit create/destroy required; pluggable allocator; CMS on context — see [CONTEXT_PLAN.md](CONTEXT_PLAN.md) |

### v1 scope

v1 decodes **every displayed keyframe** in an animated file, with the same planar float output as Rust `Render::image_planar()`. Still images are the degenerate case (one keyframe).

The core render pipeline is shared: reference-only frames, LF frames, blend compositing, and patch reference slots are required for both stills and animation.

**In scope for v1:**
- Container parse + codestream decode
- Multi-keyframe render: `jxl_decoder_num_keyframes()` + `jxl_decoder_render_keyframe(dec, idx, …)`
- `jxl_decoder_render()` as sugar for keyframe 0
- Planar float output `[0, 1]` matching Rust `Render::image_planar()`
- Crop decoding (still and animated)
- ICC via LCMS2; matrix color transforms in `jxl-color`
- Aux boxes (EXIF, etc.) — parse only if needed for parity tests
- Animation **blend compositing** via reference slots (`blending_info.source`), matching Rust `blend.rs`
- Decode oracle + conformance on **all keyframes** for animated fixtures
- **JPEG bitstream reconstruction** (`jxl-jbr`) — `jxl_decoder_jpeg_reconstruction_status()` + `jxl_decoder_reconstruct_jpeg()` when `JXL_OXIDE_C_ENABLE_JBR` is on

**Out of scope for v1:**
- Animation **timing** API (TPS, loop count, timecodes) — parse internally where the bitstream requires it, but do not expose getters
- Progressive multi-keyframe loading / partial animation decode while feeding
- CLI, WASM, `image` crate integration
- Thread pool

---

## Animation policy (v1)

Animated files are **first-class** in v1 — not a keyframe-0-only subset.

**Public API (implemented):**
- `jxl_decoder_try_init()` succeeds when `have_animation` is set; header fields match Rust.
- `jxl_decoder_num_keyframes()` returns the count of displayed keyframes.
- `jxl_decoder_render_keyframe(ctx, dec, idx, …)` renders any keyframe index.
- `jxl_decoder_render()` renders keyframe 0 (still images and animation).
- `jxl_render_keyframe_index()` / `jxl_render_duration()` on the result (`duration` parsed; timing metadata not otherwise exposed).

**Render policy (must match Rust):**
- Build the animation reference chain before each blend keyframe (`ensure_animation_refs`).
- Composite from **`blending_info.source`** ref slot — **not** “prior keyframe index − 1”. Frames alternate `save_as_reference` slots while blend source may point at an older slot.
- Do **not** skip blend, reference frames, or LF frames.
- Track `ct_done` through composite to avoid double color transforms on composed output.
- Decoder caches `animation_refs` incrementally (`animation_chain_upto`) for sequential keyframe renders.

**Bitstream parse (required even when not exposed):**
- Parse `AnimationHeader` when `have_animation` is set.
- Parse per-frame `duration` / `timecode` in `FrameHeader` for bitstream alignment.

`JXL_ERROR_ANIMATION_NOT_SUPPORTED` remains defined for API misuse (e.g. future streaming modes); it is **not** returned merely because a file is animated.

### Animation v1 gates

| Gate | Status |
|------|--------|
| Decode oracle `issue_24` (9 kf) | **Pass** |
| Decode oracle `issue_26` (42 kf, VarDCT) | **Pass** |
| Conformance `animation_newtons_cradle` (36 kf, modular) | **Pass** |
| Conformance `animation_icos4d` (48 kf, VarDCT + alpha) | **Pass** |
| Conformance `animation_spline` (60 kf, splines) | **Pass** |
| Crop tests on animation fixtures | **Pass** (all keyframes per region) |

**Remaining v1 animation work:** none — animation conformance, crop, and decode oracles are green.

## JBR policy (v1)

JPEG bitstream reconstruction re-encodes a VarDCT frame’s HF coefficients into a JPEG file when the container carries a `jbrd` box. Matches Rust `jxl-jbr` + `JxlImage::reconstruct_jpeg()`.

**Public API (when `JXL_OXIDE_C_ENABLE_JBR`):**
- `jxl_decoder_jpeg_reconstruction_status(dec)` — `Available` / `Decoding` / `NeedMoreData` / etc.
- `jxl_decoder_reconstruct_jpeg(dec, &buf, &len)` — allocates output via context allocator; caller frees with `jxl_free`

**Requirements (same as Rust):** non-XYB VarDCT keyframe, normal frame, no LF-frame / adaptive-LF-smoothing flags; embedded ICC/EXIF/XMP lengths must match `jbrd` header when expected.

**Gate:** `c_unit_jbr` (`test_jbr`) SHA256 of reconstructed JPEG vs Rust `jbrd` tests — 6 cases:
- Conformance: `grayscale_jpeg`, `cafe`, `bench_oriented_brg`
- Decode: `genshin_ycbcr_420`, `issue_425`, `starrail_jpegli_xyb`

**Status:** complete (default ON in CMake).

## Repository layout

```
jxl-c.git/                     # this repository (C port root)
  third_party/jxl-oxide/       # git submodule — Rust oracle + fixtures
  PLAN.md                      # this document
  CMakeLists.txt
  include/jxl_oxide/
  src/
  tests/
  tools/
```

The C port previously lived in `jxl-oxide.git/c/`; it is now a standalone repo with jxl-oxide as a submodule. See [README.md](README.md) for clone/bootstrap instructions.

Upstream Rust crates (oracle source) live under `third_party/jxl-oxide/crates/`:

```
third_party/jxl-oxide/
  crates/                    # oracle + upstream sync source
    jxl-bitstream/
    jxl-oxide-tests/         # fixtures, conformance, cache
    ...
  c/                         # removed — lives in jxl-c repo root
```

C library mapping (this repo):

```
src/
  bitstream/                 # jxl-bitstream
  common/                    # jxl-oxide-common
  coding/                    # jxl-coding
  grid/                      # jxl-grid
  image/                     # jxl-image
  color/                     # jxl-color (matrix paths; ICC → LCMS2)
  modular/                   # jxl-modular
  vardct/                    # jxl-vardct
  frame/                     # jxl-frame
  render/                    # jxl-render (includes blend)
  jbr/                       # jxl-jbr (optional; default ON)
  cms_lcms2.c                # LCMS2 adapter
  decoder.c                  # public decode API
tests/
  oracle/                    # golden vs output.buf.zst
  unit/
tools/
  jxl_oracle_decode.c
third_party/
  jxl-oxide/                 # git submodule
  brotli/                    # FetchContent at configure time
```

Build: CMake. Static library `jxl_oxide_c` (+ optional shared). LCMS2 and Brotli via `FetchContent` or submodules.

### CMake options

| Option | Default | Notes |
|--------|---------|-------|
| `JXL_OXIDE_C_SIMD_SSE41` | ON (x86) | Generic fallback always built |
| `JXL_OXIDE_C_SIMD_AVX2` | ON (x86) | |
| `JXL_OXIDE_C_SIMD_NEON` | ON (arm64) | |
| `JXL_OXIDE_C_REQUIRE_LCMS2` | ON | Hard dependency |
| `JXL_OXIDE_C_ENABLE_JBR` | ON | Set OFF to omit `src/jbr/` and JBR public API |
| `JXL_OXIDE_C_ENABLE_THREADS` | OFF | v2+ |

---

## Public C API (v1)

Opaque decoder; incremental feed; **multi-keyframe render** for animation.

```c
/* jxl_status.h */
typedef enum {
    JXL_OK = 0,
    JXL_NEED_MORE_DATA,
    JXL_ERROR_INVALID_INPUT,
    JXL_ERROR_UNSUPPORTED,
    JXL_ERROR_ANIMATION_NOT_SUPPORTED,  /* reserved — not used for animated init/render in v1 */
    JXL_ERROR_OUT_OF_MEMORY,
    JXL_ERROR_LIMIT_EXCEEDED,
} jxl_status_t;

/* jxl_oxide.h — sketch */
typedef struct jxl_context  jxl_context;
typedef struct jxl_decoder jxl_decoder;
typedef struct jxl_render  jxl_render;

typedef struct {
    jxl_allocator_t  alloc;        /* optional; default malloc/free/calloc/realloc */
    const jxl_cms   *cms;          /* NULL = built-in LCMS2 adapter */
} jxl_decoder_options;

jxl_status_t jxl_context_create(const jxl_decoder_options *opts, jxl_context **out);
void         jxl_context_destroy(jxl_context *ctx);

jxl_status_t jxl_decoder_create(jxl_context *ctx, const jxl_decoder_options *opts,
                                jxl_decoder **out);
jxl_status_t jxl_decoder_feed(jxl_decoder *dec, const uint8_t *data, size_t len);
jxl_status_t jxl_decoder_try_init(jxl_decoder *dec);

const jxl_image_header *jxl_decoder_header(const jxl_decoder *dec);

jxl_status_t jxl_decoder_request_icc(jxl_decoder *dec, const uint8_t *icc, size_t len);
jxl_status_t jxl_decoder_request_color_encoding(jxl_decoder *dec, jxl_color_encoding enc);

/* Renders keyframe 0 (still images and animation). */
jxl_status_t jxl_decoder_render(jxl_context *ctx, jxl_decoder *dec, jxl_render **out);

/* Multi-keyframe animation API. */
uint32_t     jxl_decoder_num_keyframes(const jxl_decoder *dec);
jxl_status_t jxl_decoder_render_keyframe(jxl_context *ctx, jxl_decoder *dec,
                                         uint32_t keyframe_index, jxl_render **out);

/* Optional crop — set before render */
jxl_status_t jxl_decoder_set_crop(jxl_decoder *dec, const jxl_crop *crop);

/* jxl_render: planar float [0,1], same layout as Rust Render::image_planar */
uint32_t    jxl_render_width(const jxl_render *r);
uint32_t    jxl_render_height(const jxl_render *r);
uint32_t    jxl_render_num_planes(const jxl_render *r);
uint32_t    jxl_render_keyframe_index(const jxl_render *r);
uint32_t    jxl_render_duration(const jxl_render *r);
const float *jxl_render_plane(const jxl_render *r, uint32_t plane);
const uint8_t *jxl_render_icc(const jxl_render *r, size_t *len);

void jxl_render_destroy(jxl_context *ctx, jxl_render *r);
void jxl_decoder_destroy(jxl_context *ctx, jxl_decoder *dec);
const char *jxl_decoder_last_error(const jxl_decoder *dec);

/* When JXL_OXIDE_C_ENABLE_JBR (default ON): */
jxl_jpeg_reconstruction_status jxl_decoder_jpeg_reconstruction_status(const jxl_decoder *dec);
jxl_status_t jxl_decoder_reconstruct_jpeg(jxl_decoder *dec, uint8_t **jpeg_out, size_t *jpeg_len);
```

**Not in v1:** TPS / loop / timecode getters, progressive loading callbacks, non-keyframe frame index API (`num_loaded_frames`).

**JBR:** Port of `crates/jxl-jbr` in `src/jbr/`; gated by `JXL_OXIDE_C_ENABLE_JBR`. See [JBR policy](#jbr-policy-v1).

**LCMS2:** Port `crates/jxl-oxide/src/lcms2.rs` as default CMS when `opts.cms == NULL`. Matrix-only transforms stay in `jxl-color` C code.

**Brotli:** Wrap `BrotliDecoderDecompressStream` for container boxes (`jxl-bitstream/container/`) and aux boxes (`jxl-oxide/aux_box.rs`).

---

## Oracle testing strategy

Rust is the source of truth. Three layers:

### Layer A — Decode fixtures (primary gate)

Fixtures: `crates/jxl-oxide-tests/decode/*/input.jxl` + `output.buf.zst`.

Golden format (from `jxl-oxide-cli generate-fixture`, `__devtools` feature):

```
[12 bytes: width, height, channels — LE u32]
per keyframe:
  [1 byte marker = 0]
  per plane: [width * height * u16 LE], sample = float * 65535 + 0.5
[1 byte trailer = 0xff]
```

v1 C tests compare **every keyframe** in the fixture (animated and still).

Thresholds (from `crates/jxl-oxide-tests/tests/decode/mod.rs`):
- VarDCT: peak error `(0.004 * 65535)` per channel
- Modular: `1 << (14 - bit_depth)` per channel

Animated decode regression fixtures: `issue_24`, `issue_26` (listed in `JXL_ORACLE_PASSING_FIXTURES`).

Generate goldens:
```bash
cargo run -p jxl-oxide-cli --features __devtools -- generate-fixture input.jxl output.buf
```

C test deps: **libzstd** (tests only) to read `.buf.zst`.

### Layer B — Conformance (full pipeline gate)

Fixtures: `crates/jxl-oxide-tests/conformance/testcases/*/input.jxl` + downloaded `.npy` references.

All cases in `main_level5.txt` and `main_level10.txt` must pass before v1 release. For **animated** conformance cases, compare **every keyframe** against the Rust `.npy` reference (same as Rust `tests/conformance/mod.rs`).

Conformance animation fixtures: `animation_newtons_cradle`, `animation_icos4d`, `animation_spline` (+ `_5` level-5 variants). All keyframes must match within per-case peak/RMSE limits.

### Layer C — Live diff (development)

```
tools/oracle_diff.sh input.jxl
  → Rust generate-fixture → /tmp/rust.buf
  → jxl_oracle_decode     → /tmp/c.buf
  → pixel diff with tolerance
```

Set `JXL_OXIDE_DEBUG=1` on Rust side for mismatch coordinates.

### CI

| Job | When |
|-----|------|
| `c-unit` | Every commit — bitstream, ANS, headers |
| `c-oracle-decode` | Every commit — decode fixtures, all keyframes |
| `c-oracle-conformance` | After phase 7 — full conformance (all keyframes for animation) |
| `rust-oracle-gen` | Manual — regenerate goldens if Rust output changes |

**Rule:** C tests never modify Rust. Rust output is always the oracle.

---

## Port order and test gates

Each phase merges only when its gate passes.

### Phase 0 — Scaffold (week 1)

- CMake, Brotli, LCMS2 linked
- `jxl_types.h`, `jxl_status.h`, allocator, error strings
- Stub `jxl_decoder_create` / `destroy`

**Gate:** Clean build on Linux, Windows, macOS.

---

### Phase 1 — Bitstream + container (~1 week)

**Port:** `jxl-bitstream` (~975 LOC) → `src/bitstream/`

- `Bitstream`, `U`, `U32`, F16, enums, signed unpack
- `ContainerParser` + Brotli-compressed boxes via google/brotli

**Gate:** Container extraction matches Rust for all decode fixtures.

---

### Phase 2 — Common parsers + image header (~2 weeks)

**Port:** `jxl-oxide-common` + `jxl-image` (~1,422 LOC)

- Replace `Bundle` / `read_bits!` macros with `jxl_*_parse()` per struct
- `SizeHeader`, `ImageMetadata`, `ColourEncoding`, `AnimationHeader`, `ImageHeader`
- Parse `AnimationHeader`; animated decode fixtures init successfully

**Gate:** Header fields match Rust for all decode fixtures (including `have_animation` on animated files).

---

### Phase 3 — Entropy coding (~2 weeks)

**Port:** `jxl-coding` (~1,510 LOC) → `src/coding/`

- ANS, prefix codes, LZ77, permutation

**Gate:** ANS unit tests on captured bitstream slices.

---

### Phase 4 — Grid (~2 weeks)

**Port:** `jxl-grid` (~2,007 LOC) → `src/grid/`

- Generic buffers first; SIMD deferred to phase 7

**Gate:** Grid unit tests.

---

### Phase 5 — Modular + VarDCT parse (~5 weeks)

**Port:** `jxl-modular` (~4,877) + `jxl-vardct` (~1,695) + frame data paths

**Gate:** Modular oracle fixtures pass once render exists (`grayalpha`, `squeeze_edge`, etc.).

---

### Phase 6 — Frame orchestration (~2 weeks)

**Port:** rest of `jxl-frame` (~2,604 LOC)

- TOC, group loading, progressive byte feed
- Internal multi-frame loading (reference/LF frames) and animation ref-chain compositing

**Gate:** Full codestream parse completes for still-image and animated fixtures.

---

### Phase 7 — Render (~8 weeks) — critical path

**Port:** `jxl-render` (~8,798) + `jxl-color` matrix paths (~5,481)

Sub-order:
1. VarDCT inverse DCT — generic C
2. Modular render path
3. **`blend.rs`** — frame compositing and animation ref-slot blending
4. Filters (EPF, Gabor, YCbCr)
5. Features (noise, splines, spot colors, upsampling)
6. LCMS2 at render boundary
7. SIMD (`generic` must pass oracle before intrinsics)

**Gate:** All decode fixtures (all keyframes) + full conformance corpus (all keyframes on animation cases).

---

### Phase 7a — Animation compositing (in progress)

**Port / fix:** animated blend via `blending_info.source`, ref-chain build, `ct_done` through composite, decoder ref cache.

**Gate:** Decode oracles `issue_24` + `issue_26`; conformance `animation_newtons_cradle`, `animation_icos4d`, `animation_spline`; animated crop tests.

**Status:** complete.

---

### Phase 8 — Top-level API (~1 week)

**Port:** `jxl-oxide` top layer (~2,778 LOC)

- `jxl_decoder_render` → `render_keyframe(0)`
- `jxl_decoder_num_keyframes` + `jxl_decoder_render_keyframe`
- Crop, aux boxes, ICC export
- `tools/jxl_oracle_decode.c`

**Gate:** Public API stable; oracle harness green on full decode suite (incl. `issue_24` / `issue_26`).

---

### Phase 8b — JBR (~1 week)

**Port:** `jxl-jbr` (~814 LOC) → `src/jbr/`

- `jbrd` aux box parse (header + brotli payload) in `aux_box.c`
- Frame coeff gather + Huffman scan encode (`reconstruct.c`, `scan.c`)
- Public: `jxl_decoder_jpeg_reconstruction_status`, `jxl_decoder_reconstruct_jpeg`

**Gate:** `c_unit_jbr` — 6 SHA256 cases vs Rust `jbrd` module (3 conformance + 3 decode fixtures).

**Status:** complete.

---

### Phase 9 — Hardening (~2 weeks)

- libFuzzer on `jxl_decoder_feed` — `c/fuzz/decode_fuzzer.c` (`JXL_OXIDE_C_BUILD_FUZZ=ON`, Clang)
- ASan/UBSan CI — `.github/workflows/c-sanitizers.yml` (`JXL_OXIDE_C_ENABLE_SANITIZERS=ON`)
- Benchmark vs Rust — `c/tools/bench_decode.c` (`JXL_OXIDE_C_BUILD_TOOLS=ON`)

---

## Module mapping

| Phase | Rust crate | C directory | LOC | Oracle gate |
|-------|-----------|-------------|-----|-------------|
| 1 | `jxl-bitstream` | `src/bitstream/` | 975 | Container bytes |
| 2 | `jxl-oxide-common`, `jxl-image` | `src/common/`, `src/image/` | 1,422 | Header parse (incl. animation metadata) |
| 3 | `jxl-coding` | `src/coding/` | 1,510 | ANS unit tests |
| 4 | `jxl-grid` | `src/grid/` | 2,007 | Grid unit tests |
| 5 | `jxl-modular`, `jxl-vardct` | `src/modular/`, `src/vardct/` | 6,572 | Modular fixtures |
| 6 | `jxl-frame` | `src/frame/` | 2,604 | Parse complete |
| 7 | `jxl-render`, `jxl-color` | `src/render/`, `src/color/` | 14,279 | Decode + conformance |
| 8 | `jxl-oxide` | `src/decoder.c`, `cms_lcms2.c` | ~2,778 | Public API + animation |
| 8b | `jxl-jbr` | `src/jbr/` | ~814 | `c_unit_jbr` (6 cases) |

Deferred: `jxl-threadpool`, animation timing/streaming API.

---

## Rust → C rules

1. **One Rust file → one C file** where possible (easier upstream sync).
2. **Hand-port `Bundle` parsers** in phase 2; revisit codegen only if macro volume blocks progress.
3. **`jxl_status_t` everywhere** internally; `jxl_decoder_last_error()` for detail.
4. **Ownership:** decoder owns state; `jxl_render` owns pixels; free only via `destroy`.
5. **Float samples:** planar `[0, 1]` `float` — matches Rust oracle format exactly.
6. **Animation:** multi-keyframe render in v1; composite from ref slots per Rust `blend.rs`; timing getters deferred.

---

## Dependencies

| Component | Library | Scope |
|-----------|---------|-------|
| Brotli decompress | google/brotli | Library |
| ICC transforms | LCMS2 | Library |
| Zstd | libzstd | **Tests only** |
| Oracle | Rust `jxl-oxide` workspace | Dev/CI |

---

## Timeline (1 developer)

| Phase | Duration | Cumulative |
|-------|----------|------------|
| 0 Scaffold | 1 week | 1 wk |
| 1 Bitstream | 1 week | 2 wk |
| 2 Headers + animation metadata | 2 weeks | 4 wk |
| 3 Coding | 2 weeks | 6 wk |
| 4 Grid | 2 weeks | 8 wk |
| 5 Modular + VarDCT | 5 weeks | 13 wk |
| 6 Frame | 2 weeks | 15 wk |
| 7 Render + color + SIMD | 8 weeks | 23 wk |
| 8 Top API | 1 week | 24 wk |
| 9 Hardening | 2 weeks | **26 wk (~6 months)** |

Second developer parallelizing phases 5–7 sub-modules: **~4–5 months**.

Animation multi-keyframe API and ref-chain compositing are **in v1**; animation timing/streaming remains v2.

### Crop tests (`tests/conformance/test_crop_conformance.c`)

Mirrors Rust `tests/crop/mod.rs`: `testcase_with_crop!` fixed regions, `testcase!` random crops (20 fixtures × 4 regions, PCG32-seeded), and `crop_upsampling_0` omitted like Rust `#[ignore]`. `jxl_crop` is in **display-oriented** coordinates (same as Rust `CropInfo`); render maps to codestream space before decode.

Rust also crops animated fixtures (`animation_icos4d`, `animation_spline`, `animation_newtons_cradle` in `tests/crop/mod.rs`). C crop harness compares **all keyframes** per crop region (same as Rust).

---

## Milestones

| # | Target | Criterion |
|---|--------|-----------|
| M1 | Week 4 | Parse header from any decode fixture (including animated) |
| M2 | Week 8 | Modular lossless decode end-to-end (generic C) |
| M3 | Week 14 | VarDCT lossy decode (generic C) |
| M4 | Week 20 | Conformance level-5 (all keyframes on animation fixtures) |
| M5 | Week 24 | v1 API frozen; SIMD; oracle + conformance green (incl. animation) |
| M6 | Week 26 | Animation conformance complete (`icos4d`, `spline`, crop) |

---

## Narrow modular buffers (i16 path)

Rust uses **`i16` modular storage by default** when `ImageMetadata.modular_16bit_buffers` is true (the common case). The C port currently keeps all modular grids as **`int32_t`** and discards the metadata flag after parse. Final render output stays **`float`** in both implementations; this work is **internal modular storage and wrapping arithmetic only**.

**Rust reference:** `Render::narrow_modular()` in `crates/jxl-render/src/lib.rs` — `!force_wide_buffers && metadata.modular_16bit_buffers`. VarDCT HF coefficients and CFL metadata remain **`i32`** regardless.

**Design (locked for this work):** tagged `jxl_modular_grid` + `jxl_modular_sample_ops` vtable (runtime dispatch). Do **not** duplicate every function by hand; do **not** store both widths in one destination — sample kind is fixed at creation, matching Rust monomorphization.

**Existing hooks:** `jxl_modular_params.narrow_buffer` (declared, never set); `modular/sample.c` i16 helpers (need spec-correct wrapping + i16 unpack).

### Phase N0 — Metadata and policy

- [x] Add `modular_16bit_buffers` to `jxl_parsed_image_header` (`image_internal.h`)
- [x] Persist parsed value in `image_metadata.c`; set `1` when `all_default` (Rust default)
- [x] Add `jxl_parsed_narrow_modular(const jxl_parsed_image_header *)`
- [x] Wire `jxl_modular_params.narrow_buffer` in `param.c` (`set_for_modular_frame`, `set_for_vardct_frame`)
- [x] Pass narrow flag into every `jxl_modular_image_destination_create` call site

**Gate:** no behavior change until N2; unit test that header parse sets narrow correctly on fixtures.

### Phase N1 — Grid abstraction

- [x] Introduce `jxl_modular_sample_kind` (`I16` / `I32`) and unified `jxl_modular_grid` (`image.h` / `image.c`)
- [x] Generalize create/destroy/view helpers (replace hard-coded `jxl_modular_grid_i32_*` API)
- [x] Fix i16 semantics in `sample.c`: wrapping add/muladd; add `jxl_modular_unpack_signed_u32_i16` (Rust i16 unpack ≠ i32)
- [x] Grid sample accessors (`sample_as_i32`, `store_i32`, typed row pointers) in `image.c`
- [x] Update `transformed_grid.h` / `transformed_grid.c` to hold typed `jxl_modular_grid` alias
- [x] Add `sample_kind` to `jxl_modular_image_destination`
- [x] Update `prepare_subimage` tile/split/merge helpers for 2- vs 4-byte elements (element-index views in `image.c`; kind inherited via struct copy)

**Gate:** grid unit tests allocate i16 and i32 buffers; i16 wrap oracle (values > 32767) matches Rust `Sample` impl.

### Phase N2 — Channel decode

- [x] Dispatch `channel_decode.c` inner loops on grid kind / sample ops (`dec_sample` helpers: unpack/add/muladd/grad, `row_get`/`row_set`)
- [x] Update `predictor_state.c` / `predictor.c` — reads via `sample_as_i32`; predictor state stays i32 internally (matches Rust)
- [x] `ma_flat.c` — no grid access; no change needed
- [x] `alloc_kind_for_destination()` honors metadata `sample_kind` (narrow path live)

**Gate:** modular-only oracle fixture decodes identically on narrow path; no regression on wide (`modular_16bit_buffers=false`) codestreams.

### Phase N3 — Inverse transforms

- [x] Add i16 squeeze inverse in `transform/squeeze.c` (scalar `inverse_h_i16` / `inverse_v_i16`; SIMD deferred)
- [x] Dispatch squeeze entry on grid kind (`inverse_squeeze_channel` in `inverse.c`)
- [x] Add `inverse_rct_row_i16` in `transform/inverse.c`; dispatch RCT on kind
- [x] Palette resolve/store via sample ops (delta palette uses `sample_as_i32` / `store_i32` + i16 add)
- [x] `prepare_subimage.c` squeeze/palette prepare inherits grid kind via struct copy in split helpers

**Gate:** `squeeze_edge` and palette-heavy conformance cases pass on narrow path.

### Phase N4 — Group / subimage pipeline

- [x] `group_subimage.c` — tile views inherit `dest->sample_kind` via struct copy in split helpers
- [x] `recursive_image.c` — meta channel grids use `dest->sample_kind`
- [x] `prepare_subimage.c` — squeeze/palette prepare unchanged (views inherit kind)
- [x] `subimage_decode.c` / `group_decode.c` — sample kind via `dest` (no extra wiring)

**Gate:** pass-group and LF-group modular oracle tests match on narrow path. **Passed** (`c_unit_modular_group_e2e`).

### Phase N5 — Render compose

- [x] `modular_sample.c` — blit via `sample_as_i32` (already typed)
- [x] `modular_compose.c` / `render_frame.c` — EC and gmodular blit via `blit_channel_to_plane` / `sample_color_float` (no direct `buf` access; audited)

**Gate:** full conformance (39/39) on narrow-default path, **including all keyframes on animation cases**. **Passed** (incl. `animation_icos4d`, `animation_spline`).

### Phase N6 — VarDCT LF quant

- [x] `lf_group.c` — `lf_coeff_parse` sets `narrow_buffer` from image metadata; sync views use typed base pointer + kind
- [x] `lf_group_sync_lf_quant_views` / `jxl_lf_quant_subgrid_sample` — element-size-aware reads; HF coeff widens at use site
- [x] Audit `vardct/dequant.c` — already uses `sample_as_i32`; `patch_render.c` LF copy uses float planes (no grid access)

**Gate:** VarDCT conformance cases pass on narrow path. **Passed** (39/39 incl. bike, progressive, patches_lossless).

### Phase N7 — Cleanup and hardening

- [x] Enable narrow allocation in `alloc_kind_for_destination()` (`image.c`)
- [x] Add `jxl_modular_grid` typedef alias for `jxl_modular_grid_i32` (`image.h`)
- [x] Unit tests: i16/i32 wrap divergence, squeeze i16 vs i32, `jxl_lf_quant_subgrid_sample`, `JXL_FORCE_WIDE_BUFFERS`
- [x] Wide-buffer regression: `c_conformance_grayscale_force_wide` (env `JXL_FORCE_WIDE_BUFFERS=1`)
- [ ] (Optional v2) i16 SIMD squeeze paths
- [x] Debug: `JXL_FORCE_WIDE_BUFFERS` env forces i32 storage (`jxl_parsed_narrow_modular`)

**Rough scope:** ~2000 LOC touched across ~20 files; highest risk is a missed wrapping site in decode/transforms.

**Non-goals:** dual-width storage in one destination; public API change; JBR i16 modular path (JBR reads `i32` HF coeffs only).

---

## Next steps

1. **Phase 9 — Hardening (in progress):** libFuzzer (`decode_fuzzer`), ASan/UBSan CI (`c-sanitizers.yml`), `bench_decode` tool
2. **`jxl_context` plan:** complete — see [CONTEXT_PLAN.md](CONTEXT_PLAN.md)
