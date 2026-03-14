cmake_minimum_required(VERSION 3.20)

project(mantis_shrimp VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

find_package(PkgConfig REQUIRED)
pkg_check_modules(ZSTD REQUIRED libzstd)

add_library(
  mantis_shrimp_lib
  src/api/api.cpp
  src/core/engine.cpp
  src/archive/tar_writer.cpp
  src/algorithms/zstd_codec.cpp
)

target_include_directories(
  mantis_shrimp_lib
  PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
  PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${ZSTD_INCLUDE_DIRS}
)

target_link_directories(mantis_shrimp_lib PRIVATE ${ZSTD_LIBRARY_DIRS})
target_link_libraries(mantis_shrimp_lib PRIVATE ${ZSTD_LIBRARIES})
target_compile_options(mantis_shrimp_lib PRIVATE ${ZSTD_CFLAGS_OTHER})

add_executable(mantis src/cli/main.cpp)
target_link_libraries(mantis PRIVATE mantis_shrimp_lib)

enable_testing()

add_executable(
  mantis_tests
  tests/test_main.cpp
)

target_include_directories(
  mantis_tests
  PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${ZSTD_INCLUDE_DIRS}
)

target_link_directories(mantis_tests PRIVATE ${ZSTD_LIBRARY_DIRS})
target_link_libraries(mantis_tests PRIVATE mantis_shrimp_lib ${ZSTD_LIBRARIES})
target_compile_options(mantis_tests PRIVATE ${ZSTD_CFLAGS_OTHER})

add_test(NAME mantis_tests COMMAND mantis_tests)
