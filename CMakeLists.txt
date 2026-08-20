cmake_minimum_required(VERSION 3.20)

project(convert_to_biallelic VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

find_package(Threads REQUIRED)
find_package(PkgConfig REQUIRED)

set(HTSLIB_ROOT "/qnap-wlee2/wanpingleelab/chengp/tools/htslib-1.16"
    CACHE PATH "HTSlib installation prefix")
set(ENV{PKG_CONFIG_PATH}
    "${HTSLIB_ROOT}/lib/pkgconfig:$ENV{PKG_CONFIG_PATH}")
pkg_check_modules(HTSLIB REQUIRED IMPORTED_TARGET htslib>=1.16)

set_target_properties(PkgConfig::HTSLIB PROPERTIES
  INTERFACE_INCLUDE_DIRECTORIES "${HTSLIB_ROOT}/include"
  INTERFACE_LINK_DIRECTORIES "${HTSLIB_ROOT}/lib"
)

add_library(ctb_core STATIC
  src/annotation_index.cpp
  src/cli.cpp
  src/converter.cpp
  src/memory_budget.cpp
  src/output_transaction.cpp
  src/pipeline.cpp
  src/progress.cpp
  src/vcf_io.cpp
)

target_include_directories(ctb_core PUBLIC
  ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_link_libraries(ctb_core PUBLIC
  PkgConfig::HTSLIB
  Threads::Threads
)

add_executable(convert-to-biallelic src/main.cpp)
target_link_libraries(convert-to-biallelic PRIVATE ctb_core)

enable_testing()
add_test(NAME python_parity
  COMMAND ${CMAKE_CURRENT_SOURCE_DIR}/tests/python_parity.sh)
