# Progressive JPEG fixtures for encoder tests

Generated with `tools/gen_progressive_fixtures.c` (needs libjpeg) via
`jpeg_simple_progression`:

| File | Content |
|------|---------|
| `progressive_smoke.jpg` | 8×8 grayscale |
| `progressive_gray.jpg` | 16×16 grayscale |
| `progressive_rgb444.jpg` | 16×16 RGB / 4:4:4 |
| `progressive_yuv420.jpg` | 16×16 RGB input → 4:2:0 |

All are `SOF2` with successive-approximation refine scans (`Ah != 0`).

Determinism: `jpeg_progressive_test`  
Bitstream goldens: `testdata/jxl/progressive/` via
`jpeg_progressive_parity_test` / `jpeg_regenerate_progressive_goldens`.
