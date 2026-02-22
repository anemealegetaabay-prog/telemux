# telemux

telemux is a small C++ library for decoding a custom binary telemetry/
sensor-network protocol. A single physical byte stream multiplexes many
logical sessions; a session's messages may be fragmented across several
physical frames and are reassembled on receipt. Decoded samples can
optionally run through a small post-processing pipeline (filtering,
resampling, channel merging) before being handed to a consumer.

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

## Fuzzing

```
mkdir build_fuzz && cd build_fuzz
cmake -DBUILD_FUZZERS=ON -DENABLE_SANITIZERS=ON ..
make -j
./fuzz/frame_fuzzer fuzz/corpus/frame_fuzzer
```

See `docs/wire_format.md` for the on-wire framing and `docs/opcode_vm.md`
for the transform pipeline's instruction set.

## CLI

```
telemux_cli decode <file>
telemux_cli stats
telemux_cli query "channel == 3 and value > 100"
```
