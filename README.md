# telemux

telemux is a small C++ library for decoding a custom binary telemetry/
sensor-network protocol. A single physical byte stream multiplexes many
logical sessions; a session's messages may be fragmented across several
physical frames and are reassembled on receipt. Decoded samples can
optionally run through a small post-processing pipeline (filtering,
resampling, channel merging) before being handed to a consumer.

## Features

- **Wire protocol:** frame parsing, per-session reassembly of fragmented
  messages, nested section parsing (`GRUP`/`CHAN`/`SAMP`/`PLAN`) with a depth
  limit, and CRC-32 checksums.
- **Sessions:** session id allocation and lifecycle, plus duplicate detection
  for late retransmits after a session closes.
- **Transform VM:** a small instruction set over sample registers (filter,
  resample, channel merge, snapshot/rollback, peek, reserve). See
  [docs/opcode_vm.md](docs/opcode_vm.md).
- **Query language:** filters such as `channel == 3 and value > 100` or
  `sample[2] > 40`.
- **Planar data:** plane layout normalization, resampling and channel merging.
- **Storage and export:** a `.tlmx` session container (manifest, paging,
  merge), delta + RLE sample encoding, and CSV / JSON Lines export.
- **Analysis:** calibration curves, rolling-window statistics with threshold
  alerts, clock offset/drift estimation, unit conversion, session digests,
  an event trace log and metrics export.
- **Testing:** 384 unit tests, six libFuzzer harnesses and a ClusterFuzzLite
  build script.

## Building

```
mkdir build && cd build
cmake -DBUILD_TESTS=ON ..
make -j
./tests/telemux_tests
```

Enable sanitizers during development:

```
cmake -DBUILD_TESTS=ON -DENABLE_SANITIZERS=ON ..
```

## Example

Real output from the CLI (the binary is `build/telemux_cli`):

```console
$ telemux_cli decode examples/sample.tlmx
decoded 24 bytes
$ telemux_cli query "channel == 3 and value > 100"
matches: true
```

## CLI

```
telemux_cli decode <file>
telemux_cli inspect <file.tlmx>
telemux_cli stats
telemux_cli query "channel == 3 and value > 100"
```

`decode` reads a raw frame stream such as `examples/sample.tlmx`. `inspect`
summarizes a session container written by `TlmxWriter`; the example file is a
raw stream, not a container, so `inspect` rejects it with `ContainerBadMagic`.

## Fuzzing

There are six libFuzzer harnesses in `fuzz/`, each with a seed corpus in
`fuzz/corpus/<harness>/`: `frame_fuzzer`, `section_fuzzer`, `session_fuzzer`,
`transform_vm_fuzzer`, `plane_pipeline_fuzzer` and `query_fuzzer`.

```
mkdir build_fuzz && cd build_fuzz
cmake -DBUILD_FUZZERS=ON -DENABLE_SANITIZERS=ON ..
make -j
mkdir -p corpus/frame_fuzzer
./fuzz/frame_fuzzer -max_total_time=60 corpus/frame_fuzzer ../fuzz/corpus/frame_fuzzer
```

New inputs go to `build_fuzz/corpus/`, and the checked-in seeds are only read.
The fuzzers need clang with libFuzzer. Apple's Xcode clang does not include
it, so on macOS add `-DCMAKE_CXX_COMPILER=$(brew --prefix llvm)/bin/clang++`.
`.clusterfuzzlite/build.sh` builds all six harnesses and their seed corpora
for ClusterFuzzLite.

See `docs/wire_format.md` for the on-wire framing and `docs/opcode_vm.md`
for the transform pipeline's instruction set.

## License

MIT, see [LICENSE](LICENSE).
