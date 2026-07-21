# Golden JXL bitstreams for `jpeg_bitstream_parity_test`

These files were captured from the **scalar minimal encoder** (no Highway
dependency) using the same settings as the parity test:

- `JxlEncoderUseContainer(true)`
- Default frame settings (CFL on, EXIF/XMP kept, brob boxes on)
- Efforts 1–10 on `jpeg/smoke.jpg`
- With and without `JxlEncoderStoreJPEGMetadata` (`*_jbrd.jxl`)

The minimal encoder must produce **byte-identical** output for every case.

To regenerate from a built tree:

```bash
cmake --build build-minimal --target jpeg_regenerate_goldens
./build-minimal/lib/jpeg_regenerate_goldens
ctest --test-dir build-minimal -R jpeg_bitstream_parity_test
```

On x86_64 these goldens currently match the upstream SIMD encoder output for
`smoke.jpg`; re-run regeneration after any encoder change that affects the
bitstream.

Progressive JPEG inputs use a separate golden set under `progressive/`
(`jpeg_progressive_parity_test` / `jpeg_regenerate_progressive_goldens`).
