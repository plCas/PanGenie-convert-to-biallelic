# C++ Multithreaded VCF Converter Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Build a Windows/Linux C++17 replacement for `convert-to-biallelic.py` that uses HTSlib, produces Python-compatible `.vcf` or `.vcf.gz`, converts records in parallel without reordering them, targets 2 GiB total memory, and reports progress to stdout.

**Architecture:** Stream annotation data into one immutable index, then stream the input through a bounded reader/worker/ordered-writer pipeline. Conversion workers operate only on text records and shared read-only annotation state; HTSlib supplies line-oriented input and plain/BGZF output, while a process-wide budget applies backpressure to all queued buffers.

**Tech Stack:** C++17, CMake 3.20+, HTSlib 1.17+, CTest, Python 3 differential-test harness, MSYS2 UCRT64/MinGW on Windows, pkg-config on Linux.

## Global Constraints

- Preserve byte-identical uncompressed output relative to `convert-to-biallelic.py` for supported newline-terminated VCF inputs.
- Support `.vcf` and `.vcf.gz` input and output; `.vcf.gz` output must be BGZF.
- Keep VCF output in the explicit `--output` file, progress on stdout, and diagnostics on stderr.
- Default `--memory-limit` to a process-wide soft target of 2 GiB, not 2 GiB per thread.
- Never pre-count input records and never load the multiallelic input VCF in full.
- Preserve deterministic input and expansion order at every thread count.
- Use C++17 and HTSlib 1.17 or newer.
- On Windows, build with MSYS2 UCRT64 and `mingw-w64-ucrt-x86_64-htslib`; the current vcpkg HTSlib port excludes Windows.
- On Linux, discover HTSlib through pkg-config.
- Keep `convert-to-biallelic.py` unchanged as the differential oracle.
- Execution prerequisite: install Git or place it on PATH before implementation; do not skip the commit checkpoints below.

## File Map

- `CMakeLists.txt`: root build, HTSlib/Threads discovery, executable, tests.
- `.github/workflows/ci.yml`: Linux and MSYS2 UCRT64 build/test matrix.
- `include/convert_to_biallelic/cli.hpp`, `src/cli.cpp`: configuration and argument validation.
- `include/convert_to_biallelic/vcf_io.hpp`, `src/vcf_io.cpp`: HTSlib line input and plain/BGZF output.
- `include/convert_to_biallelic/annotation_index.hpp`, `src/annotation_index.cpp`: immutable chromosome/ID lookup.
- `include/convert_to_biallelic/converter.hpp`, `src/converter.cpp`: pure header and record conversion.
- `include/convert_to_biallelic/memory_budget.hpp`, `src/memory_budget.cpp`: process-wide tracked-byte permits.
- `include/convert_to_biallelic/bounded_queue.hpp`: cancellation-aware bounded queue template.
- `include/convert_to_biallelic/pipeline.hpp`, `src/pipeline.cpp`: chunk creation, workers, reordering, cancellation.
- `include/convert_to_biallelic/progress.hpp`, `src/progress.cpp`: atomic counters and stdout reporter.
- `include/convert_to_biallelic/output_transaction.hpp`, `src/output_transaction.cpp`: same-directory temporary output and final rename.
- `src/main.cpp`: lifecycle orchestration and exit codes only.
- `tests/test_support.hpp`: minimal assertion helpers.
- `tests/unit/*.cpp`: focused C++ unit tests.
- `tests/fixtures/*`: small text fixtures generated into VCF and VCF.GZ forms.
- `tests/differential_test.py`: Python-versus-C++ byte comparison.
- `tests/integration_test.py`: formats, progress, failures, and determinism.
- `benchmarks/generate_fixture.py`, `benchmarks/run_benchmark.py`: reproducible scaling benchmark.
- `docs/build-linux.md`, `docs/build-windows.md`: dependency and build commands.

---

### Task 1: Establish the Cross-Platform Build and Test Harness

**Files:**
- Create: `CMakeLists.txt`
- Create: `src/main.cpp`
- Create: `tests/CMakeLists.txt`
- Create: `tests/smoke_test.py`

**Interfaces:**
- Consumes: HTSlib exposed as `PkgConfig::HTSLIB` and platform threads as `Threads::Threads`.
- Produces: executable target `convert-to-biallelic`; CTest test `smoke-version`.

- [ ] **Step 1: Write the failing smoke test**

```python
# tests/smoke_test.py
import subprocess
import sys

exe = sys.argv[1]
result = subprocess.run([exe, "--version"], text=True, capture_output=True)
assert result.returncode == 0, result.stderr
assert result.stdout == "convert-to-biallelic 0.1.0\n"
assert result.stderr == ""
```

- [ ] **Step 2: Add the initial CMake configuration without an executable implementation**

```cmake
# CMakeLists.txt
cmake_minimum_required(VERSION 3.20)
project(convert_to_biallelic VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

find_package(Threads REQUIRED)
find_package(PkgConfig REQUIRED)
pkg_check_modules(HTSLIB REQUIRED IMPORTED_TARGET htslib>=1.17)
find_package(Python3 REQUIRED COMPONENTS Interpreter)

enable_testing()
add_subdirectory(tests)
```

```cmake
# tests/CMakeLists.txt
add_test(
  NAME smoke-version
  COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/smoke_test.py
          $<TARGET_FILE:convert-to-biallelic>
)
```

- [ ] **Step 3: Configure and verify the missing-target failure**

Run on Linux or MSYS2 UCRT64:

```bash
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Debug
```

Expected: configuration fails because `convert-to-biallelic` is referenced but not defined.

- [ ] **Step 4: Add the minimal executable**

```cpp
// src/main.cpp
#include <iostream>
#include <string_view>

int main(int argc, char** argv) {
    if (argc == 2 && std::string_view(argv[1]) == "--version") {
        std::cout << "convert-to-biallelic 0.1.0\n";
        return 0;
    }
    std::cerr << "usage: convert-to-biallelic --help\n";
    return 2;
}
```

Append before `enable_testing()` in `CMakeLists.txt`:

```cmake
add_executable(convert-to-biallelic src/main.cpp)
target_link_libraries(convert-to-biallelic PRIVATE PkgConfig::HTSLIB Threads::Threads)
target_compile_options(convert-to-biallelic PRIVATE
  $<$<CXX_COMPILER_ID:GNU,Clang>:-Wall -Wextra -Wpedantic -Werror>
  $<$<CXX_COMPILER_ID:MSVC>:/W4 /WX>
)
```

- [ ] **Step 5: Build and run the smoke test**

```bash
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Debug
cmake --build build
ctest --test-dir build --output-on-failure -R smoke-version
```

Expected: `100% tests passed, 0 tests failed`.

- [ ] **Step 6: Commit the build bootstrap**

```bash
git add CMakeLists.txt src/main.cpp tests/CMakeLists.txt tests/smoke_test.py
git commit -m "build: add HTSlib C++ project bootstrap"
```

---

### Task 2: Implement CLI Parsing and Thread Allocation

**Files:**
- Create: `include/convert_to_biallelic/cli.hpp`
- Create: `src/cli.cpp`
- Create: `tests/test_support.hpp`
- Create: `tests/unit/test_cli.cpp`
- Modify: `CMakeLists.txt`
- Modify: `tests/CMakeLists.txt`

**Interfaces:**
- Consumes: raw `argc` and `argv` values.
- Produces: `ctb::Config ctb::parse_cli(int, char**)`, `ctb::ThreadAllocation ctb::allocate_threads(const Config&, bool, bool)`, and `ctb::UsageRequested`.

- [ ] **Step 1: Declare the exact configuration interface**

```cpp
// include/convert_to_biallelic/cli.hpp
#pragma once
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <stdexcept>

namespace ctb {
enum class OutputFormat { vcf, vcf_gz };

struct Config {
    std::filesystem::path variants;
    std::filesystem::path input;
    std::filesystem::path output;
    std::size_t threads = 1;
    std::uint64_t memory_limit_bytes = 2ULL * 1024ULL * 1024ULL * 1024ULL;
    std::chrono::milliseconds progress_interval{5000};
    bool quiet = false;
    bool force = false;
    OutputFormat output_format = OutputFormat::vcf;
};

struct ThreadAllocation {
    std::size_t conversion_workers;
    std::size_t input_io_workers;
    std::size_t output_io_workers;
};

class UsageRequested final : public std::exception {
public:
    explicit UsageRequested(bool version) : version_(version) {}
    bool version() const noexcept { return version_; }
    const char* what() const noexcept override { return "usage requested"; }
private:
    bool version_;
};

Config parse_cli(int argc, char** argv);
ThreadAllocation allocate_threads(const Config& config, bool compressed_input,
                                  bool compressed_output);
const char* usage_text();
}
```

- [ ] **Step 2: Write failing CLI tests**

```cpp
// tests/unit/test_cli.cpp
#include "convert_to_biallelic/cli.hpp"
#include "../test_support.hpp"
#include <string>
#include <vector>

static ctb::Config parse(std::vector<std::string> args) {
    std::vector<char*> raw;
    for (auto& arg : args) raw.push_back(arg.data());
    return ctb::parse_cli(static_cast<int>(raw.size()), raw.data());
}

int main() {
    const auto cfg = parse({"tool", "--variants", "a.vcf.gz", "--input", "i.vcf",
                            "--output", "o.vcf.gz", "--threads", "8",
                            "--memory-limit", "2G", "--progress-interval", "3"});
    CHECK(cfg.output_format == ctb::OutputFormat::vcf_gz);
    CHECK(cfg.threads == 8);
    CHECK(cfg.memory_limit_bytes == 2147483648ULL);
    CHECK(cfg.progress_interval.count() == 3000);

    const auto allocation = ctb::allocate_threads(cfg, true, true);
    CHECK(allocation.conversion_workers == 6);
    CHECK(allocation.input_io_workers == 1);
    CHECK(allocation.output_io_workers == 1);

    CHECK_THROWS(parse({"tool", "--variants", "a.vcf.gz", "--input", "i.vcf",
                        "--output", "o.txt"}));
    CHECK_THROWS(parse({"tool", "--variants", "a.vcf.gz", "--input", "i.vcf",
                        "--output", "o.vcf", "--threads", "0"}));
    return 0;
}
```

```cpp
// tests/test_support.hpp
#pragma once
#include <iostream>
#include <stdexcept>

#define CHECK(expr) do { if (!(expr)) { \
    std::cerr << __FILE__ << ':' << __LINE__ << ": CHECK failed: " #expr "\n"; \
    return 1; } } while (false)

#define CHECK_THROWS(expr) do { bool caught_ = false; try { (void)(expr); } \
    catch (const std::exception&) { caught_ = true; } \
    if (!caught_) { std::cerr << __FILE__ << ':' << __LINE__ \
    << ": expected exception: " #expr "\n"; return 1; } } while (false)
```

- [ ] **Step 3: Register and run the failing test**

Replace the Task 1 executable definition in `CMakeLists.txt` with:

```cmake
add_library(ctb_core STATIC src/cli.cpp)
target_include_directories(ctb_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(ctb_core PUBLIC PkgConfig::HTSLIB Threads::Threads)
target_compile_options(ctb_core PRIVATE
  $<$<CXX_COMPILER_ID:GNU,Clang>:-Wall -Wextra -Wpedantic -Werror>
  $<$<CXX_COMPILER_ID:MSVC>:/W4 /WX>
)

add_executable(convert-to-biallelic src/main.cpp)
target_link_libraries(convert-to-biallelic PRIVATE ctb_core)
```

Append to `tests/CMakeLists.txt`:

```cmake
add_executable(test_cli unit/test_cli.cpp)
target_link_libraries(test_cli PRIVATE ctb_core)
add_test(NAME unit-cli COMMAND test_cli)
```

Then run:

```bash
cmake --build build
ctest --test-dir build --output-on-failure -R unit-cli
```

Expected: link failure for `parse_cli` and `allocate_threads`.

- [ ] **Step 4: Implement exact parsing rules**

Implement `src/cli.cpp` with these rules:

```cpp
namespace ctb {
static std::uint64_t parse_size(std::string text);
static std::size_t parse_positive_count(std::string_view option, std::string_view text);
static OutputFormat infer_output_format(const std::filesystem::path& path);

ThreadAllocation allocate_threads(const Config& c, bool in_gz, bool out_gz) {
    std::size_t remaining = c.threads;
    std::size_t out = out_gz && remaining > 1 ? 1 : 0;
    remaining -= out;
    std::size_t in = in_gz && c.threads >= 4 && remaining > 1 ? 1 : 0;
    remaining -= in;
    return {remaining, in, out};
}
}
```

`parse_cli` must reject duplicate options, missing values, `threads < 1`, sizes below 64 MiB, negative progress intervals, unknown extensions without `--output-format`, and missing required paths. `K`, `M`, and `G` are case-insensitive binary suffixes. `--quiet`, `--force`, `--help`, and `--version` take no value.

- [ ] **Step 5: Run CLI tests and the full suite**

```bash
cmake --build build
ctest --test-dir build --output-on-failure
```

Expected: smoke and CLI tests pass.

- [ ] **Step 6: Commit CLI behavior**

```bash
git add CMakeLists.txt tests/CMakeLists.txt include/convert_to_biallelic/cli.hpp src/cli.cpp tests/test_support.hpp tests/unit/test_cli.cpp
git commit -m "feat: add converter command line configuration"
```

---

### Task 3: Add HTSlib Text Input and VCF/BGZF Output

**Files:**
- Create: `include/convert_to_biallelic/vcf_io.hpp`
- Create: `src/vcf_io.cpp`
- Create: `tests/unit/test_vcf_io.cpp`
- Modify: `CMakeLists.txt`
- Modify: `tests/CMakeLists.txt`

**Interfaces:**
- Consumes: filesystem paths, `OutputFormat`, and allocated I/O worker counts.
- Produces: `ctb::InputSource::getline`, `ctb::OutputSink::write`, `flush`, and `close`.

- [ ] **Step 1: Declare RAII I/O interfaces**

```cpp
// include/convert_to_biallelic/vcf_io.hpp
#pragma once
#include "cli.hpp"
#include <filesystem>
#include <memory>
#include <string>
#include <string_view>

namespace ctb {
class InputSource {
public:
    InputSource(const std::filesystem::path& path, std::size_t io_workers);
    ~InputSource();
    InputSource(InputSource&&) noexcept;
    InputSource& operator=(InputSource&&) noexcept;
    bool getline(std::string& line);
    bool compressed() const noexcept;
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

class OutputSink {
public:
    OutputSink(const std::filesystem::path& path, OutputFormat format,
               std::size_t io_workers);
    ~OutputSink();
    OutputSink(OutputSink&&) noexcept;
    OutputSink& operator=(OutputSink&&) noexcept;
    void write(std::string_view bytes);
    void flush();
    void close();
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
}
```

- [ ] **Step 2: Write failing plain/BGZF round-trip tests**

```cpp
// tests/unit/test_vcf_io.cpp
#include "convert_to_biallelic/vcf_io.hpp"
#include "../test_support.hpp"
#include <filesystem>
#include <string>

static int round_trip(const std::filesystem::path& path, ctb::OutputFormat format) {
    {
        ctb::OutputSink out(path, format, 2);
        out.write("##fileformat=VCFv4.2\n#CHROM\tPOS\n1\t10\n");
        out.close();
    }
    ctb::InputSource in(path, 2);
    std::string line;
    CHECK(in.getline(line)); CHECK(line == "##fileformat=VCFv4.2");
    CHECK(in.getline(line)); CHECK(line == "#CHROM\tPOS");
    CHECK(in.getline(line)); CHECK(line == "1\t10");
    CHECK(!in.getline(line));
    std::filesystem::remove(path);
    return 0;
}

int main() {
    CHECK(round_trip("io-test.vcf", ctb::OutputFormat::vcf) == 0);
    CHECK(round_trip("io-test.vcf.gz", ctb::OutputFormat::vcf_gz) == 0);
    return 0;
}
```

- [ ] **Step 3: Run the test and confirm missing symbols**

```bash
cmake --build build
ctest --test-dir build --output-on-failure -R unit-vcf-io
```

Expected: link failure for `InputSource` and `OutputSink`.

- [ ] **Step 4: Implement HTSlib wrappers**

Use `hts_open(path, "r")`, verify `hts_get_format()` reports VCF/text with no compression or gzip/BGZF, call `hts_set_threads` only when `io_workers > 0`, read with `hts_getline`, and close with `hts_close`. Treat `-1` as EOF and values below `-1` as errors.

For plain output use `hopen(path, "wb")`, `hwrite`, `hflush`, and `hclose`. For compressed output use `bgzf_open(path, "w")`, `bgzf_mt` when `io_workers > 0`, `bgzf_write`, `bgzf_flush`, and `bgzf_close`. Loop until all requested bytes are written and throw `std::runtime_error` with the path on every failure. Destructors must not throw; explicit `close()` propagates close errors.

- [ ] **Step 5: Run I/O and full tests**

```bash
cmake --build build
ctest --test-dir build --output-on-failure
```

Expected: all tests pass and both temporary files are removed.

- [ ] **Step 6: Commit HTSlib I/O**

```bash
git add CMakeLists.txt tests/CMakeLists.txt include/convert_to_biallelic/vcf_io.hpp src/vcf_io.cpp tests/unit/test_vcf_io.cpp
git commit -m "feat: add HTSlib VCF and BGZF streams"
```

---

### Task 4: Build the Immutable Annotation Index

**Files:**
- Create: `include/convert_to_biallelic/annotation_index.hpp`
- Create: `src/annotation_index.cpp`
- Create: `tests/unit/test_annotation_index.cpp`
- Modify: `CMakeLists.txt`
- Modify: `tests/CMakeLists.txt`

**Interfaces:**
- Consumes: `InputSource&`, annotation record text, and a memory limit.
- Produces: immutable `AnnotationIndex`, `VariantDefinition`, `load_annotation`, and `estimated_bytes()`.

- [ ] **Step 1: Declare the index API**

```cpp
// include/convert_to_biallelic/annotation_index.hpp
#pragma once
#include "vcf_io.hpp"
#include <cstdint>
#include <string>
#include <string_view>
#include <unordered_map>

namespace ctb {
struct VariantDefinition {
    std::int64_t position;
    std::string ref;
    std::string alt;
};

class AnnotationIndex {
public:
    const VariantDefinition* find(std::string_view chromosome,
                                  std::string_view id) const noexcept;
    std::uint64_t estimated_bytes() const noexcept;
    std::uint64_t variant_count() const noexcept;
private:
    using ById = std::unordered_map<std::string, VariantDefinition>;
    std::unordered_map<std::string, ById> variants_;
    std::uint64_t estimated_bytes_ = 0;
    friend AnnotationIndex load_annotation(InputSource&, std::uint64_t);
};

AnnotationIndex load_annotation(InputSource& source, std::uint64_t memory_limit);
}
```

- [ ] **Step 2: Write failing loader tests**

Create an annotation containing comments, two chromosomes, and a duplicate chromosome/ID whose last definition differs. Assert lookup, last-definition-wins, missing lookup, variant count, and nonzero memory estimate. Add failure cases for fewer than eight columns, missing `ID=`, comma-separated multiple IDs, invalid position, and an estimate exceeding the supplied limit.

Core assertions:

```cpp
const auto* v = index.find("chr1", "v1");
CHECK(v != nullptr);
CHECK(v->position == 12);
CHECK(v->ref == "G");
CHECK(v->alt == "T");
CHECK(index.find("chr2", "missing") == nullptr);
CHECK(index.variant_count() == 2);
CHECK(index.estimated_bytes() > 0);
```

- [ ] **Step 3: Run and verify the failing test**

```bash
cmake --build build
ctest --test-dir build --output-on-failure -R unit-annotation
```

Expected: unresolved `load_annotation` and `AnnotationIndex` methods.

- [ ] **Step 4: Implement streaming parsing and accounting**

Split records on tabs, require at least eight columns, scan semicolon-separated INFO entries for the first exact `ID=` key, require exactly one comma-delimited ID, and parse POS with `std::from_chars` into positive `std::int64_t`. Insert with `variants_[chromosome][id] = definition` so the last entry wins. Recompute the conservative estimate after each insertion using string capacities, `sizeof` node payloads, and bucket arrays; fail before insertion would exceed `memory_limit - memory_limit / 10`.

- [ ] **Step 5: Run annotation and full tests**

```bash
cmake --build build
ctest --test-dir build --output-on-failure
```

Expected: all tests pass.

- [ ] **Step 6: Commit the annotation index**

```bash
git add CMakeLists.txt tests/CMakeLists.txt include/convert_to_biallelic/annotation_index.hpp src/annotation_index.cpp tests/unit/test_annotation_index.cpp
git commit -m "feat: load immutable variant annotation index"
```

---

### Task 5: Implement Python-Compatible Record Conversion

**Files:**
- Create: `include/convert_to_biallelic/converter.hpp`
- Create: `src/converter.cpp`
- Create: `tests/unit/test_converter.cpp`
- Modify: `CMakeLists.txt`
- Modify: `tests/CMakeLists.txt`

**Interfaces:**
- Consumes: one header or data line, immutable `AnnotationIndex`, and source line number.
- Produces: `convert_header` and `convert_record` with complete newline-terminated output text.

- [ ] **Step 1: Declare conversion results**

```cpp
// include/convert_to_biallelic/converter.hpp
#pragma once
#include "annotation_index.hpp"
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

namespace ctb {
struct ConversionResult {
    std::string bytes;
    std::uint64_t output_records = 0;
};

std::optional<std::string> convert_header(std::string_view line);
ConversionResult convert_record(std::string_view line,
                                const AnnotationIndex& annotation,
                                std::uint64_t line_number);
}
```

- [ ] **Step 2: Write focused failing tests**

Test all four filtered header declarations and an unchanged header. Build a tiny annotation and assert exact strings for:

- one known ID;
- multiple IDs sorted by annotation POS;
- repeated IDs deduplicated;
- `MA`/`UK` kept while other INFO is removed;
- `GT` with and without `GQ`;
- `|` normalized to `/`;
- missing alleles;
- a single unknown ID passed through;
- unknown ID inside a composite mapping rejected;
- allele index outside `allele_to_ids` rejected.

Use a full exact assertion such as:

```cpp
const auto result = ctb::convert_record(
    "chr1\t100\t.\tA\tC,G\t.\tPASS\tID=v2,v1;MA=1;DROP=x\tGT:GQ\t1|2:42",
    index, 9);
CHECK(result.bytes ==
      "chr1\t10\tv1\tA\tC\t.\tPASS\tID=v1;MA=1\tGT:GQ\t0/1:42\n"
      "chr1\t20\tv2\tA\tG\t.\tPASS\tID=v2;MA=1\tGT:GQ\t1/0:42\n");
CHECK(result.output_records == 2);
```

- [ ] **Step 3: Run and verify failure**

```bash
cmake --build build
ctest --test-dir build --output-on-failure -R unit-converter
```

Expected: unresolved conversion functions.

- [ ] **Step 4: Implement conversion as a pure function**

Parse the tab fields once, parse INFO once while preserving its encounter order for `MA` and `UK`, parse FORMAT once, and parse each sample only once. Build `allele_to_ids` as REF empty string plus comma-separated `INFO/ID` values. Collect `(position, id)` pairs in a deduplicating set, sort by position and then ID for deterministic ties, and emit lines with tabs and LF. Preserve the original unknown single-ID line exactly except for LF normalization. Every error includes `input line <number>`.

- [ ] **Step 5: Compare the unit cases with the Python oracle manually**

```bash
python convert-to-biallelic.py --help
cmake --build build
ctest --test-dir build --output-on-failure -R unit-converter
```

Expected: Python help exits successfully and converter tests pass.

- [ ] **Step 6: Commit pure conversion**

```bash
git add CMakeLists.txt tests/CMakeLists.txt include/convert_to_biallelic/converter.hpp src/converter.cpp tests/unit/test_converter.cpp
git commit -m "feat: reproduce Python biallelic conversion"
```

---

### Task 6: Prove Single-Thread End-to-End Equivalence

**Files:**
- Create: `tests/fixtures/annotation.vcf`
- Create: `tests/fixtures/input.vcf`
- Create: `tests/make_fixtures.py`
- Create: `tests/differential_test.py`
- Create: `include/convert_to_biallelic/pipeline.hpp`
- Create: `src/pipeline.cpp`
- Modify: `src/main.cpp`
- Modify: `CMakeLists.txt`
- Modify: `tests/CMakeLists.txt`

**Interfaces:**
- Consumes: `Config`, `AnnotationIndex`, `InputSource`, `OutputSink`.
- Produces: `PipelineStats run_single_threaded(...)` and a working executable for `--threads 1`.

- [ ] **Step 1: Create representative text fixtures and gzip generator**

The annotation fixture must include every ID used by the known records and at least one duplicate definition. The input fixture must include filtered headers, retained headers, unknown biallelic passthrough, composite allele IDs, phased/unphased/missing genotypes, GQ present/absent, MA/UK, and one record expanding to multiple lines.

```python
# tests/make_fixtures.py
import gzip
import pathlib
import sys

root = pathlib.Path(sys.argv[1])
source = root / "annotation.vcf"
target = root / "annotation.vcf.gz"
with source.open("rb") as incoming:
    with gzip.GzipFile(filename=str(target), mode="wb", mtime=0) as outgoing:
        outgoing.write(incoming.read())
```

- [ ] **Step 2: Write the failing differential harness**

```python
# tests/differential_test.py
import gzip
import pathlib
import subprocess
import sys
import tempfile

exe, python_script, fixture_dir = map(pathlib.Path, sys.argv[1:4])
with tempfile.TemporaryDirectory() as tmp:
    tmp = pathlib.Path(tmp)
    annotation_gz = tmp / "annotation.vcf.gz"
    with (fixture_dir / "annotation.vcf").open("rb") as src:
        with gzip.GzipFile(filename=str(annotation_gz), mode="wb", mtime=0) as dst:
            dst.write(src.read())
    source_bytes = (fixture_dir / "input.vcf").read_bytes()
    oracle = subprocess.run(
        [sys.executable, str(python_script), str(annotation_gz)],
        input=source_bytes, capture_output=True, check=True).stdout
    output = tmp / "result.vcf"
    run = subprocess.run(
        [str(exe), "--variants", str(annotation_gz), "--input",
         str(fixture_dir / "input.vcf"), "--output", str(output),
         "--threads", "1", "--quiet"], capture_output=True, check=True)
    assert run.stdout == b""
    assert run.stderr == b""
    assert output.read_bytes() == oracle
```

- [ ] **Step 3: Register and run the failing differential test**

```bash
cmake --build build
ctest --test-dir build --output-on-failure -R differential-single
```

Expected: failure because the executable does not yet run conversion.

- [ ] **Step 4: Add the single-thread pipeline interface and implementation**

```cpp
// include/convert_to_biallelic/pipeline.hpp
#pragma once
#include "annotation_index.hpp"
#include "cli.hpp"
#include "vcf_io.hpp"
#include <cstdint>

namespace ctb {
struct PipelineStats {
    std::uint64_t input_records = 0;
    std::uint64_t output_records = 0;
    std::uint64_t output_bytes = 0;
    std::uint64_t peak_tracked_bytes = 0;
};

PipelineStats run_single_threaded(InputSource& input, OutputSink& output,
                                  const AnnotationIndex& annotation);
}
```

`run_single_threaded` reads leading headers, calls `convert_header`, then converts each data record with an increasing physical line number. `main` parses the CLI, loads annotation before opening output, creates input/output objects with zero I/O workers, runs this function, closes output explicitly, and maps usage to exit 0, CLI errors to 2, and processing errors to 1.

- [ ] **Step 5: Run the differential and complete suite**

```bash
cmake --build build
ctest --test-dir build --output-on-failure
```

Expected: Python and C++ plain outputs match byte-for-byte.

- [ ] **Step 6: Commit the first working converter**

```bash
git add CMakeLists.txt tests/CMakeLists.txt src/main.cpp include/convert_to_biallelic/pipeline.hpp src/pipeline.cpp tests/fixtures tests/make_fixtures.py tests/differential_test.py
git commit -m "feat: add single-thread differential converter"
```

---

### Task 7: Add Process-Wide Memory Permits and a Bounded Queue

**Files:**
- Create: `include/convert_to_biallelic/memory_budget.hpp`
- Create: `src/memory_budget.cpp`
- Create: `include/convert_to_biallelic/bounded_queue.hpp`
- Create: `tests/unit/test_memory_budget.cpp`
- Create: `tests/unit/test_bounded_queue.cpp`
- Modify: `CMakeLists.txt`
- Modify: `tests/CMakeLists.txt`

**Interfaces:**
- Consumes: configured memory bytes and queue item byte reservations.
- Produces: movable `MemoryPermit`, `MemoryBudget::acquire`, `cancel`, `peak_bytes`, and `BoundedQueue<T>::push/pop/close/cancel`.

- [ ] **Step 1: Declare memory ownership**

```cpp
// include/convert_to_biallelic/memory_budget.hpp
#pragma once
#include <condition_variable>
#include <cstdint>
#include <mutex>

namespace ctb {
class MemoryBudget;
class MemoryPermit {
public:
    MemoryPermit() = default;
    ~MemoryPermit();
    MemoryPermit(MemoryPermit&&) noexcept;
    MemoryPermit& operator=(MemoryPermit&&) noexcept;
    MemoryPermit(const MemoryPermit&) = delete;
    MemoryPermit& operator=(const MemoryPermit&) = delete;
    std::uint64_t bytes() const noexcept;
    void resize(std::uint64_t bytes, bool allow_one_overage);
private:
    MemoryPermit(MemoryBudget* owner, std::uint64_t bytes, bool overage);
    MemoryBudget* owner_ = nullptr;
    std::uint64_t bytes_ = 0;
    bool overage_ = false;
    friend class MemoryBudget;
};

class MemoryBudget {
public:
    explicit MemoryBudget(std::uint64_t tracked_limit);
    MemoryPermit acquire(std::uint64_t bytes, bool allow_oversized_single);
    void cancel();
    std::uint64_t current_bytes() const;
    std::uint64_t peak_bytes() const;
private:
    void release(std::uint64_t bytes);
    mutable std::mutex mutex_;
    std::condition_variable changed_;
    std::uint64_t limit_;
    std::uint64_t current_ = 0;
    std::uint64_t peak_ = 0;
    bool cancelled_ = false;
    bool overage_active_ = false;
    friend class MemoryPermit;
};
}
```

- [ ] **Step 2: Write blocking, release, cancellation, and queue tests**

Tests must prove a second acquisition blocks until the first permit is destroyed, moving a permit releases exactly once, resizing changes tracked bytes, only one permitted overage can be active, releasing that overage wakes the next waiter, cancellation wakes a blocked acquisition with an exception, FIFO order is preserved, close drains queued items then returns `false`, and cancel wakes both blocked producers and consumers.

- [ ] **Step 3: Run and verify failures**

```bash
cmake --build build
ctest --test-dir build --output-on-failure -R "unit-(memory|queue)"
```

Expected: missing type or link failures.

- [ ] **Step 4: Implement permit accounting and the header-only queue**

Use predicates on every condition-variable wait. `MemoryPermit::~MemoryPermit` calls `owner_->release(bytes_)` only when `owner_ != nullptr`. Normal acquisition/resizing waits for `current + requested_delta <= limit`. When `allow_one_overage` is true, exactly one caller may exceed the limit; it sets `overage_active_`, and its permit clears that flag on shrink or destruction. This guarantees that one worker can finish and unblock the writer instead of all workers deadlocking while growing results. `BoundedQueue<T>` stores `std::deque<T>`, has a fixed item capacity, and uses separate not-empty/not-full condition variables under one mutex. `cancel()` marks cancellation and clears queued items so their permits release immediately.

- [ ] **Step 5: Run concurrency tests repeatedly**

```bash
cmake --build build
ctest --test-dir build --output-on-failure -R "unit-(memory|queue)" --repeat until-fail:50
```

Expected: all 50 repetitions pass without hangs.

- [ ] **Step 6: Commit bounded memory primitives**

```bash
git add CMakeLists.txt tests/CMakeLists.txt include/convert_to_biallelic/memory_budget.hpp src/memory_budget.cpp include/convert_to_biallelic/bounded_queue.hpp tests/unit/test_memory_budget.cpp tests/unit/test_bounded_queue.cpp
git commit -m "feat: add bounded pipeline memory primitives"
```

---

### Task 8: Implement the Ordered Multithreaded Pipeline

**Files:**
- Modify: `include/convert_to_biallelic/pipeline.hpp`
- Modify: `src/pipeline.cpp`
- Create: `tests/unit/test_pipeline.cpp`
- Modify: `src/main.cpp`
- Modify: `CMakeLists.txt`
- Modify: `tests/CMakeLists.txt`

**Interfaces:**
- Consumes: annotation, I/O streams, thread allocation, memory target, and optional test hook.
- Produces: `PipelineStats run_pipeline(...)` with deterministic ordered output and first-error cancellation.

- [ ] **Step 1: Add exact chunk and pipeline types**

```cpp
namespace ctb {
struct WorkChunk {
    std::uint64_t sequence;
    std::uint64_t first_line_number;
    std::vector<std::string> records;
    MemoryPermit permit;
};

struct ResultChunk {
    std::uint64_t sequence;
    std::string bytes;
    std::uint64_t input_records;
    std::uint64_t output_records;
    MemoryPermit permit;
};

struct PipelineOptions {
    std::size_t conversion_workers;
    std::uint64_t memory_limit_bytes;
    std::size_t target_records_per_chunk = 512;
    std::uint64_t target_bytes_per_chunk = 8ULL * 1024ULL * 1024ULL;
};

PipelineStats run_pipeline(InputSource& input, OutputSink& output,
                           const AnnotationIndex& annotation,
                           const PipelineOptions& options);
}
```

- [ ] **Step 2: Write a failing forced-reordering test**

Add an internal test-only worker hook that sleeps on sequence zero and immediately completes later chunks. Feed at least four chunks, assert the observed completion order differs from sequence order, and assert writer output remains byte-identical to single-thread output. Add a worker exception test that returns within five seconds, joins every thread, and reports the first input line error.

- [ ] **Step 3: Run and verify failure**

```bash
cmake --build build
ctest --test-dir build --output-on-failure -R unit-pipeline
```

Expected: failure because `run_pipeline` is not implemented.

- [ ] **Step 4: Implement reader, workers, and ordered writer**

Reader behavior:

1. Convert and write leading headers before submitting data chunks.
2. Accumulate until 512 records or 8 MiB, whichever comes first.
3. Acquire a permit for stored string capacities.
4. Push to a work queue capped at `2 * conversion_workers`.
5. Close the work queue at EOF.

Worker behavior:

1. Pop a work chunk.
2. Convert its records sequentially into one result string, calling `permit.resize(input_capacity + next_output_capacity, true)` before each output-buffer reserve.
3. Release the input record strings, then shrink the permit to the final result capacity before queueing it.
4. Push one result with the same sequence.
5. The last exiting worker closes the result queue.

Writer behavior:

1. Store received results in `std::map<std::uint64_t, ResultChunk>`.
2. Repeatedly write and erase `next_sequence` when present.
3. Throw if the queue closes with a missing sequence.

The first exception is stored in `std::exception_ptr` under a mutex; cancellation closes both queues and cancels the memory budget. Join all threads before rethrowing.

- [ ] **Step 5: Run deterministic and differential tests across thread counts**

Extend `differential_test.py` to run `--threads 1,2,4,8` and compare every output to the same oracle.

```bash
cmake --build build
ctest --test-dir build --output-on-failure -R "unit-pipeline|differential"
ctest --test-dir build --output-on-failure -R unit-pipeline --repeat until-fail:25
```

Expected: every output matches and no repetition hangs.

- [ ] **Step 6: Commit ordered parallel conversion**

```bash
git add CMakeLists.txt tests/CMakeLists.txt include/convert_to_biallelic/pipeline.hpp src/pipeline.cpp src/main.cpp tests/unit/test_pipeline.cpp tests/differential_test.py
git commit -m "feat: add ordered multithreaded conversion pipeline"
```

---

### Task 9: Add Stdout Progress and Final Statistics

**Files:**
- Create: `include/convert_to_biallelic/progress.hpp`
- Create: `src/progress.cpp`
- Create: `tests/unit/test_progress.cpp`
- Modify: `include/convert_to_biallelic/pipeline.hpp`
- Modify: `src/pipeline.cpp`
- Modify: `src/main.cpp`
- Modify: `CMakeLists.txt`
- Modify: `tests/CMakeLists.txt`

**Interfaces:**
- Consumes: atomic pipeline counters, configured interval, quiet flag, and `std::ostream&`.
- Produces: `ProgressCounters`, `ProgressReporter::start/finish`, periodic lines, and one final summary.

- [ ] **Step 1: Declare progress state**

```cpp
// include/convert_to_biallelic/progress.hpp
#pragma once
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <iosfwd>
#include <mutex>
#include <thread>

namespace ctb {
struct PipelineStats;

struct ProgressCounters {
    std::atomic<std::uint64_t> input_records{0};
    std::atomic<std::uint64_t> output_records{0};
    std::atomic<std::uint64_t> tracked_memory{0};
};

class ProgressReporter {
public:
    ProgressReporter(ProgressCounters&, std::chrono::milliseconds,
                     std::ostream&, bool quiet);
    ~ProgressReporter();
    void start();
    void finish(const PipelineStats& stats);
private:
    void run();
    ProgressCounters& counters_;
    std::chrono::milliseconds interval_;
    std::ostream& output_;
    bool quiet_;
    std::atomic<bool> stopping_{false};
    std::mutex mutex_;
    std::condition_variable changed_;
    std::thread thread_;
    std::chrono::steady_clock::time_point started_;
};
}
```

- [ ] **Step 2: Write failing formatting and quiet-mode tests**

Use `std::ostringstream` and a 10 ms interval. Update counters, wait up to 100 ms, finish, and assert output contains `input=`, `output=`, `rate=`, `memory=`, and `Finished:`. Assert quiet mode produces an empty string. Assert the reporter joins on destruction.

- [ ] **Step 3: Run and verify failure**

```bash
cmake --build build
ctest --test-dir build --output-on-failure -R unit-progress
```

Expected: missing progress symbols.

- [ ] **Step 4: Implement the single reporter thread**

Use `steady_clock`, atomic loads with relaxed ordering, `std::condition_variable` for interruptible interval waits, integer MiB formatting, and one `output_ << line << '\n' << std::flush` operation per report. Never write from conversion workers. `main` passes `std::cout`; exceptions and warnings continue to use `std::cerr`.

- [ ] **Step 5: Add progress integration assertions**

Run without `--quiet` and assert stdout matches periodic/final field names while the output VCF still matches the oracle. Run with `--quiet` and assert stdout is empty.

```bash
cmake --build build
ctest --test-dir build --output-on-failure -R "unit-progress|integration-progress|differential"
```

Expected: all tests pass.

- [ ] **Step 6: Commit progress reporting**

```bash
git add CMakeLists.txt tests/CMakeLists.txt include/convert_to_biallelic/progress.hpp src/progress.cpp include/convert_to_biallelic/pipeline.hpp src/pipeline.cpp src/main.cpp tests/unit/test_progress.cpp tests/integration_test.py
git commit -m "feat: report conversion progress on stdout"
```

---

### Task 10: Add Transactional Output and Failure Cleanup

**Files:**
- Create: `include/convert_to_biallelic/output_transaction.hpp`
- Create: `src/output_transaction.cpp`
- Create: `tests/unit/test_output_transaction.cpp`
- Modify: `src/main.cpp`
- Modify: `CMakeLists.txt`
- Modify: `tests/CMakeLists.txt`

**Interfaces:**
- Consumes: final output path and `--force`.
- Produces: unique same-directory temporary path, `commit()`, and destructor cleanup.

- [ ] **Step 1: Declare transactional output**

```cpp
// include/convert_to_biallelic/output_transaction.hpp
#pragma once
#include <filesystem>

namespace ctb {
class OutputTransaction {
public:
    OutputTransaction(std::filesystem::path destination, bool force);
    ~OutputTransaction();
    const std::filesystem::path& temporary_path() const noexcept;
    void commit();
private:
    std::filesystem::path destination_;
    std::filesystem::path temporary_;
    bool force_;
    bool committed_ = false;
};
}
```

- [ ] **Step 2: Write failing safety tests**

Test that construction rejects an existing destination without force, a destroyed uncommitted transaction removes its temporary file, commit renames complete content, force replaces an existing destination on both platforms, and two transactions use different names.

- [ ] **Step 3: Run and verify failure**

```bash
cmake --build build
ctest --test-dir build --output-on-failure -R unit-output-transaction
```

Expected: missing transaction symbols.

- [ ] **Step 4: Implement same-directory finalize behavior**

Create names of the form `.<filename>.ctb.<process-id>.<counter>.tmp` with exclusive creation. On POSIX, finalize with `std::filesystem::rename`; when force replacement requires it, remove only the already-validated exact destination immediately before rename. On Windows, use `MoveFileExW(temporary, destination, MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)` when forced. Destructor removes only `temporary_` and ignores cleanup errors.

`main` must construct the transaction after annotation succeeds, point `OutputSink` at its temporary path while retaining the requested final format, close the sink, then call `commit()`. Any exception leaves the previous destination untouched unless `--force` replacement reached the final commit operation.

- [ ] **Step 5: Run injected-failure integration tests**

```bash
cmake --build build
ctest --test-dir build --output-on-failure -R "output-transaction|integration-failure"
```

Expected: nonzero exit, one stderr diagnostic, no published partial destination, and no `.ctb.*.tmp` file.

- [ ] **Step 6: Commit output safety**

```bash
git add CMakeLists.txt tests/CMakeLists.txt include/convert_to_biallelic/output_transaction.hpp src/output_transaction.cpp src/main.cpp tests/unit/test_output_transaction.cpp tests/integration_test.py
git commit -m "feat: publish converted VCF transactionally"
```

---

### Task 11: Complete Format, Memory, and Error Integration Coverage

**Files:**
- Modify: `tests/differential_test.py`
- Modify: `tests/integration_test.py`
- Modify: `tests/CMakeLists.txt`
- Modify: `src/main.cpp`
- Modify: `src/pipeline.cpp`

**Interfaces:**
- Consumes: the complete CLI and executable.
- Produces: verified four-way format support, memory backpressure, deterministic threading, and stable exit behavior.

- [ ] **Step 1: Add failing four-format tests**

For `.vcf` and `.vcf.gz` input crossed with `.vcf` and `.vcf.gz` output, run threads 1 and 4, decompress outputs when required, and compare with the Python oracle. Verify `.vcf.gz` begins with gzip magic bytes and can be read through HTSlib.

- [ ] **Step 2: Add failing memory-pressure and malformed-input tests**

Generate enough 1 MiB records to force `--memory-limit 64M` backpressure without exceeding it in one record. Assert completion, equality, and `peak_tracked_bytes <= 60397978` (90% of 64 MiB). Add malformed INFO, missing GT, invalid allele index, unreadable input, unwritable output directory, and annotation-too-large cases; each must exit 1 or 2 as specified and name the path/line in stderr.

- [ ] **Step 3: Run and observe specific failures**

```bash
cmake --build build
ctest --test-dir build --output-on-failure -R "differential-formats|integration-memory|integration-errors"
```

Expected: failures identify any incomplete format detection, accounting, or diagnostic paths.

- [ ] **Step 4: Make the minimum integration corrections**

Limit corrections to behaviors exposed by these tests: preserve final LF, pass the requested final format to a temporary filename without relying on the temporary extension, update tracked-memory counters on every permit change, include physical line numbers in conversion errors, and check explicit HTSlib flush/close return values before transaction commit.

- [ ] **Step 5: Run the entire suite repeatedly**

```bash
cmake --build build
ctest --test-dir build --output-on-failure
ctest --test-dir build --output-on-failure -R "pipeline|differential|integration" --repeat until-fail:20
```

Expected: full pass and 20 deterministic repetitions.

- [ ] **Step 6: Commit integration hardening**

```bash
git add tests/differential_test.py tests/integration_test.py tests/CMakeLists.txt src/main.cpp src/pipeline.cpp
git commit -m "test: verify formats memory and failure behavior"
```

---

### Task 12: Add Benchmarks, Build Documentation, and CI

**Files:**
- Create: `benchmarks/generate_fixture.py`
- Create: `benchmarks/run_benchmark.py`
- Create: `docs/build-linux.md`
- Create: `docs/build-windows.md`
- Create: `.github/workflows/ci.yml`
- Create: `README.md`
- Modify: `CMakeLists.txt`

**Interfaces:**
- Consumes: the finished executable and supported dependency environments.
- Produces: reproducible benchmark CSV, documented Linux/Windows builds, and automated cross-platform verification.

- [ ] **Step 1: Write the benchmark generator and runner**

`generate_fixture.py` accepts `--records`, `--samples`, `--seed`, `--annotation`, and `--input`; it uses only Python's standard library and writes deterministic valid fixtures. `run_benchmark.py` accepts the executable and fixtures, runs thread counts `1,2,4,8,16`, verifies every decompressed output hash is identical, and prints CSV columns:

```text
threads,seconds,input_records_per_second,output_bytes_per_second,peak_tracked_mib,sha256
```

- [ ] **Step 2: Document exact Linux commands**

```bash
sudo apt-get update
sudo apt-get install -y build-essential cmake ninja-build pkg-config libhts-dev python3 git
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build
ctest --test-dir build --output-on-failure
```

Document the `pkg-config --modversion htslib` check and require 1.17 or newer.

- [ ] **Step 3: Document exact Windows MSYS2 UCRT64 commands**

```bash
pacman -Syu
pacman -S --needed mingw-w64-ucrt-x86_64-gcc mingw-w64-ucrt-x86_64-cmake \
  mingw-w64-ucrt-x86_64-ninja mingw-w64-ucrt-x86_64-htslib \
  mingw-w64-ucrt-x86_64-python git
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build
ctest --test-dir build --output-on-failure
```

State that commands must run from the MSYS2 UCRT64 shell, not ordinary PowerShell.

- [ ] **Step 4: Add the CI matrix**

Use `ubuntu-latest` to install `libhts-dev`, configure, build, and test. Use `windows-latest` with `msys2/setup-msys2`, `msystem: UCRT64`, `update: true`, and install the UCRT64 GCC/CMake/Ninja/HTSlib/Python packages. Run the same CMake and CTest commands in both jobs. Upload `Testing/Temporary/LastTest.log` only on failure.

- [ ] **Step 5: Run release verification and a local benchmark**

```bash
cmake -S . -B build-release -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build-release
ctest --test-dir build-release --output-on-failure
python benchmarks/generate_fixture.py --records 100000 --samples 100 --seed 7 --annotation benchmark-annotation.vcf.gz --input benchmark-input.vcf.gz
python benchmarks/run_benchmark.py build-release/convert-to-biallelic benchmark-annotation.vcf.gz benchmark-input.vcf.gz
```

Expected: all tests pass, all SHA-256 values match, and the CSV records measured scaling without asserting an artificial speedup threshold.

- [ ] **Step 6: Commit documentation and automation**

```bash
git add README.md CMakeLists.txt .github/workflows/ci.yml docs/build-linux.md docs/build-windows.md benchmarks/generate_fixture.py benchmarks/run_benchmark.py
git commit -m "docs: add cross-platform build and benchmark workflow"
```

---

## Final Verification Gate

- [ ] Configure and build a clean Debug tree.
- [ ] Run the complete CTest suite with `--output-on-failure`.
- [ ] Run pipeline/differential/integration tests 20 times.
- [ ] Configure and build a clean Release tree.
- [ ] Run the 100,000-record benchmark at 1, 2, 4, 8, and 16 threads.
- [ ] Confirm all decompressed hashes match the Python oracle.
- [ ] Confirm progress is stdout-only and diagnostics are stderr-only.
- [ ] Confirm the output directory contains no abandoned temporary files.
- [ ] Confirm `git status --short` contains only intentional files.
- [ ] Record HTSlib, compiler, CMake, OS, elapsed-time, and peak-memory versions/results in the final handoff.
