#!/bin/bash -eu

SRC_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"

$CXX $CXXFLAGS -std=c++17 -Iinclude -fsanitize=fuzzer-no-link -c \
  "$SRC_DIR"/src/*.cpp -I"$SRC_DIR/include"

ar rcs "$WORK/libtelemux.a" *.o

for fuzzer in transform_vm_fuzzer frame_fuzzer section_fuzzer session_fuzzer plane_pipeline_fuzzer; do
  $CXX $CXXFLAGS -std=c++17 -I"$SRC_DIR/include" -fsanitize=fuzzer \
    "$SRC_DIR/fuzz/$fuzzer.cc" "$WORK/libtelemux.a" \
    -o "$OUT/$fuzzer"

  seed_dir="$SRC_DIR/fuzz/corpus/$fuzzer"
  if [ -d "$seed_dir" ]; then
    zip -j "$OUT/${fuzzer}_seed_corpus.zip" "$seed_dir"/* || true
  fi
done
