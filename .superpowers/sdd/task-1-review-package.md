# Task 1 source-only review package

No Git base/head exists because Git use is prohibited for this source-only run. All files below are new in Task 1. This package is the complete task change.

## CMakeLists.txt

```cmake
cmake_minimum_required(VERSION 3.20)
project(convert_to_biallelic VERSION 0.1.0 LANGUAGES CXX)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
find_package(Threads REQUIRED)
find_package(PkgConfig REQUIRED)
pkg_check_modules(HTSLIB REQUIRED IMPORTED_TARGET htslib>=1.17)
add_library(ctb_core STATIC
  src/annotation_index.cpp src/cli.cpp src/converter.cpp
  src/memory_budget.cpp src/output_transaction.cpp src/pipeline.cpp
  src/progress.cpp src/vcf_io.cpp)
target_include_directories(ctb_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(ctb_core PUBLIC PkgConfig::HTSLIB Threads::Threads)
add_executable(convert-to-biallelic src/main.cpp)
target_link_libraries(convert-to-biallelic PRIVATE ctb_core)
```

## include/convert_to_biallelic/types.hpp

```cpp
#pragma once
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>
namespace ctb {
enum class OutputFormat { vcf, vcf_gz };
struct ThreadAllocation {
    std::size_t conversion_workers = 1;
    std::size_t input_io_workers = 0;
    std::size_t output_io_workers = 0;
};
struct PipelineStats {
    std::uint64_t input_records = 0;
    std::uint64_t output_records = 0;
    std::uint64_t output_bytes = 0;
    std::uint64_t peak_tracked_bytes = 0;
};
struct RawWorkChunk {
    std::uint64_t sequence = 0;
    std::uint64_t first_line_number = 0;
    std::vector<std::string> records;
};
struct RawResultChunk {
    std::uint64_t sequence = 0;
    std::string bytes;
    std::uint64_t input_records = 0;
    std::uint64_t output_records = 0;
};
}
```

## README.md

````md
# convert_to_biallelic

`convert_to_biallelic` is a planned C++17/HTSlib converter for transforming
multiallelic VCF input into biallelic VCF output using an annotation VCF.

## Intended usage

```text
convert-to-biallelic \
  --variants annotation.vcf.gz \
  --input multiallelic.vcf.gz \
  --output biallelic.vcf.gz \
  --threads 8 \
  --memory-limit 2GiB
```

This source tree is authored only; its build, compatibility, and performance
properties remain unverified. See [UNVERIFIED.md](UNVERIFIED.md) for the
explicit verification boundary.
````

## UNVERIFIED.md

```md
# Unverified status

This source has not been compiled, linked, or executed. It has not been tested
against the Python implementation, memory-profiled, or benchmarked. It has not
been verified on Windows or Linux.

The CMake configuration and future HTSlib integration are source descriptions
only. Buildability, compatibility, correctness, memory behavior, and
performance must be verified in a suitable environment before use.
```
