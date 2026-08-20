# C++ Multithreaded VCF Converter Source-Only Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Author an uncompiled C++17/HTSlib replacement for `convert-to-biallelic.py` that supports ordered multithreading, `.vcf` and `.vcf.gz` output, a process-wide 2 GiB memory target, and progress on stdout.

**Architecture:** Load the annotation VCF once into an immutable chromosome/ID index, then stream the multiallelic input through a bounded reader, conversion-worker pool, and ordered writer. Keep conversion text-oriented for Python-compatible formatting while HTSlib handles line input and plain/BGZF output.

**Tech Stack:** C++17 source, CMake project metadata, HTSlib 1.17+ APIs, standard C++ threads and synchronization.

## Global Constraints

- This is a source-authoring-only plan because the user declined toolchain installation.
- Do not install Git, MSYS2, HTSlib, CMake, Ninja, GCC, pkg-config, Python packages, or any other dependency.
- Do not compile, link, execute, benchmark, or test the C++ program.
- Do not create a Git worktree or make commits because Git is unavailable.
- Do not claim that the source builds, passes tests, improves performance, or matches Python output until verified later in a suitable environment.
- Preserve the approved design in `docs/superpowers/specs/2026-07-18-cpp-multithreaded-vcf-converter-design.md` except where this plan explicitly removes testing and build execution.
- Keep `convert-to-biallelic.py` unchanged.
- Support explicit `--input`, `--variants`, and `--output` file paths; do not use stdin or stdout for VCF data.
- Send progress to stdout and errors/warnings to stderr.
- Infer plain VCF from `.vcf` and BGZF-compressed VCF from `.vcf.gz`.
- Treat 2 GiB as a soft process-wide target, not a per-thread allocation.
- Preserve record ordering with sequence-numbered chunks and an ordered writer.
- Use HTSlib text-line input rather than `bcf1_t` reserialization.
- Mark every delivered build, compatibility, memory, and performance property as unverified.

## Static Review Rules

Static review is allowed because it does not require installing or executing a development toolchain. Reviewers may use `Get-Content`, `rg`, and file listings to inspect authored text. They must not run CMake, a compiler, the Python oracle, the generated executable, or dependency commands.

For every task, the reviewer checks:

1. Every declared function has exactly one declaration and one source definition unless it is a template.
2. Namespaces, types, includes, and signatures match between files.
3. The task does not alter `convert-to-biallelic.py`.
4. No test, build, benchmark, installation, worktree, or Git command is executed.
5. Any uncertainty is recorded in `UNVERIFIED.md` rather than silently resolved by claiming success.

## File Map

- `CMakeLists.txt`: unexecuted future build description.
- `include/convert_to_biallelic/types.hpp`: shared output, thread, chunk, and statistics types.
- `include/convert_to_biallelic/cli.hpp`, `src/cli.cpp`: CLI and deterministic thread allocation.
- `include/convert_to_biallelic/vcf_io.hpp`, `src/vcf_io.cpp`: HTSlib/hFILE/BGZF wrappers.
- `include/convert_to_biallelic/annotation_index.hpp`, `src/annotation_index.cpp`: immutable lookup table.
- `include/convert_to_biallelic/converter.hpp`, `src/converter.cpp`: Python-compatible textual conversion logic.
- `include/convert_to_biallelic/memory_budget.hpp`, `src/memory_budget.cpp`: tracked-byte permits and one-overage escape.
- `include/convert_to_biallelic/bounded_queue.hpp`: cancellation-aware queue template.
- `include/convert_to_biallelic/progress.hpp`, `src/progress.cpp`: atomic counters and stdout reporting.
- `include/convert_to_biallelic/pipeline.hpp`, `src/pipeline.cpp`: reader/workers/ordered writer.
- `include/convert_to_biallelic/output_transaction.hpp`, `src/output_transaction.cpp`: retained native output ownership and platform publication.
- `src/main.cpp`: top-level orchestration and exit codes.
- `README.md`: intended usage and explicit unverified status.
- `UNVERIFIED.md`: compilation, correctness, performance, and platform checks deferred by request.

---

### Task 1: Create the Source Tree and Shared Types

**Files:**
- Create: `CMakeLists.txt`
- Create: `include/convert_to_biallelic/types.hpp`
- Create: `README.md`
- Create: `UNVERIFIED.md`

**Interfaces:**
- Produces: `OutputFormat`, `ThreadAllocation`, `PipelineStats`, `RawWorkChunk`, and `RawResultChunk` names used by later tasks.

- [ ] **Step 1: Create directories and an unexecuted CMake description**

Create `include/convert_to_biallelic/` and `src/`. Author `CMakeLists.txt` with C++17, `Threads`, `PkgConfig`, `htslib>=1.17`, a `ctb_core` static library containing every planned `.cpp`, and a `convert-to-biallelic` executable containing `src/main.cpp`. Do not configure or build it.

Required target shape:

```cmake
cmake_minimum_required(VERSION 3.20)
project(convert_to_biallelic VERSION 0.1.0 LANGUAGES C CXX)
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

- [ ] **Step 2: Define shared types**

```cpp
// include/convert_to_biallelic/types.hpp
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

- [ ] **Step 3: Document the unverified boundary**

`README.md` must show intended CLI usage and link to `UNVERIFIED.md`. `UNVERIFIED.md` must state that the source has not been compiled, linked, executed, tested against Python, memory-profiled, benchmarked, or verified on Windows/Linux.

- [ ] **Step 4: Perform static file review only**

Use file inspection to confirm the expected paths exist and the CMake source list matches the file map. Do not run CMake.

---

### Task 2: Author CLI Parsing and Thread Allocation

**Files:**
- Create: `include/convert_to_biallelic/cli.hpp`
- Create: `src/cli.cpp`

**Interfaces:**
- Consumes: `argc`, `argv`, filename extensions, and logical CPU count.
- Produces: `Config parse_cli(int, char**)`, `ThreadAllocation allocate_threads(...)`, and `usage_text()`.

- [ ] **Step 1: Declare configuration**

```cpp
// include/convert_to_biallelic/cli.hpp
#pragma once
#include "types.hpp"
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <stdexcept>

namespace ctb {
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

class UsageRequested final : public std::exception {
public:
    explicit UsageRequested(bool version) : version_(version) {}
    bool version() const noexcept { return version_; }
    const char* what() const noexcept override { return "usage requested"; }
private:
    bool version_;
};

Config parse_cli(int argc, char** argv);
ThreadAllocation allocate_threads(const Config&, bool compressed_input,
                                  bool compressed_output);
const char* usage_text();
}
```

- [ ] **Step 2: Implement argument parsing**

Implement required options `--variants`, `--input`, and `--output`; optional `--threads`, `--memory-limit`, `--progress-interval`, `--quiet`, `--force`, `--output-format`, `--help`, and `--version`. Reject missing/duplicate/unknown options, zero threads, memory below 64 MiB, negative progress intervals, and unsupported output extensions. Infer `.vcf` or `.vcf.gz` case-insensitively unless overridden.

- [ ] **Step 3: Implement deterministic thread allocation**

Use one output I/O worker when output is compressed and `threads > 1`; use one input I/O worker when input is compressed and `threads >= 4`; assign the remainder to conversion with a minimum of one. Treat `--threads` as an approximate CPU-intensive budget because reader, writer, and progress coordination threads also exist.

- [ ] **Step 4: Static-review CLI completeness**

Compare every option named in `usage_text()` with the parser branches. Record parser behavior as unverified; do not execute `--help` or `--version`.

---

### Task 3: Author HTSlib Text and BGZF I/O Wrappers

**Files:**
- Create: `include/convert_to_biallelic/vcf_io.hpp`
- Create: `src/vcf_io.cpp`

**Interfaces:**
- Consumes: paths, `OutputFormat`, and I/O thread counts.
- Produces: RAII `InputSource` and `OutputSink`.

- [ ] **Step 1: Declare pimpl wrappers**

```cpp
namespace ctb {
class InputSource {
public:
    InputSource(const std::filesystem::path&, std::size_t io_workers);
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
    OutputSink(const std::filesystem::path&, OutputFormat,
               std::size_t io_workers);
    OutputSink(int owned_fd, const std::filesystem::path& display_path,
               OutputFormat, std::size_t io_workers);
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

- [ ] **Step 2: Implement input using HTSlib line APIs**

Use `hts_open(path, "r")`, inspect `hts_get_format`, optionally call `hts_set_threads`, read through `hts_getline(..., KS_SEP_LINE, ...)`, treat `-1` as EOF and values below `-1` as error, copy the `kstring_t` content into `std::string`, and check `hts_close`.

- [ ] **Step 3: Implement explicit-format output**

For standalone path output, retain the path constructor. For transactional output, adopt an owned descriptor with `hdopen` so the sink never reopens the temporary pathname. For `OutputFormat::vcf`, use repeated `hwrite` until complete, `hflush`, and `hclose`. For `OutputFormat::vcf_gz`, wrap the adopted hFILE with `bgzf_hopen`, then use optional `bgzf_mt`, repeated `bgzf_write`, `bgzf_flush`, and `bgzf_close`. Use the explicit format rather than temporary filename extension.

- [ ] **Step 4: Static-review ownership and failure paths**

Confirm every handle is initialized once, explicitly closable, and released by a nonthrowing destructor. Mark exact HTSlib signature compatibility unverified.

---

### Task 4: Author the Annotation Index

**Files:**
- Create: `include/convert_to_biallelic/annotation_index.hpp`
- Create: `src/annotation_index.cpp`

**Interfaces:**
- Consumes: annotation `InputSource` and process memory limit.
- Produces: immutable lookup `chromosome -> ID -> VariantDefinition`.

- [ ] **Step 1: Declare lookup types**

```cpp
namespace ctb {
struct VariantDefinition {
    std::int64_t position = 0;
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
    std::uint64_t variant_count_ = 0;
    friend AnnotationIndex load_annotation(InputSource&, std::uint64_t);
};

AnnotationIndex load_annotation(InputSource&, std::uint64_t memory_limit);
}
```

- [ ] **Step 2: Implement streaming annotation parsing**

Skip headers; split data by tabs; require at least eight fields; parse positive POS with `std::from_chars`; find exact `ID=` in INFO; reject missing or comma-separated IDs; store POS, REF, and ALT; and overwrite an existing chromosome/ID so the last definition wins.

- [ ] **Step 3: Implement conservative memory estimation**

Include string capacities, hash-node payloads, and bucket arrays. Reserve 10% of the configured process target for allocator/HTSlib/thread overhead. Fail before output creation if the annotation estimate consumes the tracked allowance.

- [ ] **Step 4: Static-review index immutability**

Confirm no public mutating method exists after `load_annotation` returns and all worker access is through `const AnnotationIndex&`.

---

### Task 5: Author Python-Compatible Text Conversion

**Files:**
- Create: `include/convert_to_biallelic/converter.hpp`
- Create: `src/converter.cpp`

**Interfaces:**
- Consumes: one header/data line, `AnnotationIndex`, and physical source line number.
- Produces: optional header text or one-to-many newline-terminated records.

- [ ] **Step 1: Declare conversion API**

```cpp
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

- [ ] **Step 2: Implement header filtering**

Return no value for header lines containing `INFO=<ID=AF`, `INFO=<ID=AK`, `FORMAT=<ID=GL`, or `FORMAT=<ID=KC`. Return all other headers plus LF.

- [ ] **Step 3: Implement record parsing once per record**

Split tab fields; require at least nine fields; parse INFO once; require `ID`; create `allele_to_ids` as REF empty entry plus comma-separated allele IDs; parse FORMAT once; locate GT and optional GQ once; validate every sample field and allele index.

- [ ] **Step 4: Implement one-to-many emission**

For a single unknown ID, return the original line plus LF. Otherwise expand colon-separated component IDs, resolve each annotation, deduplicate by ID, sort by POS then ID, substitute POS/ID/REF/ALT, replace INFO with `ID=<id>` plus encountered MA/UK entries, retain only GT and optional GQ, normalize `|` to `/`, map emitted-ID alleles to `1`, other alleles to `0`, and preserve `.`.

- [ ] **Step 5: Perform line-by-line static comparison with Python**

Read `convert-to-biallelic.py` lines 26-101 and map each branch to a named C++ block in review notes. Record byte equivalence as unverified because neither implementation is executed.

---

### Task 6: Author Memory Budget and Bounded Queue Primitives

**Files:**
- Create: `include/convert_to_biallelic/memory_budget.hpp`
- Create: `src/memory_budget.cpp`
- Create: `include/convert_to_biallelic/bounded_queue.hpp`

**Interfaces:**
- Consumes: tracked-byte requests and item-capacity limits.
- Produces: movable permits, cancellation, peak accounting, and blocking FIFO queues.

- [ ] **Step 1: Implement movable memory permits**

`MemoryPermit` owns a byte reservation and releases it exactly once. `MemoryBudget::acquire` waits under a condition variable, `MemoryPermit::resize` adjusts before buffer growth, `cancel` wakes waiters, and `peak_bytes` reports the maximum tracked count.

- [ ] **Step 2: Implement one-overage escape**

Permit at most one worker to exceed the soft limit while growing a result. Track `overage_active_`; all other overage requests wait. Clear the flag when that permit shrinks below the limit or is destroyed so a worker can always finish and unblock the ordered writer.

- [ ] **Step 3: Implement the queue template**

Use `std::deque<T>`, one mutex, not-empty/not-full condition variables, fixed item capacity, `push`, `pop`, `close`, and `cancel`. Closing drains existing items; cancellation clears items and wakes all producers/consumers.

- [ ] **Step 4: Static-review lock ordering**

Document the only permitted nesting order as queue lock then no memory lock, or memory lock then no queue lock. Require callers to acquire/resize permits outside queue critical sections to avoid lock inversion. Record runtime deadlock behavior as unverified.

---

### Task 7: Author Progress Reporting and the Ordered Pipeline

**Files:**
- Create: `include/convert_to_biallelic/progress.hpp`
- Create: `src/progress.cpp`
- Create: `include/convert_to_biallelic/pipeline.hpp`
- Create: `src/pipeline.cpp`

**Interfaces:**
- Consumes: input/output streams, annotation, thread allocation, memory limit, progress interval.
- Produces: deterministic `PipelineStats run_pipeline(...)` and stdout progress.

- [ ] **Step 1: Implement progress counters and reporter**

Use atomic input/output/memory counters and one reporter thread. Every interval, emit one flushed stdout line containing elapsed time, input records, written output records, recent rate, and tracked MiB. The final stdout summary includes peak tracked pipeline MiB. `--quiet` produces no periodic or final output. Errors never use stdout.

- [ ] **Step 2: Define pipeline options**

```cpp
namespace ctb {
struct WorkChunk {
    RawWorkChunk data;
    MemoryPermit permit;
};

struct ResultChunk {
    RawResultChunk data;
    MemoryPermit permit;
};

struct PipelineOptions {
    ThreadAllocation threads;
    std::uint64_t memory_limit_bytes = 2ULL * 1024ULL * 1024ULL * 1024ULL;
    std::size_t target_records_per_chunk = 512;
    std::uint64_t target_bytes_per_chunk = 8ULL * 1024ULL * 1024ULL;
    std::chrono::milliseconds progress_interval{5000};
    bool quiet = false;
};

PipelineStats run_pipeline(InputSource&, OutputSink&, const AnnotationIndex&,
                           const PipelineOptions&, std::ostream& progress,
                           std::ostream& diagnostics);
}
```

- [ ] **Step 3: Implement reader/chunk creation**

Write filtered leading headers before starting data results. Assign increasing chunk sequence numbers. End a chunk at 512 records or 8 MiB, whichever comes first; a single oversized record becomes one chunk. Limit in-flight chunks to twice the conversion-worker count and reduce through memory backpressure.

- [ ] **Step 4: Implement workers and ordered writer**

Workers pop dynamically, convert records sequentially, resize their permits before output-buffer growth, release source storage, and push one result per sequence. A pipeline-owned string or vector `reserve()` temporarily admits both its old allocation and the requested new allocation, then reconciles actual capacity. The writer stores results in `std::map<uint64_t, ResultChunk>`, writes only the next expected sequence, releases permits after writing, and rejects a missing final sequence.

- [ ] **Step 5: Implement cancellation**

Store the first `exception_ptr`, cancel queues and memory waits, let the last worker close results, join every thread, stop the progress reporter, and rethrow only after joins. Record concurrency correctness as unverified.

- [ ] **Step 6: Static-review ordering invariants**

Confirm the coordinator writes filtered leading headers synchronously before any pipeline thread starts, and only the ordered writer writes data-result chunks afterward. Workers never write files or stdout; sequence increments only in the reader; and writer increments `next_sequence` only after a successful complete chunk write.

---

### Task 8: Author Transactional Output and Main Orchestration

**Files:**
- Create: `include/convert_to_biallelic/output_transaction.hpp`
- Create: `src/output_transaction.cpp`
- Create: `src/main.cpp`

**Interfaces:**
- Consumes: `Config` and all completed components.
- Produces: executable lifecycle, final file publication, and exit codes.

- [ ] **Step 1: Implement output transaction**

Exclusively create and retain a native output identity in the destination directory. Duplicate that HANDLE/file descriptor for `OutputSink`; main must not reopen a temporary pathname. Prefer Linux `O_TMPFILE`, with an exclusive named fallback where unsupported. Without `--force`, reject an existing destination during preflight and use no-overwrite publication so a concurrently created destination is not replaced. Force publication may replace the final destination without pre-deleting it. A nonthrowing destructor disposes transaction-owned state best-effort, and `commit()` is idempotent.

The threat model is normal, non-adversarial operation, including failures, temporary-name collisions, and concurrent creation of the final destination. The output directory must not be modified by an adversarial process while the transaction is active. Windows forced `MoveFileExW` and Linux named-temporary/anonymous-stage publication are pathname-based and are not hardened against same-directory substitution between best-effort identity checks and publication.

- [ ] **Step 2: Implement main lifecycle**

Order operations exactly:

1. Parse CLI and handle help/version.
2. Open and load annotation.
3. Open input and detect compression.
4. Allocate conversion/input/output threads.
5. Create output transaction.
6. Duplicate the transaction's retained native output and construct `OutputSink` from the owned descriptor using the requested format, not temporary extension.
7. Run pipeline with `std::cout` progress and `std::cerr` diagnostics.
8. Flush/close output.
9. Commit transaction.
10. Print final progress summary unless quiet.

- [ ] **Step 3: Implement exit mapping**

Return 0 for help/version/success, 2 for CLI errors, and 1 for annotation, input, conversion, output, HTSlib, memory, or thread failures. Emit one primary exception message to stderr.

- [ ] **Step 4: Static-review resource order**

Confirm C++ object construction/destruction order closes reporter/threads before streams and streams before transaction cleanup. Confirm descriptor/HANDLE ownership and best-effort path-identity checks. Record Windows/POSIX publication behavior and the non-adversarial output-directory threat model as unverified.

---

### Task 9: Perform Whole-Source Static Review and Handoff

**Files:**
- Modify: `README.md`
- Modify: `UNVERIFIED.md`
- Inspect: all `include/convert_to_biallelic/*.hpp`
- Inspect: all `src/*.cpp`

**Interfaces:**
- Consumes: complete authored source tree.
- Produces: an honest source-only handoff with unresolved verification work.

- [ ] **Step 1: Check file and declaration coverage**

Use `rg` to list every `ctb::` declaration and definition. Reconcile misspelled names, missing includes visible from inspection, duplicate types, namespace mismatches, and CMake source-list omissions without running the compiler.

- [ ] **Step 2: Check approved requirements statically**

Confirm source text contains HTSlib line I/O, BGZF output, `.vcf` output, a 2 GiB default, stdout progress, stderr diagnostics, bounded queues, dynamic chunks, sequence reordering, Python header filters, MA/UK handling, GT/GQ handling, and explicit output paths.

- [ ] **Step 3: Update the unverified ledger**

List every deferred requirement:

- compiler and linker success;
- HTSlib API/ABI compatibility;
- Windows and Linux builds;
- Python byte equivalence;
- `.vcf` and `.vcf.gz` round trips;
- multithread ordering and deadlock freedom;
- memory-limit behavior;
- progress routing;
- transactional cleanup;
- throughput and scaling.

- [ ] **Step 4: Deliver without success claims**

Report the created source files and explicitly state: “Source authored but not compiled or tested at the user’s request.” Do not describe the converter as working, complete, passing, fast, or compatible until future execution verifies those properties.
