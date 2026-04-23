cmake_minimum_required(VERSION 3.20)

# Mantis Shrimp - Universal Compression Utility
# Current Status: Early Development (v0.3.x)
# Not production-ready. API and CLI subject to change.
# Stability target: v1.0

project(mantis_shrimp VERSION 0.3.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
include(GNUInstallDirs)

find_package(PkgConfig REQUIRED)
find_package(ZLIB REQUIRED)
pkg_check_modules(ZSTD REQUIRED libzstd)
pkg_check_modules(BROTLI REQUIRED libbrotlienc libbrotlidec)
pkg_check_modules(LZ4 REQUIRED liblz4)
pkg_check_modules(LZMA REQUIRED liblzma)
pkg_check_modules(LIBZIP REQUIRED libzip)

add_library(
  mantis_shrimp_lib
  src/api/api.cpp
  src/core/engine.cpp
  src/core/smart_engine.cpp
  src/archive/tar_writer.cpp
  src/algorithms/zstd_codec.cpp
  src/algorithms/gzip_codec.cpp
  src/codecs/codec_registry.cpp
  src/codecs/zstd_codec_v2.cpp
  src/codecs/gzip_codec_v2.cpp
  src/codecs/brotli_codec.cpp
  src/codecs/lz4_codec.cpp
  src/codecs/xz_codec.cpp
  src/codecs/zip_codec.cpp
)

target_include_directories(
  mantis_shrimp_lib
  PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
  PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${ZSTD_INCLUDE_DIRS}
    ${BROTLI_INCLUDE_DIRS}
    ${LZ4_INCLUDE_DIRS}
    ${LZMA_INCLUDE_DIRS}
    ${LIBZIP_INCLUDE_DIRS}
)

target_link_directories(
  mantis_shrimp_lib
  PRIVATE
    ${ZSTD_LIBRARY_DIRS}
    ${BROTLI_LIBRARY_DIRS}
    ${LZ4_LIBRARY_DIRS}
    ${LZMA_LIBRARY_DIRS}
    ${LIBZIP_LIBRARY_DIRS}
)
target_link_libraries(
  mantis_shrimp_lib
  PRIVATE
    ${ZSTD_LIBRARIES}
    ${BROTLI_LIBRARIES}
    ${LZ4_LIBRARIES}
    ${LZMA_LIBRARIES}
    ${LIBZIP_LIBRARIES}
    ZLIB::ZLIB
)
target_compile_options(
  mantis_shrimp_lib
  PRIVATE
    ${ZSTD_CFLAGS_OTHER}
    ${BROTLI_CFLAGS_OTHER}
    ${LZ4_CFLAGS_OTHER}
    ${LZMA_CFLAGS_OTHER}
)

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
    ${BROTLI_INCLUDE_DIRS}
    ${LZ4_INCLUDE_DIRS}
    ${LZMA_INCLUDE_DIRS}
    ${LIBZIP_INCLUDE_DIRS}
)

target_link_directories(
  mantis_tests
  PRIVATE
    ${ZSTD_LIBRARY_DIRS}
    ${BROTLI_LIBRARY_DIRS}
    ${LZ4_LIBRARY_DIRS}
    ${LZMA_LIBRARY_DIRS}
    ${LIBZIP_LIBRARY_DIRS}
)

target_link_libraries(
  mantis_tests
  PRIVATE
    mantis_shrimp_lib
    ${ZSTD_LIBRARIES}
    ${BROTLI_LIBRARIES}
    ${LZ4_LIBRARIES}
    ${LZMA_LIBRARIES}
    ${LIBZIP_LIBRARIES}
)

target_compile_options(
  mantis_tests
  PRIVATE
    ${ZSTD_CFLAGS_OTHER}
    ${BROTLI_CFLAGS_OTHER}
    ${LZ4_CFLAGS_OTHER}
    ${LZMA_CFLAGS_OTHER}
)

add_test(NAME mantis_tests COMMAND mantis_tests)

install(
  TARGETS mantis mantis_shrimp_lib
  EXPORT mantis_shrimp_targets
  RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
  LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
  ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
)

install(
  DIRECTORY include/
  DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
)

install(
  FILES README.md LICENSE CHANGELOG.md VERSION
  DESTINATION ${CMAKE_INSTALL_DATADIR}/mantis_shrimp
)

install(
  EXPORT mantis_shrimp_targets
  FILE mantis_shrimp_targets.cmake
  NAMESPACE mantis_shrimp::
  DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/mantis_shrimp
)

set(CPACK_PACKAGE_NAME "mantis-shrimp")
set(CPACK_PACKAGE_VENDOR "mantis-shrimp")
set(CPACK_PACKAGE_CONTACT "Sampad6701")
set(CPACK_PACKAGE_VERSION ${PROJECT_VERSION})
set(CPACK_PACKAGE_FILE_NAME "${CPACK_PACKAGE_NAME}-${CPACK_PACKAGE_VERSION}-${CMAKE_SYSTEM_NAME}-${CMAKE_SYSTEM_PROCESSOR}")
set(CPACK_RESOURCE_FILE_LICENSE "${CMAKE_CURRENT_SOURCE_DIR}/LICENSE")
set(CPACK_GENERATOR "TGZ;ZIP")
include(CPack)
