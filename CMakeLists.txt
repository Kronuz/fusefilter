cmake_minimum_required(VERSION 3.14)
project(fusefilter LANGUAGES CXX)

# Header-only: fusefilter.hh is the whole implementation.
add_library(fusefilter INTERFACE)
target_include_directories(fusefilter INTERFACE "${CMAKE_CURRENT_SOURCE_DIR}")
target_compile_features(fusefilter INTERFACE cxx_std_20)
add_library(fusefilter::fusefilter ALIAS fusefilter)

if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
  enable_testing()
  add_executable(fusefilter_test test/test.cc)
  target_link_libraries(fusefilter_test PRIVATE fusefilter)
  add_test(NAME fusefilter COMMAND fusefilter_test)
  add_executable(fusefilter_bench bench/bench.cc)
  target_link_libraries(fusefilter_bench PRIVATE fusefilter)
  # fusefilter_bench also times the reference binary fuse filter (FastFilter/xor_singleheader,
  # fetched at a pinned commit) and boost::bloom::filter (found, Boost 1.89 or later); neither
  # is a dependency of the fusefilter target. Needs CMake 3.18.
  option(FUSEFILTER_BENCH_COMPARATORS "Build fusefilter_bench with the reference filter and boost::bloom" OFF)
  if(FUSEFILTER_BENCH_COMPARATORS)
    include(FetchContent)
    find_package(Boost 1.89 REQUIRED CONFIG)
    FetchContent_Declare(xor_singleheader
      GIT_REPOSITORY https://github.com/FastFilter/xor_singleheader.git
      GIT_TAG 5426b7d84994ecf60e596fde8ae70bc1ae7a9720
      SOURCE_SUBDIR nonexistent)
    FetchContent_MakeAvailable(xor_singleheader)
    target_include_directories(fusefilter_bench PRIVATE "${xor_singleheader_SOURCE_DIR}/include")
    target_link_libraries(fusefilter_bench PRIVATE Boost::headers)
    target_compile_definitions(fusefilter_bench PRIVATE FUSEFILTER_BENCH_REFERENCE FUSEFILTER_BENCH_BOOST)
  endif()
  option(FUSEFILTER_FUZZ "Build the libFuzzer target" OFF)
  if(FUSEFILTER_FUZZ)
    add_executable(fusefilter_fuzz fuzz/fuzz_deserialize.cc)
    target_link_libraries(fusefilter_fuzz PRIVATE fusefilter)
    target_compile_options(fusefilter_fuzz PRIVATE -fsanitize=fuzzer,address,undefined -fno-sanitize-recover=all)
    target_link_options(fusefilter_fuzz PRIVATE -fsanitize=fuzzer,address,undefined)
  endif()
endif()
