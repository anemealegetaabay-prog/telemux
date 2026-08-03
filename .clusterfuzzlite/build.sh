#!/bin/bash
# ClusterFuzzLite / Fenrir build script for telemux.
# Environment: $SRC, $OUT, $CXX, $CXXFLAGS, $LIB_FUZZING_ENGINE.
# Fenrir may invoke this as .clusterfuzzlite/build.sh or copy it to repo-root
# build.sh — resolve REPO_ROOT for both layouts.
set -euo pipefail

: "${OUT:?OUT must be set}"
: "${CXX:?CXX must be set}"

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
if [[ "$SCRIPT_DIR" == */.clusterfuzzlite ]]; then
  REPO_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"
elif [ -n "${SRC:-}" ] && [ -d "${SRC}/src" ]; then
  REPO_ROOT="$SRC"
elif [ -d "$SCRIPT_DIR/src" ]; then
  REPO_ROOT="$SCRIPT_DIR"
else
  echo "telemux build.sh: cannot locate sources (SCRIPT_DIR=$SCRIPT_DIR SRC=${SRC:-})" >&2
  exit 1
fi
cd "$REPO_ROOT"

echo "=== telemux ClusterFuzzLite build ==="
echo "REPO_ROOT=$REPO_ROOT OUT=$OUT"

CXXFLAGS="${CXXFLAGS:-} -std=c++17 -I${REPO_ROOT}/include -fno-omit-frame-pointer"
OBJ_DIR="$OUT/telemux_objs"
mkdir -p "$OBJ_DIR"

shopt -s nullglob
SRCS=(src/*.cpp)
if [ ${#SRCS[@]} -eq 0 ]; then
  echo "telemux build.sh: no sources under $REPO_ROOT/src" >&2
  exit 1
fi

OBJS=()
for src in "${SRCS[@]}"; do
  obj="$OBJ_DIR/$(basename "$src" .cpp).o"
  echo "  Compiling $src"
  $CXX $CXXFLAGS -c "$src" -o "$obj"
  OBJS+=("$obj")
done

HARNESSES=(
  transform_vm_fuzzer
  frame_fuzzer
  section_fuzzer
  session_fuzzer
  plane_pipeline_fuzzer
  query_fuzzer
)

for harness in "${HARNESSES[@]}"; do
  echo "  Linking $harness"
  $CXX $CXXFLAGS \
    "fuzz/${harness}.cc" \
    "${OBJS[@]}" \
    ${LIB_FUZZING_ENGINE:--fsanitize=fuzzer} \
    -o "$OUT/$harness"

  corpus_dir="fuzz/corpus/$harness"
  if [ -d "$corpus_dir" ] && [ -n "$(ls -A "$corpus_dir" 2>/dev/null || true)" ]; then
    echo "  Zipping corpus for $harness"
    zip -j "$OUT/${harness}_seed_corpus.zip" "$corpus_dir"/*
  fi
done

rm -rf "$OBJ_DIR"

echo "=== telemux build complete ==="
ls -lh "$OUT/"
