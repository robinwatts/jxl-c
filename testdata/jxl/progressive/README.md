# Progressive JPEG→JXL golden bitstreams

Goldens for `jpeg_progressive_parity_test`, captured from the scalar minimal
encoder:

- Inputs: `testdata/jpeg/progressive_{smoke,gray,rgb444,yuv420}.jpg`
- `JxlEncoderUseContainer(true)`
- Efforts 1–10, with and without `JxlEncoderStoreJPEGMetadata`
- Naming: `{stem}_e{NN}.jxl` / `{stem}_e{NN}_jbrd.jxl`

Regenerate:

```bash
cmake --build build-minimal --target jpeg_regenerate_progressive_goldens
./build-minimal/lib/jpeg_regenerate_progressive_goldens
./build-minimal/lib/jpeg_progressive_parity_test
```
