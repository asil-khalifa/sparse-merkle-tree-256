cmake_minimum_required(VERSION 3.14)
project(smt256 LANGUAGES CXX
        DESCRIPTION "Optimized 256-level Sparse Merkle Tree with compact non-membership proofs")

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

option(SMT_NATIVE "Compile with -march=native" OFF)

add_library(smt STATIC
  src/sha256.cpp
  src/hash.cpp
  src/smt.cpp
  src/proof.cpp
  src/json.cpp
  src/db_file.cpp)
target_include_directories(smt PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_compile_options(smt PRIVATE -Wall -Wextra -Wpedantic)
if(SMT_NATIVE)
  target_compile_options(smt PUBLIC -march=native)
endif()

add_executable(smt_cli src/cli/main.cpp)
set_target_properties(smt_cli PROPERTIES OUTPUT_NAME smt)
target_link_libraries(smt_cli PRIVATE smt)
target_compile_options(smt_cli PRIVATE -Wall -Wextra)

add_executable(smt_tests
  tests/test_main.cpp
  tests/test_sha256.cpp
  tests/test_smt.cpp
  tests/test_proof.cpp
  tests/test_db_file.cpp)
target_link_libraries(smt_tests PRIVATE smt)
target_compile_options(smt_tests PRIVATE -Wall -Wextra)

add_executable(smt_bench bench/bench.cpp)
target_link_libraries(smt_bench PRIVATE smt)
target_compile_options(smt_bench PRIVATE -Wall -Wextra)

enable_testing()
add_test(NAME unit_tests COMMAND smt_tests)
