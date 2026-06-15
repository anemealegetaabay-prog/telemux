cmake_minimum_required(VERSION 3.16)
project(telemux CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

option(BUILD_TESTS "Build unit tests" ON)
option(BUILD_FUZZERS "Build libFuzzer harnesses" OFF)
option(ENABLE_SANITIZERS "Build with ASan/UBSan" OFF)

if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE RelWithDebInfo)
endif()

if(ENABLE_SANITIZERS)
  add_compile_options(-fsanitize=address,undefined -fno-omit-frame-pointer -g -O1)
  add_link_options(-fsanitize=address,undefined)
endif()

if(BUILD_FUZZERS)
  add_compile_options(-fsanitize=fuzzer-no-link)
endif()

set(TELEMUX_SOURCES
  src/errors.cpp
  src/byte_cursor.cpp
  src/config.cpp
  src/logging.cpp
  src/checksum.cpp
  src/sample_arena.cpp
  src/register_file.cpp
  src/undo_history.cpp
  src/peek_cache.cpp
  src/transform_ops_merge.cpp
  src/transform_ops_filter.cpp
  src/transform_vm.cpp
  src/ring_buffer.cpp
  src/recv_arena.cpp
  src/reassembly.cpp
  src/frame_parser.cpp
  src/metrics.cpp
  src/stats_export.cpp
  src/section_parser.cpp
  src/session_manager.cpp
  src/view_session.cpp
  src/dedup_cache.cpp
  src/idle_sweep.cpp
  src/query_lexer.cpp
  src/query_parser.cpp
  src/query_eval.cpp
  src/decode.cpp
  src/plane_pool.cpp
  src/plane_normalize.cpp
  src/plane_export.cpp
  src/transform_ops_plane.cpp
  src/plane_pipeline.cpp
  src/codec_rle.cpp
  src/serializer.cpp
  src/calibration.cpp
  src/tlmx_file.cpp
  src/window_stats.cpp
  src/clock_sync.cpp
  src/device_registry.cpp
  src/record_export.cpp
  src/event_trace.cpp
)

add_library(telemux STATIC ${TELEMUX_SOURCES})
target_include_directories(telemux PUBLIC include)

add_executable(telemux_cli tools/telemux_cli.cpp)
target_link_libraries(telemux_cli PRIVATE telemux)

if(BUILD_TESTS)
  enable_testing()
  add_subdirectory(tests)
endif()

if(BUILD_FUZZERS)
  add_subdirectory(fuzz)
endif()
