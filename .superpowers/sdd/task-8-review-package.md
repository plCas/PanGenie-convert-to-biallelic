# Task 8 Full-File Static Review Package

## No-Git-base note

This source-only review did not use a Git base, diff, status, commit, branch, or
worktree. The package below contains exact fenced copies of every file changed
for Task 8 before this package was created. The package cannot recursively
contain itself.

## Review boundary

The files below were compared statically with the Task 8 brief and governing
design/source-only plan. Retained HANDLE/file-descriptor sink ownership,
platform publication, collision and concurrent-destination behavior,
best-effort identity checks, the non-adversarial output-directory threat model,
cleanup, post-commit summary ordering, object lifetime, and the deferred
runtime boundary are documented in the Task 8 report.

No compiler, build, executable, test, benchmark, installation, Python process,
or Git command was run.

## `README.md`

````````text
# convert_to_biallelic

`convert_to_biallelic` is an authored, source-only C++17/HTSlib converter for
transforming multiallelic VCF input into biallelic VCF output using an
annotation VCF.

## Intended usage

```text
convert-to-biallelic \
  --variants annotation.vcf.gz \
  --input multiallelic.vcf.gz \
  --output biallelic.vcf.gz \
  --threads 8 \
  --memory-limit 2G
```

The intended executable requires explicit input, annotation, and output paths.
It writes periodic progress to stdout and diagnostics to stderr. Output is
written through a duplicated descriptor for an exclusively created native
file identity retained by the transaction; main never reopens the temporary
pathname for sink I/O. The requested path is published only after the pipeline
and output close succeed. Existing output is rejected unless `--force` is
supplied, and no-force publication does not replace a destination created
concurrently. The final success summary is intended to appear only after
publication succeeds, and `--quiet` suppresses both periodic progress and that
summary.

The Linux implementation is intentionally Linux-specific. It prefers an
unnamed `O_TMPFILE` and publishes that retained identity with `AT_EMPTY_PATH`
when permitted, or through the documented unprivileged
`/proc/self/fd/<fd>` plus `AT_SYMLINK_FOLLOW` route. If the filesystem or
kernel does not support `O_TMPFILE`, it keeps an exclusively created named
temporary entry and verifies its device/inode identity against the retained
descriptor immediately before link/rename publication.

The transaction guarantee assumes normal, non-adversarial operation. It covers
failures, temporary-name collisions, and concurrent creation of the final
destination, but the output directory must not be modified by an adversarial
process while a transaction is active. Windows forced `MoveFileExW` and Linux
named-temporary or anonymous-stage fallbacks are pathname-based and are not
hardened against same-directory substitution between the best-effort identity
check and publication. These routes remain source-only and must be verified on
target kernels, filesystems, procfs mounts, and Windows installations.

This source tree is authored only; its build, compatibility, and performance
properties remain unverified. See [UNVERIFIED.md](UNVERIFIED.md) for the
explicit verification boundary.

````````

## `UNVERIFIED.md`

````````text
# Unverified status

This source has not been compiled, linked, or executed. It has not been tested
against the Python implementation, memory-profiled, or benchmarked. It has not
been verified on Windows or Linux.

The CMake configuration and future HTSlib integration are source descriptions
only. Buildability, compatibility, correctness, memory behavior, and
performance must be verified in a suitable environment before use.

Transactional output is also unverified. Linux prefers `O_TMPFILE`, attempts
Linux-specific `AT_EMPTY_PATH`, and falls back on the documented unprivileged
`/proc/self/fd/<fd>` plus `AT_SYMLINK_FOLLOW` route when capability is absent.
Availability and behavior across kernels, filesystems, procfs mount/security
settings, and protected-hardlink policies must be validated. Filesystems
without `O_TMPFILE` use a linked exclusive temporary entry with immediate
pre-publication device/inode verification; mismatch/error cleanup and possible
safe orphaning require fault injection. Windows `DuplicateHandle`, CRT
descriptor adoption, handle-based no-force rename, verified-path forced
`MoveFileExW` replacement with write-through, and disposition cleanup likewise
require native fault-injection verification.

The intended transactional threat model is normal, non-adversarial operation,
including failures, temporary-name collisions, and concurrent creation of the
final destination. The output directory must not be modified by an adversarial
process during a transaction. Windows forced `MoveFileExW` and Linux named or
stage fallback publication are pathname-based; their immediate identity checks
are best-effort mismatch detection, not hardening against same-directory
substitution between check and publication.

````````

## `.superpowers/sdd/progress.md`

````````text
# Source-only SDD progress

Plan: `docs/superpowers/plans/2026-07-18-cpp-multithreaded-vcf-converter-source-only.md`

Constraints: no installation, compilation, execution, tests, worktrees, or commits. Reviews are static and all deliverables remain unverified.

Task 1: complete (source-only, static review clean; no commits)
Task 2: complete (source-only, static review clean; no commits)
Task 3: complete (source-only, static review clean; no commits; minor: defensive over-write check and future C++20 u8string portability)
Task 4: complete (source-only, static review clean; no commits; minor: theoretical uint64 line-count boundary)
Task 5: complete (source-only, static review clean; no commits; added checked annotation lookup to distinguish absence from allocation failure)
Task 6: complete (source-only, static review clean; no commits; queue requires nothrow movable/destructible elements)
Task 7: complete (source-only, static review clean after bounded-scratch and allocation-free annotation lookup refactor; no commits)
Task 8: complete (source-only, static review clean; retained handle/fd sink ownership, O_TMPFILE or checked linked fallback, transactional publication under a documented non-adversarial output-directory threat model, and post-commit final summary; no commits)

````````

## `.superpowers/sdd/task-8-brief.md`

````````text
### Task 8: Author Transactional Output and Main Orchestration

Create `include/convert_to_biallelic/output_transaction.hpp`, `src/output_transaction.cpp`, and `src/main.cpp`. Modify `vcf_io`, progress/pipeline/shared stats, and documentation only as needed for retained-descriptor output and the final-success ordering below. Source-only: no install/build/run/test/Git; Python unchanged.

OutputTransaction API:

```cpp
class OutputTransaction {
public:
  OutputTransaction(std::filesystem::path destination, bool force);
  ~OutputTransaction();
  OutputTransaction(const OutputTransaction&) = delete;
  OutputTransaction& operator=(const OutputTransaction&) = delete;
  OutputTransaction(OutputTransaction&&) = delete;
  OutputTransaction& operator=(OutputTransaction&&) = delete;
  const std::filesystem::path& temporary_path() const noexcept;
  int take_sink_fd();
  void commit();
};
```

Transaction requirements:

1. Destination must have a nonempty filename and existing parent directory. Without force, reject existing destination before creating temp.
2. Exclusively create and retain a same-directory native output identity. Windows uses a unique `.<filename>.ctb.<pid>.<counter>.tmp` entry. Linux prefers `O_TMPFILE` and uses an exclusive no-follow named entry when unsupported. Do not follow or smash an existing temporary path.
3. `take_sink_fd()` duplicates the retained HANDLE/file descriptor exactly once for an adopting `OutputSink`; main must not reopen a temporary pathname. The noexcept destructor disposes transaction-owned uncommitted state best-effort. `commit()` after commit is idempotent.
4. Windows no-force finalize must fail if destination exists; force uses `MoveFileExW` replacement + write-through. Linux no-force uses no-overwrite publication; force uses same-filesystem rename replacement. Do not pre-delete an existing destination.
5. Check every OS call and include source/destination paths in errors. Do not delete an existing final destination before a successful replacement operation.
6. Output format is never inferred from temp suffix; main passes Config.output_format to OutputSink.
7. Transaction guarantees cover normal non-adversarial operation, failures, temporary-name collisions, and concurrent destination creation. The output directory must not be modified by an adversarial process while a transaction is active. Windows forced `MoveFileExW` and Linux named/stage fallbacks are pathname-based and not hardened against same-directory substitution; retain the existing best-effort immediate identity checks.

Main lifecycle:

1. Parse CLI. `--help` prints usage to stdout and returns 0; `--version` prints `convert-to-biallelic 0.1.0` and returns 0.
2. Load annotation from Config.variants before creating output transaction.
3. Open input, get compressed(), calculate ThreadAllocation using config and output format.
4. Construct OutputTransaction, take its duplicate native descriptor, then construct the adopting OutputSink with the temporary path only as display context plus the explicit final OutputFormat and output I/O workers.
5. Construct PipelineOptions and run_pipeline with cout progress/cerr diagnostics.
6. On pipeline success, flush and explicitly close OutputSink; then commit transaction.
7. Only after successful commit print/flush exactly one final success summary to stdout unless quiet. Resolve the earlier Task7 plan conflict in favor of this safety rule: pipeline periodic reporter must stop without final success text and return elapsed time/statistics; final summary formatting is reusable from progress.cpp and called by main post-commit.
8. If pipeline, flush, close, or commit fails: no final success summary; emit one primary diagnostic to stderr; transaction cleans temp; return 1.
9. CLI errors return 2. Unexpected std::exception returns 1. Avoid duplicate diagnostics from lower layers.
10. Annotation InputSource may use zero I/O workers during initial load; record this choice.
11. Preserve stdout progress/stderr errors and explicit output file.
12. Static-review object destruction order: threads/reporter finish before I/O; OutputSink closes before transaction commit; transaction outlives sink; no dangling callbacks.
13. Update README intended usage/status if orchestration changes it. Everything remains unverified.

Use apply_patch. Write report and complete exact-file review package including every changed file, at `task-8-report.md` and `task-8-review-package.md`. No tests/commits.

````````

## `docs/superpowers/specs/2026-07-18-cpp-multithreaded-vcf-converter-design.md`

````````text
# C++ Multithreaded VCF Converter Design

Date: 2026-07-18

## Objective

Replace `convert-to-biallelic.py` with a cross-platform C++17 command-line program that preserves the Python converter's uncompressed VCF output, processes records in parallel, uses HTSlib for VCF text and BGZF I/O, reports progress to stdout, and targets at most 2 GiB of process memory under ordinary inputs.

The first release supports `.vcf` and `.vcf.gz` inputs and outputs on Windows and Linux. BCF, indexing, region queries, distributed processing, and a Python API are outside this scope.

## Success Criteria

1. For every supported valid fixture, the C++ program's uncompressed VCF bytes equal the Python program's output bytes.
2. Runs with different thread counts produce identical uncompressed VCF output and record ordering.
3. `.vcf.gz` output is BGZF-compressed and decompresses to the same bytes as `.vcf` output.
4. The program opens each input and output stream once and does not pre-count VCF records.
5. The default memory target is 2 GiB for the entire process, not per worker.
6. Progress is printed to stdout without contaminating the VCF, which is always written to an explicit output file.
7. The project builds and its tests run on Windows and Linux.

## Command-Line Interface

The executable is named `convert-to-biallelic`.

```text
convert-to-biallelic \
  --variants annotation.vcf.gz \
  --input multiallelic.vcf.gz \
  --output result.vcf.gz \
  --threads 16 \
  --memory-limit 2G
```

Required options:

- `--variants PATH`: annotation VCF containing the position, REF, ALT, and one `INFO/ID` value for each known variant.
- `--input PATH`: multiallelic VCF to convert.
- `--output PATH`: destination ending in `.vcf` or `.vcf.gz`.

Optional options:

- `--threads N`: CPU-intensive thread budget. The default is the number of logical CPUs reported by the standard library, with a minimum of one.
- `--memory-limit SIZE`: process-wide soft memory target. The default is `2G`; suffixes `K`, `M`, and `G` use powers of 1024.
- `--progress-interval SECONDS`: progress-report interval. The default is 5 seconds. Zero disables periodic reports but not the final summary.
- `--quiet`: disable periodic progress and the final summary.
- `--force`: allow replacement of an existing output file. Without it, an existing destination is an error.
- `--output-format vcf|vcf.gz`: override extension-based output selection. Without this option, `.vcf` selects plain VCF and `.vcf.gz` selects BGZF-compressed VCF; other extensions are rejected.
- `--help` and `--version`.

Standard input and standard output are not VCF data channels in the first release. This keeps stdout exclusively available for requested progress output. Diagnostics go to stderr.

## Compatibility Contract

The converter preserves the behavior of `convert-to-biallelic.py`:

- Header lines containing `INFO=<ID=AF`, `INFO=<ID=AK`, `FORMAT=<ID=GL`, or `FORMAT=<ID=KC` are removed.
- Other header lines retain their text and order.
- The annotation's `INFO/ID` value must contain exactly one comma-delimited ID.
- The last annotation entry wins if the same chromosome and ID occur more than once, matching Python dictionary assignment.
- A biallelic record with a single unknown ID is passed through unchanged.
- Known IDs referenced by an input record are deduplicated and sorted by annotation position.
- Each known ID produces one record with annotation position, ID, REF, ALT, and `INFO/ID` substituted.
- Only `MA` and `UK` are copied from the remaining INFO entries.
- FORMAT output contains `GT` and, when present in the input FORMAT, `GQ`.
- Input phasing separators are normalized to `/`, as in the Python implementation.
- Missing alleles remain `.`; alleles containing the emitted ID become `1`; other alleles become `0`.
- The ordering of input records, emitted IDs within each input record, and samples is deterministic.

The compatibility target is the uncompressed VCF byte stream. BGZF block layout, compression metadata, and compressed bytes are not required to match another compressor.

LF and CRLF input terminators are accepted through HTSlib and output is normalized to LF, matching Python text-mode behavior. Differential equivalence is defined for conventional newline-terminated VCF text. An unterminated final line is outside the byte-equivalence contract because the Python script's `line[:-1]` behavior can truncate such a line.

The C++ implementation replaces Python `assert` failures with descriptive validation errors that identify the file and record number. A validation failure makes the command fail rather than silently changing the record.

## HTSlib Strategy

HTSlib provides transparent plain/BGZF input and BGZF output. Conversion remains text-oriented rather than decoding and reserializing every record through `bcf1_t`, because text-oriented processing gives direct control over the Python-compatible output representation.

An `InputSource` abstraction uses `hts_open`, `hts_getline`, `hts_set_threads` when allocated I/O workers, and `hts_close`. It yields complete text lines without trailing newline characters. An `OutputSink` abstraction accepts complete output buffers. For transactional output, it adopts a duplicated native descriptor rather than reopening a temporary pathname. Its plain implementation uses HTSlib hFILE operations (`hdopen`, `hwrite`, and `hclose`); its compressed implementation wraps that hFILE with `bgzf_hopen`, then uses `bgzf_mt`, `bgzf_write`, `bgzf_flush`, and `bgzf_close`. Both implementations check write, flush, and close errors.

HTSlib 1.17 or newer is required. CI records and tests a specific HTSlib release rather than silently accepting an older library.

HTSlib structured VCF APIs may be used for validation where they do not alter the text, but structured reserialization is not part of the output path. BCF support is deliberately excluded.

## Components

### CLI and configuration

Parses arguments, validates paths and numeric limits, chooses the output format, calculates the thread allocation, and produces an immutable configuration object.

### Annotation index

Streams the annotation file once and builds a read-only lookup:

```text
chromosome -> variant ID -> {position, REF, ALT}
```

Position is stored as a checked 64-bit integer for sorting and as normalized decimal text for output. Strings are owned by the index. After construction, workers only perform concurrent reads, so the index needs no locks.

The loader estimates index memory from string capacities, table bucket counts, and element overhead. If the index and safety reserve consume the entire 2 GiB target, the program fails before opening the output.

### Record converter

A pure conversion unit accepts one input record and the immutable annotation index, then returns zero or more complete VCF lines. It has no file handles and no shared mutable state. This boundary allows direct unit and differential testing.

Parsing improvements over the Python implementation are allowed only when output behavior remains identical. In particular, FORMAT is parsed once per record; GT and GQ indices are reused for all samples; INFO and allele-to-ID relationships are also parsed once per record.

### Work and result chunks

A work chunk contains a monotonically increasing sequence number and a vector of source records. Chunking is scheduling only; it never merges biological records.

A result chunk contains the same sequence number, an output byte buffer, and input/output record counters. Records inside a chunk are converted sequentially, so their local ordering is stable.

The reader starts with a target of 512 records or 8 MiB of input text, whichever comes first. The memory controller may reduce these limits. The final partial chunk is valid. A single oversized VCF record forms its own chunk.

### Ordered pipeline

The pipeline has one reader/coordinator, conversion workers, and one ordered writer:

```text
HTSlib reader -> bounded work queue -> conversion workers
              -> bounded result queue -> reorder buffer -> HTSlib writer
```

The reader assigns sequence numbers and blocks when the work queue is full. Workers claim chunks dynamically, which avoids the load imbalance caused by assigning one fixed file partition to each CPU. The writer stores early results until the next expected sequence arrives, then writes consecutive ready chunks in order.

No line-counting pass is performed. The input and output are each opened once.

### Progress reporter

Workers and the writer update atomic counters. A single reporter samples them at the configured interval and writes one complete line at a time to stdout. It reports elapsed time, input records converted, output records written, recent throughput, and tracked buffered memory. It does not claim an exact percentage because the program does not pre-count records.

Example:

```text
[00:00:30] input=4,821,120 output=6,903,445 rate=160,704 records/s memory=742 MiB
Finished: input=13,442,817 output=18,791,203 elapsed=00:01:08 average=197,688 records/s
```

Annotation-loading progress may report records and estimated memory before conversion starts.

## Thread Allocation

`--threads N` is the approximate budget for CPU-intensive conversion and compressed-I/O work. Reader, writer, and progress coordination threads exist in addition but usually block on queues or timers.

- With `N=1`, conversion and HTSlib I/O are synchronous.
- With `N>1` and `.vcf.gz` output, one unit of the budget is assigned to HTSlib BGZF compression.
- With `N>=4` and compressed input, one additional unit is assigned to HTSlib decompression.
- All remaining units become conversion workers, with at least one conversion worker.
- Plain input or output returns the corresponding I/O unit to conversion workers.

`hts_set_threads` is used for compressed input and `bgzf_mt` for compressed output with the allocations above. Benchmark results may justify a later explicit `--io-threads` tuning option, but that option is not in the first release.

## Memory Management

The 2 GiB default is a soft target for the whole process. It includes the annotation index estimate, queued source text, queued result text, reorder buffers, and tracked worker scratch buffers. It is not multiplied by the number of workers.

The controller reserves 10% of the configured limit for allocator overhead, thread stacks, HTSlib, and untracked temporary allocations. The remaining tracked budget is divided between the immutable index and pipeline buffers.

At most twice the number of conversion workers may be in flight, and fewer are allowed when their reserved bytes reach the budget. Enqueuing a chunk acquires memory-budget capacity; writing and destroying it releases capacity. This backpressure pauses reading rather than allowing unbounded buffering.

Output expansion is data-dependent. Under pressure, the reader reduces future chunks as far as one record. If a single input record or its complete converted output exceeds the remaining tracked budget, it is permitted as one exceptional chunk and a warning is printed to stderr. Thus the target cannot be an absolute RSS ceiling for arbitrary records. The final summary reports peak tracked pipeline memory.

## Output Safety and Errors

The program validates all input paths and the destination before conversion. The output transaction exclusively creates and retains a native file identity in the destination directory, and `OutputSink` writes through a duplicate of that retained descriptor or HANDLE. The final destination is published only after the pipeline and output close succeed. This avoids publishing a partial final VCF after a worker, HTSlib, disk, or validation failure. `--force` controls replacement of an existing destination; no-force publication must not overwrite a destination created concurrently.

These transactional guarantees cover normal, non-adversarial operation, including failures, temporary-name collisions, and concurrent creation of the final destination. The output directory must not be modified by an adversarial process while a transaction is active. Windows forced replacement uses pathname-based `MoveFileExW`, and Linux named-temporary and anonymous-staging fallbacks use pathname-based link/rename operations. Those branches retain best-effort immediate identity checks where available, but they are not hardened against same-directory pathname substitution between the check and publication.

Any thread may publish the first failure through shared cancellation state. Queues wake, producers and consumers stop, all threads join, HTSlib handles close, and transaction-owned temporary state is removed best-effort. The program emits one primary diagnostic to stderr and exits nonzero.

Errors include:

- unsupported extension or invalid option;
- unreadable input or unwritable destination;
- malformed VCF fields or missing `INFO/ID`;
- non-numeric or invalid position;
- unknown composite variant ID that the Python program would fail to resolve;
- invalid genotype allele index;
- HTSlib read, write, flush, compression, or close failure;
- configured memory too small for the annotation index and safety reserve.

## Testing

Tests use CTest and a small Python harness. The existing Python script remains the compatibility oracle during development.

### Unit tests

- CLI size and extension parsing.
- INFO and FORMAT parsing.
- GT/GQ conversion, phasing normalization, and missing alleles.
- ID deduplication and positional ordering.
- unknown single-ID passthrough.
- annotation duplicate handling.
- chunk-size and memory-budget decisions.
- ordered writer behavior under deliberately reversed worker completion.

### Differential tests

Generate the Python output for fixtures, run the C++ tool, decompress when necessary, and compare bytes. Fixtures cover headers, MA/UK, unknown IDs, multiple ALT alleles, composite allele IDs, missing alleles, GT with and without GQ, multiple samples, duplicate IDs, LF and CRLF input, and one input record expanding to several outputs.

Every fixture runs with one and multiple threads. Repeated multithreaded runs verify deterministic output.

### Integration tests

- All four supported input/output combinations: VCF-to-VCF, VCF-to-VCF.GZ, VCF.GZ-to-VCF, and VCF.GZ-to-VCF.GZ.
- Progress captured from stdout and diagnostics captured from stderr.
- Existing-output behavior with and without `--force`.
- Cancellation and temporary-file cleanup after injected conversion and write failures.
- Small memory limits that force queue backpressure and one-record chunks.

### Performance tests

A generated, representative multi-sample VCF is processed with 1, 2, 4, 8, and 16 requested threads. Reports include elapsed time, input records per second, output bytes per second, peak tracked memory, and scaling efficiency. Chunk targets of 128, 512, and 1,024 records plus byte limits are benchmarked before retaining the default. Correctness comparisons are mandatory; a speedup threshold is reported rather than hard-coded until representative real data is available.

### Cross-platform checks

CI builds and runs tests on current Windows and Linux runners. Windows uses the MSYS2 UCRT64 MinGW toolchain and `mingw-w64-ucrt-x86_64-htslib`; the current vcpkg HTSlib port cannot be used because it excludes Windows. Linux discovers HTSlib 1.17 or newer through pkg-config. The build records the exact compilers and HTSlib release exercised by CI.

## Proposed Project Layout

```text
CMakeLists.txt
.github/workflows/ci.yml
include/convert_to_biallelic/
  annotation_index.hpp
  bounded_queue.hpp
  cli.hpp
  converter.hpp
  memory_budget.hpp
  pipeline.hpp
  progress.hpp
  vcf_io.hpp
src/
  annotation_index.cpp
  cli.cpp
  converter.cpp
  main.cpp
  memory_budget.cpp
  pipeline.cpp
  progress.cpp
  vcf_io.cpp
tests/
  fixtures/
  differential_test.py
  unit/
benchmarks/
  generate_fixture.py
docs/
  build-windows.md
  build-linux.md
```

## Delivery Sequence

1. Establish CMake, HTSlib discovery, a minimal executable, and cross-platform smoke tests.
2. Capture the Python behavior in differential fixtures before implementing conversion.
3. Implement HTSlib text I/O and the annotation index.
4. Implement and validate the single-record converter.
5. Add single-thread streaming output and prove differential equivalence.
6. Add bounded queues, ordered multithreading, cancellation, and the memory controller.
7. Add `.vcf.gz` output and HTSlib I/O thread allocation.
8. Add stdout progress reporting and final statistics.
9. Complete failure-path, cross-platform, determinism, memory, and performance verification.

## Design References

- HTSlib documentation: https://www.htslib.org/doc/
- HTSlib official repository: https://github.com/samtools/htslib
- BGZF behavior and threading: https://www.htslib.org/doc/bgzip.html
- MSYS2 Windows HTSlib package: https://packages.msys2.org/base/mingw-w64-htslib
- vcpkg HTSlib manifest showing Windows exclusion: https://github.com/microsoft/vcpkg/blob/master/ports/htslib/vcpkg.json

````````

## `docs/superpowers/plans/2026-07-18-cpp-multithreaded-vcf-converter-source-only.md`

````````text
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

Use atomic input/output/memory counters and one reporter thread. Every interval, emit one flushed stdout line containing elapsed time, input records, written output records, recent rate, and tracked MiB. `--quiet` produces no periodic or final output. Errors never use stdout.

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

Workers pop dynamically, convert records sequentially, resize their permits before output-buffer growth, release source storage, and push one result per sequence. The writer stores results in `std::map<uint64_t, ResultChunk>`, writes only the next expected sequence, releases permits after writing, and rejects a missing final sequence.

- [ ] **Step 5: Implement cancellation**

Store the first `exception_ptr`, cancel queues and memory waits, let the last worker close results, join every thread, stop the progress reporter, and rethrow only after joins. Record concurrency correctness as unverified.

- [ ] **Step 6: Static-review ordering invariants**

Confirm only the writer calls `OutputSink::write`; workers never write files or stdout; sequence increments only in the reader; and writer increments `next_sequence` only after a successful complete chunk write.

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

````````

## `include/convert_to_biallelic/types.hpp`

````````text
#pragma once

#include <chrono>
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
    std::chrono::steady_clock::duration elapsed{};
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

}  // namespace ctb

````````

## `include/convert_to_biallelic/progress.hpp`

````````text
#pragma once

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <exception>
#include <functional>
#include <iosfwd>
#include <mutex>
#include <string>
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
    ProgressReporter(ProgressCounters& counters,
                     std::chrono::milliseconds interval,
                     std::ostream& output,
                     bool quiet,
                     std::function<void(std::exception_ptr)> error_callback =
                         {});
    ~ProgressReporter();

    ProgressReporter(const ProgressReporter&) = delete;
    ProgressReporter& operator=(const ProgressReporter&) = delete;
    ProgressReporter(ProgressReporter&&) = delete;
    ProgressReporter& operator=(ProgressReporter&&) = delete;

    void start();
    std::chrono::steady_clock::duration finish();
    void stop_without_summary() noexcept;

private:
    enum class State { idle, running, finishing, stopped, finished };

    void record_error(std::exception_ptr error) noexcept;
    void join_thread_safely() noexcept;
    void run() noexcept;

    ProgressCounters& counters_;
    std::chrono::milliseconds interval_;
    std::ostream& output_;
    bool quiet_;
    std::function<void(std::exception_ptr)> error_callback_;

    std::mutex mutex_;
    std::condition_variable changed_;
    std::thread thread_;
    std::chrono::steady_clock::time_point started_{};
    std::exception_ptr thread_error_;
    State state_ = State::idle;
    bool stopping_ = false;
    bool thread_exited_ = true;
};

std::string format_final_summary(const PipelineStats& stats);

}  // namespace ctb

````````

## `include/convert_to_biallelic/output_transaction.hpp`

````````text
#pragma once

#include <filesystem>
#include <memory>

namespace ctb {

class OutputTransaction {
public:
    OutputTransaction(std::filesystem::path destination, bool force);
    ~OutputTransaction() noexcept;

    OutputTransaction(const OutputTransaction&) = delete;
    OutputTransaction& operator=(const OutputTransaction&) = delete;
    OutputTransaction(OutputTransaction&&) = delete;
    OutputTransaction& operator=(OutputTransaction&&) = delete;

    const std::filesystem::path& temporary_path() const noexcept;
    int take_sink_fd();
    void commit();

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

}  // namespace ctb

````````

## `include/convert_to_biallelic/vcf_io.hpp`

````````text
#pragma once

#include <cstddef>
#include <filesystem>
#include <memory>
#include <string>
#include <string_view>

#include "convert_to_biallelic/types.hpp"

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
    OutputSink(const std::filesystem::path& path,
               OutputFormat format,
               std::size_t io_workers);
    OutputSink(int owned_fd,
               const std::filesystem::path& display_path,
               OutputFormat format,
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

}  // namespace ctb

````````

## `src/progress.cpp`

````````text
#include "convert_to_biallelic/progress.hpp"

#include <algorithm>
#include <iomanip>
#include <limits>
#include <ostream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <system_error>
#include <utility>

#include "convert_to_biallelic/types.hpp"

namespace ctb {
namespace {

constexpr std::uint64_t kBytesPerMebibyte = 1024ULL * 1024ULL;

std::string format_elapsed(
    std::chrono::steady_clock::duration duration) {
    using Seconds = std::chrono::seconds;
    const auto elapsed = std::max(Seconds::zero(),
                                  std::chrono::duration_cast<Seconds>(duration));
    const auto total_seconds = elapsed.count();
    const auto hours = total_seconds / 3600;
    const auto minutes = (total_seconds / 60) % 60;
    const auto seconds = total_seconds % 60;

    std::ostringstream formatted;
    formatted << std::setfill('0') << std::setw(2) << hours << ':'
              << std::setw(2) << minutes << ':' << std::setw(2) << seconds;
    return formatted.str();
}

std::uint64_t records_per_second(
    std::uint64_t records,
    std::chrono::steady_clock::duration duration) noexcept {
    const long double seconds =
        std::chrono::duration<long double>(duration).count();
    if (records == 0 || seconds <= 0.0L) {
        return 0;
    }

    const long double rate = static_cast<long double>(records) / seconds;
    const long double maximum = static_cast<long double>(
        std::numeric_limits<std::uint64_t>::max());
    return rate >= maximum ? std::numeric_limits<std::uint64_t>::max()
                           : static_cast<std::uint64_t>(rate);
}

std::string periodic_line(
    std::chrono::steady_clock::duration elapsed,
    std::uint64_t input_records,
    std::uint64_t output_records,
    std::uint64_t recent_rate,
    std::uint64_t tracked_memory) {
    std::ostringstream line;
    line << '[' << format_elapsed(elapsed) << "] input=" << input_records
         << " output=" << output_records << " rate=" << recent_rate
         << " records/s memory=" << tracked_memory / kBytesPerMebibyte
         << " MiB";
    return line.str();
}

}  // namespace

std::string format_final_summary(const PipelineStats& stats) {
    std::ostringstream line;
    line << "Finished: input=" << stats.input_records
         << " output=" << stats.output_records
         << " elapsed=" << format_elapsed(stats.elapsed)
         << " average=" << records_per_second(stats.input_records,
                                                stats.elapsed)
         << " records/s";
    return line.str();
}

ProgressReporter::ProgressReporter(ProgressCounters& counters,
                                   std::chrono::milliseconds interval,
                                   std::ostream& output,
                                   bool quiet,
                                   std::function<void(std::exception_ptr)>
                                       error_callback)
    : counters_(counters),
      interval_(interval),
      output_(output),
      quiet_(quiet),
      error_callback_(std::move(error_callback)) {}

ProgressReporter::~ProgressReporter() {
    stop_without_summary();
}

void ProgressReporter::start() {
    std::lock_guard<std::mutex> lock(mutex_);
    if (state_ != State::idle) {
        throw std::logic_error("Progress reporter has already been started");
    }

    started_ = std::chrono::steady_clock::now();
    stopping_ = false;
    thread_error_ = nullptr;
    thread_exited_ = true;
    state_ = State::running;

    if (quiet_ || interval_ <= std::chrono::milliseconds::zero()) {
        return;
    }

    try {
        thread_exited_ = false;
        thread_ = std::thread(&ProgressReporter::run, this);
    } catch (...) {
        state_ = State::idle;
        stopping_ = false;
        thread_exited_ = true;
        throw;
    }
}

std::chrono::steady_clock::duration ProgressReporter::finish() {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (state_ != State::running) {
            throw std::logic_error(
                "Progress reporter is not running or was already finished");
        }
        if (thread_.joinable() &&
            thread_.get_id() == std::this_thread::get_id()) {
            throw std::logic_error(
                "Progress reporter cannot be finished from its own thread");
        }
        state_ = State::finishing;
        stopping_ = true;
    }
    changed_.notify_all();
    join_thread_safely();

    const auto elapsed = std::chrono::steady_clock::now() - started_;
    std::exception_ptr thread_error;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        state_ = State::finished;
        thread_error = thread_error_;
    }

    if (thread_error) {
        std::rethrow_exception(thread_error);
    }
    return elapsed;
}

void ProgressReporter::stop_without_summary() noexcept {
    try {
        {
            std::lock_guard<std::mutex> lock(mutex_);
            if (state_ != State::running) {
                return;
            }
            stopping_ = true;
            if (thread_.joinable() &&
                thread_.get_id() == std::this_thread::get_id()) {
                // The internal thread cannot join itself. Leave the reporter
                // in `running` state so an owning/control thread can join it
                // after this callback returns and run() exits.
                changed_.notify_all();
                return;
            }
            state_ = State::stopped;
        }
        changed_.notify_all();
        join_thread_safely();
    } catch (...) {
        record_error(std::current_exception());
    }
}

void ProgressReporter::record_error(std::exception_ptr error) noexcept {
    if (!error) {
        return;
    }

    bool first = false;
    try {
        {
            std::lock_guard<std::mutex> lock(mutex_);
            if (!thread_error_) {
                thread_error_ = error;
                first = true;
            }
            stopping_ = true;
        }
        changed_.notify_all();
    } catch (...) {
        std::terminate();
    }

    if (first && error_callback_) {
        try {
            error_callback_(error);
        } catch (...) {
            // The original reporter failure remains the authoritative error.
            // Error callbacks are notifications and must not escape this
            // no-throw reporter boundary.
        }
    }
}

void ProgressReporter::join_thread_safely() noexcept {
    if (!thread_.joinable()) {
        return;
    }
    if (thread_.get_id() == std::this_thread::get_id()) {
        // The public lifecycle rejects self-finish and leaves self-stop for an
        // owning control thread. Destroying `this` from run() is unsupported
        // and cannot be made lifetime-safe by detaching an active thread.
        std::terminate();
    }

    try {
        thread_.join();
        return;
    } catch (const std::system_error&) {
        record_error(std::current_exception());
    } catch (...) {
        record_error(std::current_exception());
    }

    try {
        std::unique_lock<std::mutex> lock(mutex_);
        changed_.wait(lock, [this] { return thread_exited_; });
    } catch (...) {
        record_error(std::current_exception());
        // Detaching before run() exits would permit use-after-destruction.
        std::terminate();
    }

    // Retry a normal join after confirmed thread exit. Detach is used only if
    // the platform still rejects that join, and is then lifetime-safe because
    // run() has published its terminal state already.
    try {
        thread_.join();
        return;
    } catch (const std::system_error&) {
        record_error(std::current_exception());
    } catch (...) {
        record_error(std::current_exception());
    }

    if (thread_.joinable()) {
        try {
            thread_.detach();
        } catch (...) {
            record_error(std::current_exception());
            std::terminate();
        }
    }
}

void ProgressReporter::run() noexcept {
    try {
        auto previous_time = std::chrono::steady_clock::now();
        std::uint64_t previous_input =
            counters_.input_records.load(std::memory_order_relaxed);

        for (;;) {
            {
                std::unique_lock<std::mutex> lock(mutex_);
                if (changed_.wait_for(lock, interval_,
                                      [this] { return stopping_; })) {
                    break;
                }
            }

            const auto now = std::chrono::steady_clock::now();
            const std::uint64_t input =
                counters_.input_records.load(std::memory_order_relaxed);
            const std::uint64_t output =
                counters_.output_records.load(std::memory_order_relaxed);
            const std::uint64_t memory =
                counters_.tracked_memory.load(std::memory_order_relaxed);
            const std::uint64_t delta =
                input >= previous_input ? input - previous_input : 0;
            const std::uint64_t rate =
                records_per_second(delta, now - previous_time);

            output_ << periodic_line(now - started_, input, output, rate,
                                     memory)
                    << '\n'
                    << std::flush;
            if (!output_) {
                throw std::runtime_error(
                    "Failed to write periodic progress output");
            }
            previous_time = now;
            previous_input = input;
        }
    } catch (...) {
        record_error(std::current_exception());
    }

    try {
        std::unique_lock<std::mutex> lock(mutex_);
        thread_exited_ = true;
        // Publish completion only at native thread exit. A join fallback can
        // therefore detach only after this reporter has stopped using `this`.
        std::notify_all_at_thread_exit(changed_, std::move(lock));
    } catch (...) {
        std::terminate();
    }
}

}  // namespace ctb

````````

## `src/pipeline.cpp`

````````text
#include "convert_to_biallelic/pipeline.hpp"

#include <algorithm>
#include <atomic>
#include <condition_variable>
#include <exception>
#include <functional>
#include <limits>
#include <map>
#include <memory>
#include <mutex>
#include <ostream>
#include <stdexcept>
#include <string>
#include <system_error>
#include <thread>
#include <utility>
#include <vector>

#include "convert_to_biallelic/bounded_queue.hpp"
#include "convert_to_biallelic/converter.hpp"
#include "convert_to_biallelic/progress.hpp"

namespace ctb {
namespace {

constexpr std::uint64_t kMinimumMemoryLimit =
    64ULL * 1024ULL * 1024ULL;

std::uint64_t checked_size(std::size_t value, const char* description) {
    if (value > static_cast<std::size_t>(
                    std::numeric_limits<std::uint64_t>::max())) {
        throw std::overflow_error(std::string(description) + " is too large");
    }
    return static_cast<std::uint64_t>(value);
}

std::uint64_t checked_add(std::uint64_t left,
                          std::uint64_t right,
                          const char* description) {
    if (right > std::numeric_limits<std::uint64_t>::max() - left) {
        throw std::overflow_error(std::string(description) + " overflow");
    }
    return left + right;
}

std::uint64_t checked_multiply(std::uint64_t left,
                               std::uint64_t right,
                               const char* description) {
    if (left != 0 &&
        right > std::numeric_limits<std::uint64_t>::max() / left) {
        throw std::overflow_error(std::string(description) + " overflow");
    }
    return left * right;
}

std::uint64_t string_storage_bytes(const std::string& value) {
    return checked_add(checked_size(value.capacity(), "string capacity"), 1,
                       "string storage");
}

std::uint64_t vector_storage_bytes(
    const std::vector<std::string>& records) {
    return checked_multiply(checked_size(records.capacity(),
                                         "record vector capacity"),
                            sizeof(std::string),
                            "record vector storage");
}

std::uint64_t work_storage_bytes(const RawWorkChunk& chunk) {
    std::uint64_t bytes = vector_storage_bytes(chunk.records);
    for (const std::string& record : chunk.records) {
        bytes = checked_add(bytes, string_storage_bytes(record),
                            "work chunk storage");
    }
    return bytes;
}

std::uint64_t result_storage_bytes(const RawResultChunk& chunk) {
    return string_storage_bytes(chunk.bytes);
}

std::size_t planned_vector_capacity(std::size_t current,
                                    std::size_t required,
                                    std::size_t target) {
    if (required <= current) {
        return current;
    }
    if (required > target) {
        throw std::length_error("Work chunk record target was exceeded");
    }

    std::size_t capacity = current == 0 ? 1 : current;
    while (capacity < required) {
        if (capacity >= target || capacity > target - capacity) {
            capacity = target;
        } else {
            capacity *= 2;
        }
    }
    return capacity;
}

std::uint64_t projected_work_storage(const WorkChunk& chunk,
                                     const std::string& next_record,
                                     std::size_t target_records) {
    if (chunk.data.records.size() ==
        std::numeric_limits<std::size_t>::max()) {
        throw std::overflow_error("Work chunk record count overflow");
    }

    const std::size_t required = chunk.data.records.size() + 1;
    const std::size_t planned = planned_vector_capacity(
        chunk.data.records.capacity(), required, target_records);
    const std::uint64_t added_slots = checked_size(
        planned - chunk.data.records.capacity(),
        "additional record vector capacity");
    const std::uint64_t added_vector_bytes = checked_multiply(
        added_slots, sizeof(std::string), "record vector growth");

    return checked_add(
        checked_add(chunk.permit.bytes(), added_vector_bytes,
                    "projected work chunk storage"),
        string_storage_bytes(next_record), "projected work chunk storage");
}

void publish_memory(ProgressCounters& counters,
                    MemoryBudget& budget,
                    std::mutex& publication_mutex) {
    // Serialize the read-and-publish pair so a delayed publisher cannot store
    // a snapshot older than one already published by another pipeline thread.
    std::lock_guard<std::mutex> lock(publication_mutex);
    counters.tracked_memory.store(budget.current_bytes(),
                                  std::memory_order_relaxed);
}

void resize_result_permit(
    MemoryPermit& permit,
    std::uint64_t bytes,
    std::uint64_t sequence,
    std::uint64_t tracked_allowance,
    const std::atomic<std::uint64_t>& next_written_sequence,
    const std::atomic<bool>& pipeline_cancelled,
    ProgressCounters& counters,
    MemoryBudget& budget,
    std::mutex& coordination_mutex,
    std::condition_variable& memory_changed);

void append_work_record(WorkChunk& chunk,
                        std::string& record,
                        std::uint64_t line_number,
                        std::size_t target_records,
                        std::uint64_t tracked_allowance,
                        const std::atomic<std::uint64_t>&
                            next_written_sequence,
                        const std::atomic<bool>& pipeline_cancelled,
                        ProgressCounters& counters,
                        MemoryBudget& budget,
                        std::mutex& publication_mutex,
                        std::condition_variable& memory_changed) {
    const std::size_t required = chunk.data.records.size() + 1;
    const std::size_t planned = planned_vector_capacity(
        chunk.data.records.capacity(), required, target_records);
    const std::uint64_t planned_bytes = projected_work_storage(
        chunk, record, target_records);

    // Reserve the permit before every planned vector or string-capacity
    // transfer into pipeline-owned storage.
    resize_result_permit(
        chunk.permit, planned_bytes, chunk.data.sequence,
        tracked_allowance, next_written_sequence, pipeline_cancelled,
        counters, budget, publication_mutex, memory_changed);
    if (planned > chunk.data.records.capacity()) {
        chunk.data.records.reserve(planned);
    }

    // The standard permits reserve() to choose a larger capacity than the
    // request. Reconcile that implementation-selected capacity before the
    // record string itself is moved into the vector.
    const std::uint64_t before_move = checked_add(
        work_storage_bytes(chunk.data), string_storage_bytes(record),
        "work chunk storage before record move");
    resize_result_permit(
        chunk.permit, before_move, chunk.data.sequence,
        tracked_allowance, next_written_sequence, pipeline_cancelled,
        counters, budget, publication_mutex, memory_changed);

    if (chunk.data.records.empty()) {
        chunk.data.first_line_number = line_number;
    }
    chunk.data.records.push_back(std::move(record));

    // A nothrow string move normally transfers its capacity. Reconcile the
    // actual capacity so the permit remains exact even for an SSO move.
    resize_result_permit(
        chunk.permit, work_storage_bytes(chunk.data), chunk.data.sequence,
        tracked_allowance, next_written_sequence, pipeline_cancelled,
        counters, budget, publication_mutex, memory_changed);
}

void checked_atomic_add(std::atomic<std::uint64_t>& counter,
                        std::uint64_t additional,
                        const char* description) {
    std::uint64_t current = counter.load(std::memory_order_relaxed);
    for (;;) {
        if (additional >
            std::numeric_limits<std::uint64_t>::max() - current) {
            throw std::overflow_error(std::string(description) + " overflow");
        }
        if (counter.compare_exchange_weak(
                current, current + additional, std::memory_order_relaxed,
                std::memory_order_relaxed)) {
            return;
        }
    }
}

std::uint64_t physical_line_number(std::uint64_t first,
                                   std::size_t offset) {
    const std::uint64_t converted_offset =
        checked_size(offset, "record line offset");
    return checked_add(first, converted_offset, "physical line number");
}

void resize_result_permit(
    MemoryPermit& permit,
    std::uint64_t bytes,
    std::uint64_t sequence,
    std::uint64_t tracked_allowance,
    const std::atomic<std::uint64_t>& next_written_sequence,
    const std::atomic<bool>& pipeline_cancelled,
    ProgressCounters& counters,
    MemoryBudget& budget,
    std::mutex& coordination_mutex,
    std::condition_variable& memory_changed) {
    if (permit.bytes() == bytes) {
        return;
    }

    std::unique_lock<std::mutex> lock(coordination_mutex);
    for (;;) {
        if (pipeline_cancelled.load(std::memory_order_acquire)) {
            throw std::runtime_error("Pipeline was cancelled");
        }

        const bool shrinking = bytes <= permit.bytes();
        const std::uint64_t current = budget.current_bytes();
        const std::uint64_t additional =
            shrinking ? 0 : bytes - permit.bytes();
        const bool fits_normally =
            shrinking ||
            (current <= tracked_allowance &&
             additional <= tracked_allowance - current);
        const std::uint64_t next =
            next_written_sequence.load(std::memory_order_acquire);
        if (next > sequence) {
            throw std::logic_error(
                "Result sequence advanced before conversion completed");
        }
        const bool is_next_sequence = next == sequence;

        if (fits_normally || is_next_sequence) {
            // A later sequence may use only normal capacity. The next result
            // required by the writer may claim or continue the sole overage,
            // so an over-budget result can never wait in the reorder map while
            // blocking an earlier sequence from finishing.
            permit.resize(bytes, is_next_sequence);
            counters.tracked_memory.store(budget.current_bytes(),
                                          std::memory_order_relaxed);
            lock.unlock();
            memory_changed.notify_all();
            return;
        }

        memory_changed.wait(lock);
    }
}

ResultChunk convert_chunk(WorkChunk& work,
                          const AnnotationIndex& annotation,
                          std::uint64_t tracked_allowance,
                          const std::atomic<std::uint64_t>&
                              next_written_sequence,
                          const std::atomic<bool>& pipeline_cancelled,
                          ProgressCounters& counters,
                          MemoryBudget& budget,
                          std::mutex& memory_publication_mutex,
                          std::condition_variable& memory_changed) {
    const std::uint64_t input_records =
        checked_size(work.data.records.size(), "input record count");
    const std::uint64_t input_storage = work_storage_bytes(work.data);
    const std::uint64_t sequence = work.data.sequence;
    resize_result_permit(
        work.permit, input_storage, sequence, tracked_allowance,
        next_written_sequence, pipeline_cancelled, counters, budget,
        memory_publication_mutex, memory_changed);

    RawResultChunk converted_chunk;
    converted_chunk.sequence = sequence;
    converted_chunk.input_records = input_records;

    resize_result_permit(
        work.permit,
        checked_add(input_storage, result_storage_bytes(converted_chunk),
                    "combined input and result storage"),
        sequence, tracked_allowance, next_written_sequence,
        pipeline_cancelled, counters, budget, memory_publication_mutex,
        memory_changed);

    const std::function<void(std::size_t, std::size_t)> before_reserve =
        [&](std::size_t target_output_capacity,
            std::size_t target_scratch_bytes) {
            const std::uint64_t planned_result = checked_add(
                checked_size(target_output_capacity,
                             "planned result capacity"),
                1, "planned result storage");
            const std::uint64_t planned_scratch = checked_size(
                target_scratch_bytes, "planned converter scratch size");
            resize_result_permit(
                work.permit,
                checked_add(
                    checked_add(input_storage, planned_result,
                                "combined input and result storage"),
                    planned_scratch,
                    "combined input, result, and converter scratch storage"),
                sequence, tracked_allowance, next_written_sequence,
                pipeline_cancelled, counters, budget,
                memory_publication_mutex, memory_changed);
        };

    for (std::size_t index = 0; index < work.data.records.size(); ++index) {
        append_converted_record(
            work.data.records[index], annotation,
            physical_line_number(work.data.first_line_number, index),
            converted_chunk.bytes, converted_chunk.output_records,
            before_reserve);
    }

    // Release source storage before shrinking its reservation away. The
    // remaining permit then describes only the result string capacity.
    work.data = RawWorkChunk{};
    ResultChunk result;
    result.data = std::move(converted_chunk);
    result.permit = std::move(work.permit);
    resize_result_permit(
        result.permit, result_storage_bytes(result.data), sequence,
        tracked_allowance, next_written_sequence, pipeline_cancelled, counters,
        budget, memory_publication_mutex, memory_changed);
    checked_atomic_add(counters.input_records, input_records,
                       "pipeline input record counter");
    return result;
}

class PipelineState {
public:
    bool capture_failure(std::exception_ptr error) noexcept {
        bool first = false;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            if (!first_error_) {
                first_error_ = error;
                cancelled_ = true;
                first = true;
            }
        }
        changed_.notify_all();
        return first;
    }

    std::exception_ptr first_error() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return first_error_;
    }

    bool cancelled() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return cancelled_;
    }

    bool wait_until_written(std::uint64_t count) {
        std::unique_lock<std::mutex> lock(mutex_);
        changed_.wait(lock, [this, count] {
            return cancelled_ || written_chunks_ >= count;
        });
        return !cancelled_;
    }

    void mark_written(std::uint64_t count) {
        {
            std::lock_guard<std::mutex> lock(mutex_);
            if (count < written_chunks_) {
                throw std::logic_error(
                    "Written chunk sequence moved backwards");
            }
            written_chunks_ = count;
        }
        changed_.notify_all();
    }

private:
    mutable std::mutex mutex_;
    std::condition_variable changed_;
    std::exception_ptr first_error_;
    std::uint64_t written_chunks_ = 0;
    bool cancelled_ = false;
};

class ThreadCompletion {
public:
    void mark_exited() noexcept {
        try {
            std::unique_lock<std::mutex> lock(mutex_);
            exited_ = true;
            // Keep the completion mutex locked until the native thread has
            // actually exited. A fallback waiter therefore cannot detach on
            // an about-to-exit signal while wrapper epilogue code is active.
            std::notify_all_at_thread_exit(changed_, std::move(lock));
        } catch (...) {
            // Without publishing completion, a failed join cannot safely
            // recover while pipeline threads still reference stack state.
            std::terminate();
        }
    }

    void wait_until_exited() {
        std::unique_lock<std::mutex> lock(mutex_);
        changed_.wait(lock, [this] { return exited_; });
    }

private:
    std::mutex mutex_;
    std::condition_variable changed_;
    bool exited_ = false;
};

class WorkerLifecycle {
public:
    void add_starting_worker() {
        std::lock_guard<std::mutex> lock(mutex_);
        if (spawning_finished_) {
            throw std::logic_error(
                "Cannot add a worker after spawning has finished");
        }
        if (active_workers_ ==
            std::numeric_limits<std::size_t>::max()) {
            throw std::overflow_error("Active worker count overflow");
        }
        ++active_workers_;
    }

    void rollback_unstarted_worker() {
        std::lock_guard<std::mutex> lock(mutex_);
        if (active_workers_ == 0) {
            throw std::logic_error(
                "Cannot roll back an absent starting worker");
        }
        --active_workers_;
    }

    bool worker_exited_and_should_close() {
        std::lock_guard<std::mutex> lock(mutex_);
        if (active_workers_ == 0) {
            throw std::logic_error(
                "Active worker count underflow");
        }
        --active_workers_;
        return claim_result_close_locked();
    }

    bool finish_spawning_and_should_close() {
        std::lock_guard<std::mutex> lock(mutex_);
        spawning_finished_ = true;
        return claim_result_close_locked();
    }

private:
    bool claim_result_close_locked() noexcept {
        if (!spawning_finished_ || active_workers_ != 0 ||
            result_close_claimed_) {
            return false;
        }
        result_close_claimed_ = true;
        return true;
    }

    std::mutex mutex_;
    std::size_t active_workers_ = 0;
    bool spawning_finished_ = false;
    bool result_close_claimed_ = false;
};

std::size_t queue_capacity(std::size_t conversion_workers) noexcept {
    const std::size_t maximum = std::numeric_limits<std::size_t>::max();
    if (conversion_workers > maximum / 2) {
        return maximum;
    }
    return std::max<std::size_t>(1, conversion_workers * 2);
}

std::uint64_t validate_and_calculate_allowance(
    const AnnotationIndex& annotation,
    const PipelineOptions& options) {
    if (options.threads.conversion_workers == 0) {
        throw std::invalid_argument(
            "Pipeline requires at least one conversion worker");
    }
    if (options.target_records_per_chunk == 0) {
        throw std::invalid_argument(
            "Pipeline record target must be positive");
    }
    if (options.target_bytes_per_chunk == 0) {
        throw std::invalid_argument(
            "Pipeline byte target must be positive");
    }
    if (options.memory_limit_bytes < kMinimumMemoryLimit) {
        throw std::invalid_argument(
            "Pipeline memory limit must be at least 64 MiB");
    }

    const std::uint64_t reserve = options.memory_limit_bytes / 10;
    const std::uint64_t after_reserve =
        options.memory_limit_bytes - reserve;
    if (annotation.estimated_bytes() >= after_reserve) {
        throw std::invalid_argument(
            "Annotation estimate plus the 10% reserve must be below the memory limit");
    }
    return after_reserve - annotation.estimated_bytes();
}

bool read_physical_line(InputSource& input,
                        std::string& line,
                        std::uint64_t& line_number) {
    if (!input.getline(line)) {
        return false;
    }
    if (line_number == std::numeric_limits<std::uint64_t>::max()) {
        throw std::overflow_error("Physical input line number overflow");
    }
    ++line_number;
    return true;
}

}  // namespace

PipelineStats run_pipeline(InputSource& input,
                           OutputSink& output,
                           const AnnotationIndex& annotation,
                           const PipelineOptions& options,
                           std::ostream& progress,
                           std::ostream& diagnostics) {
    if (std::addressof(progress) == std::addressof(diagnostics) ||
        progress.rdbuf() == diagnostics.rdbuf()) {
        throw std::invalid_argument(
            "Progress and diagnostics streams must not share an object or "
            "stream buffer");
    }

    const std::uint64_t tracked_allowance =
        validate_and_calculate_allowance(annotation, options);

    // Header I/O is deliberately complete before any conversion, writer, or
    // reporter thread is started. The first non-header line is retained for
    // the streaming reader below.
    std::uint64_t line_number = 0;
    std::uint64_t header_output_bytes = 0;
    std::string line;
    std::string first_data_line;
    std::uint64_t first_data_line_number = 0;
    bool has_first_data_line = false;

    while (read_physical_line(input, line, line_number)) {
        if (!line.empty() && line.front() == '#') {
            const auto converted = convert_header(line);
            if (converted) {
                const std::uint64_t updated_header_bytes = checked_add(
                    header_output_bytes,
                    checked_size(converted->size(), "header output size"),
                    "header output byte counter");
                output.write(*converted);
                header_output_bytes = updated_header_bytes;
            }
            continue;
        }

        first_data_line = std::move(line);
        first_data_line_number = line_number;
        has_first_data_line = true;
        break;
    }

    ProgressCounters counters;
    MemoryBudget memory_budget(tracked_allowance);
    const std::size_t capacity =
        queue_capacity(options.threads.conversion_workers);
    BoundedQueue<WorkChunk> work_queue(capacity);
    BoundedQueue<ResultChunk> result_queue(capacity);
    PipelineState state;
    std::mutex memory_publication_mutex;
    std::condition_variable memory_changed;
    std::atomic<std::uint64_t> submitted_chunks{0};
    std::atomic<std::uint64_t> next_written_sequence{0};
    std::atomic<std::uint64_t> output_bytes{header_output_bytes};
    std::atomic<bool> pipeline_cancelled{false};
    WorkerLifecycle worker_lifecycle;

    auto record_failure = [&](std::exception_ptr error) noexcept {
        if (error && state.capture_failure(error)) {
            // Do not hold the failure-state mutex while entering any memory or
            // queue operation. Each cancellation path wakes its waiters.
            pipeline_cancelled.store(true, std::memory_order_release);
            memory_budget.cancel();
            memory_changed.notify_all();
            work_queue.cancel();
            result_queue.cancel();
        }
    };

    ProgressReporter reporter(
        counters, options.progress_interval, progress, options.quiet,
        [&](std::exception_ptr error) noexcept { record_failure(error); });

    auto writer_body = [&] {
        try {
            std::map<std::uint64_t, ResultChunk> pending;
            std::uint64_t next_sequence = 0;
            ResultChunk incoming;

            while (result_queue.pop(incoming)) {
                const std::uint64_t sequence = incoming.data.sequence;
                if (sequence < next_sequence) {
                    throw std::runtime_error(
                        "Duplicate result sequence " +
                        std::to_string(sequence));
                }

                const auto insertion =
                    pending.emplace(sequence, std::move(incoming));
                if (!insertion.second) {
                    throw std::runtime_error(
                        "Duplicate result sequence " +
                        std::to_string(sequence));
                }

                for (;;) {
                    auto ready = pending.find(next_sequence);
                    if (ready == pending.end()) {
                        break;
                    }

                    const std::uint64_t output_record_total = checked_add(
                        counters.output_records.load(
                            std::memory_order_relaxed),
                        ready->second.data.output_records,
                        "pipeline output record counter");
                    const std::uint64_t output_byte_total = checked_add(
                        output_bytes.load(std::memory_order_relaxed),
                        checked_size(ready->second.data.bytes.size(),
                                     "result output size"),
                        "pipeline output byte counter");

                    output.write(ready->second.data.bytes);
                    counters.output_records.store(output_record_total,
                                                  std::memory_order_relaxed);
                    output_bytes.store(output_byte_total,
                                       std::memory_order_relaxed);

                    if (next_sequence ==
                        std::numeric_limits<std::uint64_t>::max()) {
                        throw std::overflow_error(
                            "Writer result sequence overflow");
                    }
                    ++next_sequence;
                    pending.erase(ready);
                    publish_memory(counters, memory_budget,
                                   memory_publication_mutex);
                    next_written_sequence.store(next_sequence,
                                                std::memory_order_release);
                    memory_changed.notify_all();
                    state.mark_written(next_sequence);
                }
            }

            const std::uint64_t expected =
                submitted_chunks.load(std::memory_order_acquire);
            if (!pending.empty() || next_sequence != expected) {
                throw std::runtime_error(
                    "Result queue closed with a missing sequence: expected " +
                    std::to_string(next_sequence) + " of " +
                    std::to_string(expected));
            }
        } catch (...) {
            record_failure(std::current_exception());
        }
    };

    auto worker_body = [&] {
        try {
            WorkChunk work;
            while (work_queue.pop(work)) {
                ResultChunk result = convert_chunk(
                    work, annotation, tracked_allowance,
                    next_written_sequence, pipeline_cancelled, counters,
                    memory_budget, memory_publication_mutex, memory_changed);
                result_queue.push(std::move(result));
            }
        } catch (...) {
            record_failure(std::current_exception());
        }

        bool should_close_results = false;
        try {
            should_close_results =
                worker_lifecycle.worker_exited_and_should_close();
        } catch (...) {
            record_failure(std::current_exception());
        }

        if (should_close_results) {
            try {
                result_queue.close();
            } catch (...) {
                record_failure(std::current_exception());
            }
        }
    };

    std::thread writer_thread;
    std::vector<std::thread> worker_threads;
    std::shared_ptr<ThreadCompletion> writer_completion;
    std::vector<std::shared_ptr<ThreadCompletion>> worker_completions;

    auto join_safely = [&](
                           std::thread& thread,
                           const std::shared_ptr<ThreadCompletion>& completion)
        noexcept {
        if (!thread.joinable()) {
            return;
        }
        if (thread.get_id() == std::this_thread::get_id() || !completion) {
            // run_pipeline owns and joins these threads only from its reader
            // context. Detaching an active self-thread would invalidate every
            // reference capture, so fail closed if that invariant is broken.
            std::terminate();
        }

        try {
            thread.join();
            return;
        } catch (const std::system_error&) {
            record_failure(std::current_exception());
        } catch (...) {
            record_failure(std::current_exception());
        }

        try {
            completion->wait_until_exited();
        } catch (...) {
            record_failure(std::current_exception());
            // Stack-captured pipeline state must outlive every active thread.
            // A wait failure therefore cannot fall through to active detach.
            std::terminate();
        }

        // Once the wrapper has published completion, none of its stack
        // references can be used again. Retry join, then detach only the
        // already-exited native thread as a last-resort handle cleanup.
        try {
            thread.join();
            return;
        } catch (const std::system_error&) {
            record_failure(std::current_exception());
        } catch (...) {
            record_failure(std::current_exception());
        }

        if (thread.joinable()) {
            try {
                thread.detach();
            } catch (...) {
                record_failure(std::current_exception());
                std::terminate();
            }
        }
    };

    auto finish_worker_spawning = [&]() noexcept {
        try {
            if (worker_lifecycle.finish_spawning_and_should_close()) {
                result_queue.close();
            }
        } catch (...) {
            record_failure(std::current_exception());
        }
    };

    try {
        reporter.start();
        writer_completion = std::make_shared<ThreadCompletion>();
        writer_thread = std::thread(
            [&, completion = writer_completion]() noexcept {
                try {
                    writer_body();
                } catch (...) {
                    record_failure(std::current_exception());
                }
                completion->mark_exited();
            });
        worker_threads.reserve(options.threads.conversion_workers);
        worker_completions.reserve(options.threads.conversion_workers);
        for (std::size_t remaining =
                 options.threads.conversion_workers;
             remaining != 0; --remaining) {
            if (state.cancelled()) {
                break;
            }

            auto completion = std::make_shared<ThreadCompletion>();
            worker_completions.push_back(completion);
            try {
                worker_lifecycle.add_starting_worker();
            } catch (...) {
                worker_completions.pop_back();
                throw;
            }
            try {
                worker_threads.emplace_back(
                    [&, completion = std::move(completion)]() noexcept {
                        try {
                            worker_body();
                        } catch (...) {
                            record_failure(std::current_exception());
                        }
                        completion->mark_exited();
                    });
            } catch (...) {
                const std::exception_ptr creation_error =
                    std::current_exception();
                worker_completions.pop_back();
                record_failure(creation_error);
                try {
                    worker_lifecycle.rollback_unstarted_worker();
                } catch (...) {
                    record_failure(std::current_exception());
                }
                throw;
            }
        }
        finish_worker_spawning();
        if (state.cancelled()) {
            throw std::runtime_error(
                "Pipeline was cancelled during worker creation");
        }

        WorkChunk chunk;
        std::uint64_t chunk_input_bytes = 0;
        std::uint64_t next_sequence = 0;
        bool warned_about_input_overage = false;

        auto begin_chunk = [&] {
            if (next_sequence ==
                std::numeric_limits<std::uint64_t>::max()) {
                throw std::overflow_error("Reader chunk sequence overflow");
            }
            chunk = WorkChunk{};
            chunk.data.sequence = next_sequence;
            chunk.permit = memory_budget.acquire(0, false);
            publish_memory(counters, memory_budget,
                           memory_publication_mutex);
        };

        auto submit_chunk = [&] {
            if (chunk.data.records.empty()) {
                return;
            }
            work_queue.push(std::move(chunk));
            ++next_sequence;
            submitted_chunks.store(next_sequence, std::memory_order_release);
            chunk = WorkChunk{};
            chunk_input_bytes = 0;
        };

        auto process_data_line = [&](std::string& record,
                                     std::uint64_t record_line_number) {
            if (!record.empty() && record.front() == '#') {
                throw std::runtime_error(
                    "Input line " + std::to_string(record_line_number) +
                    ": header line encountered after data began");
            }

            const std::uint64_t record_bytes =
                checked_size(record.size(), "input record size");
            if (chunk.permit.bytes() == 0 && chunk.data.records.empty()) {
                begin_chunk();
            }

            std::uint64_t projected = projected_work_storage(
                chunk, record, options.target_records_per_chunk);
            const bool exceeds_byte_target =
                record_bytes > options.target_bytes_per_chunk ||
                chunk_input_bytes >
                    options.target_bytes_per_chunk -
                        std::min(record_bytes,
                                 options.target_bytes_per_chunk);
            const bool exceeds_memory_target =
                projected > tracked_allowance;

            if (!chunk.data.records.empty() &&
                (exceeds_byte_target || exceeds_memory_target)) {
                submit_chunk();
                begin_chunk();
                projected = projected_work_storage(
                    chunk, record, options.target_records_per_chunk);
            }

            const bool single_record_overage =
                chunk.data.records.empty() && projected > tracked_allowance;
            if (single_record_overage) {
                // An input overage must never sit behind older work while it
                // owns the sole overage slot. Drain all prior sequences first,
                // then the worker that pops this chunk can grow the same permit
                // without deadlocking other workers.
                if (!state.wait_until_written(next_sequence)) {
                    throw std::runtime_error("Pipeline was cancelled");
                }
                if (!warned_about_input_overage) {
                    diagnostics
                        << "Warning: input line " << record_line_number
                        << " exceeds the tracked pipeline memory allowance; "
                           "permitting one overage chunk"
                        << '\n'
                        << std::flush;
                    if (!diagnostics) {
                        throw std::runtime_error(
                            "Failed to write pipeline diagnostics");
                    }
                    warned_about_input_overage = true;
                }
            }

            append_work_record(
                chunk, record, record_line_number,
                options.target_records_per_chunk, tracked_allowance,
                next_written_sequence, pipeline_cancelled, counters,
                memory_budget, memory_publication_mutex, memory_changed);
            chunk_input_bytes = checked_add(
                chunk_input_bytes, record_bytes, "chunk input byte count");

            if (chunk.data.records.size() >=
                    options.target_records_per_chunk ||
                chunk_input_bytes >= options.target_bytes_per_chunk ||
                single_record_overage) {
                submit_chunk();
            }
        };

        if (has_first_data_line) {
            process_data_line(first_data_line, first_data_line_number);
        }
        while (read_physical_line(input, line, line_number)) {
            process_data_line(line, line_number);
        }
        submit_chunk();
        work_queue.close();
    } catch (...) {
        record_failure(std::current_exception());
    }
    finish_worker_spawning();

    for (std::size_t index = 0; index < worker_threads.size(); ++index) {
        join_safely(worker_threads[index], worker_completions[index]);
    }
    join_safely(writer_thread, writer_completion);

    if (const std::exception_ptr error = state.first_error()) {
        reporter.stop_without_summary();
        std::rethrow_exception(error);
    }

    try {
        publish_memory(counters, memory_budget, memory_publication_mutex);
        PipelineStats stats;
        stats.input_records =
            counters.input_records.load(std::memory_order_relaxed);
        stats.output_records =
            counters.output_records.load(std::memory_order_relaxed);
        stats.output_bytes = output_bytes.load(std::memory_order_relaxed);
        stats.peak_tracked_bytes = memory_budget.peak_bytes();
        stats.elapsed = reporter.finish();
        return stats;
    } catch (...) {
        reporter.stop_without_summary();
        throw;
    }
}

}  // namespace ctb

````````

## `src/output_transaction.cpp`

````````text
#ifndef _WIN32
#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#endif

#include "convert_to_biallelic/output_transaction.hpp"

#include <atomic>
#include <cerrno>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>
#include <stdexcept>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

#ifdef _WIN32
#define NOMINMAX
#include <fcntl.h>
#include <io.h>
#include <windows.h>
#elif defined(__linux__)
#include <fcntl.h>
#include <stdio.h>
#include <sys/stat.h>
#include <unistd.h>
#else
#error "OutputTransaction supports only Windows and Linux"
#endif

namespace ctb {
namespace {

std::atomic<std::uint64_t> temporary_counter{0};
constexpr std::uint64_t kMaximumCreationAttempts = 1024ULL * 1024ULL;

std::string display_path(const std::filesystem::path& path) {
    return path.u8string();
}

std::string system_message(unsigned long error) {
#ifdef _WIN32
    return std::system_category().message(static_cast<int>(error));
#elif defined(__linux__)
    return std::generic_category().message(static_cast<int>(error));
#endif
}

std::string errno_message(int error) {
    return std::generic_category().message(error);
}

[[noreturn]] void throw_path_error(
    std::string_view operation,
    const std::filesystem::path& source,
    const std::filesystem::path& destination,
    unsigned long error) {
    throw std::runtime_error(
        std::string(operation) + " from '" + display_path(source) +
        "' to '" + display_path(destination) + "': " +
        system_message(error));
}

[[noreturn]] void throw_filesystem_error(
    std::string_view operation,
    const std::filesystem::path& path,
    const std::filesystem::path& destination,
    const std::error_code& error) {
    throw std::runtime_error(std::string(operation) + " '" +
                             display_path(path) + "' for destination '" +
                             display_path(destination) + "': " +
                             error.message());
}

void append_ascii(std::filesystem::path::string_type& destination,
                  std::string_view ascii) {
    for (const char character : ascii) {
        destination.push_back(
            static_cast<std::filesystem::path::value_type>(character));
    }
}

std::uint64_t process_id() noexcept {
#ifdef _WIN32
    return static_cast<std::uint64_t>(::GetCurrentProcessId());
#elif defined(__linux__)
    return static_cast<std::uint64_t>(::getpid());
#endif
}

std::filesystem::path make_candidate_name(
    const std::filesystem::path& filename,
    std::uint64_t counter,
    std::string_view role = {}) {
    std::filesystem::path::string_type name;
    name.push_back(static_cast<std::filesystem::path::value_type>('.'));
    name += filename.native();
    append_ascii(name, ".ctb.");
    if (!role.empty()) {
        append_ascii(name, role);
        name.push_back(static_cast<std::filesystem::path::value_type>('.'));
    }
    append_ascii(name, std::to_string(process_id()));
    name.push_back(static_cast<std::filesystem::path::value_type>('.'));
    append_ascii(name, std::to_string(counter));
    append_ascii(name, ".tmp");
    return std::filesystem::path(std::move(name));
}

std::filesystem::path absolute_destination(
    const std::filesystem::path& destination) {
    std::error_code error;
    std::filesystem::path absolute =
        std::filesystem::absolute(destination, error);
    if (error) {
        throw_filesystem_error("Failed to resolve output destination",
                               destination, destination, error);
    }
    return absolute.lexically_normal();
}

void validate_parent(const std::filesystem::path& parent,
                     const std::filesystem::path& destination) {
    std::error_code error;
    const std::filesystem::file_status status =
        std::filesystem::status(parent, error);
    if (error) {
        throw_filesystem_error("Failed to inspect output parent directory",
                               parent, destination, error);
    }
    if (!std::filesystem::is_directory(status)) {
        throw std::invalid_argument(
            "Output parent '" + display_path(parent) +
            "' for destination '" + display_path(destination) +
            "' is not an existing directory");
    }
}

#ifdef _WIN32
bool windows_path_entry_exists(const std::filesystem::path& path) {
    std::error_code error;
    const std::filesystem::file_status status =
        std::filesystem::symlink_status(path, error);
    if (error) {
        throw_filesystem_error("Failed to inspect output destination", path,
                               path, error);
    }
    if (status.type() == std::filesystem::file_type::not_found) {
        return false;
    }
    if (!std::filesystem::status_known(status)) {
        throw std::runtime_error(
            "Output destination has unknown filesystem status: '" +
            display_path(path) + "'");
    }
    return true;
}

bool same_windows_identity(const BY_HANDLE_FILE_INFORMATION& left,
                           const BY_HANDLE_FILE_INFORMATION& right) noexcept {
    return left.dwVolumeSerialNumber == right.dwVolumeSerialNumber &&
           left.nFileIndexHigh == right.nFileIndexHigh &&
           left.nFileIndexLow == right.nFileIndexLow;
}

void verify_windows_path_identity(
    HANDLE retained,
    const std::filesystem::path& temporary,
    const std::filesystem::path& destination) {
    BY_HANDLE_FILE_INFORMATION retained_info{};
    if (::GetFileInformationByHandle(retained, &retained_info) == 0) {
        const DWORD error = ::GetLastError();
        throw_path_error("Failed to inspect retained temporary output identity",
                         temporary, destination, error);
    }

    HANDLE path_handle = ::CreateFileW(
        temporary.c_str(), FILE_READ_ATTRIBUTES,
        FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
        nullptr, OPEN_EXISTING,
        FILE_ATTRIBUTE_NORMAL | FILE_FLAG_OPEN_REPARSE_POINT, nullptr);
    if (path_handle == INVALID_HANDLE_VALUE) {
        const DWORD error = ::GetLastError();
        throw_path_error("Failed to open temporary output path for identity verification",
                         temporary, destination, error);
    }

    BY_HANDLE_FILE_INFORMATION path_info{};
    if (::GetFileInformationByHandle(path_handle, &path_info) == 0) {
        const DWORD inspect_error = ::GetLastError();
        if (::CloseHandle(path_handle) == 0) {
            const DWORD close_error = ::GetLastError();
            throw std::runtime_error(
                "Failed to inspect temporary output path identity '" +
                display_path(temporary) + "' for destination '" +
                display_path(destination) + "': " +
                system_message(inspect_error) +
                "; verification-handle cleanup also failed: " +
                system_message(close_error));
        }
        throw_path_error("Failed to inspect temporary output path identity",
                         temporary, destination, inspect_error);
    }
    if (::CloseHandle(path_handle) == 0) {
        const DWORD error = ::GetLastError();
        throw_path_error("Failed to close temporary identity verification handle",
                         temporary, destination, error);
    }
    if (!same_windows_identity(retained_info, path_info)) {
        throw std::runtime_error(
            "Temporary output path no longer names the retained file identity: '" +
            display_path(temporary) + "' for destination '" +
            display_path(destination) + "'");
    }
}
#endif

#ifdef __linux__
bool tmpfile_is_unsupported(int error) noexcept {
    return error == EOPNOTSUPP || error == EISDIR || error == ENOENT ||
           error == EINVAL;
}

bool same_linux_identity(const struct stat& left,
                         const struct stat& right) noexcept {
    return left.st_dev == right.st_dev && left.st_ino == right.st_ino;
}

bool linux_named_identity_matches(
    int retained_fd,
    int directory_fd,
    const std::filesystem::path& temporary_name,
    const std::filesystem::path& temporary,
    const std::filesystem::path& destination) {
    struct stat retained_status {};
    if (::fstat(retained_fd, &retained_status) != 0) {
        const int error = errno;
        throw_path_error("Failed to inspect retained Linux output identity",
                         temporary, destination,
                         static_cast<unsigned long>(error));
    }

    struct stat path_status {};
    if (::fstatat(directory_fd, temporary_name.c_str(), &path_status,
                  AT_SYMLINK_NOFOLLOW) != 0) {
        const int error = errno;
        throw_path_error("Failed to inspect linked temporary output identity",
                         temporary, destination,
                         static_cast<unsigned long>(error));
    }
    return same_linux_identity(retained_status, path_status);
}

struct LinkIdentityResult {
    bool linked = false;
    int error = 0;
    int empty_path_error = 0;
    bool procfs_attempted = false;
};

bool should_try_procfs_link(int error) noexcept {
    return error == ENOENT || error == EPERM || error == EACCES ||
           error == EINVAL;
}

LinkIdentityResult link_anonymous_identity(
    int retained_fd,
    int directory_fd,
    const std::filesystem::path& destination_name) {
    if (::linkat(retained_fd, "", directory_fd,
                 destination_name.c_str(), AT_EMPTY_PATH) == 0) {
        return LinkIdentityResult{true, 0, 0, false};
    }

    const int empty_path_error = errno;
    if (!should_try_procfs_link(empty_path_error)) {
        return LinkIdentityResult{false, empty_path_error,
                                  empty_path_error, false};
    }

    const std::string proc_path =
        "/proc/self/fd/" + std::to_string(retained_fd);
    if (::linkat(AT_FDCWD, proc_path.c_str(), directory_fd,
                 destination_name.c_str(), AT_SYMLINK_FOLLOW) == 0) {
        return LinkIdentityResult{true, 0, empty_path_error, true};
    }
    return LinkIdentityResult{false, errno, empty_path_error, true};
}

[[noreturn]] void throw_link_identity_error(
    std::string_view operation,
    const std::filesystem::path& source,
    const std::filesystem::path& destination,
    const LinkIdentityResult& result) {
    if (result.procfs_attempted) {
        throw std::runtime_error(
            std::string(operation) + " from retained identity '" +
            display_path(source) + "' to '" + display_path(destination) +
            "': AT_EMPTY_PATH failed: " +
            system_message(static_cast<unsigned long>(result.empty_path_error)) +
            "; /proc/self/fd fallback failed: " +
            system_message(static_cast<unsigned long>(result.error)));
    }
    throw_path_error(operation, source, destination,
                     static_cast<unsigned long>(result.error));
}
#endif

}  // namespace

struct OutputTransaction::Impl {
    std::filesystem::path destination;
    std::filesystem::path parent;
    std::filesystem::path destination_name;
    std::filesystem::path temporary;
    bool force = false;
    bool committed = false;
    bool sink_fd_taken = false;

#ifdef _WIN32
    HANDLE file_handle = INVALID_HANDLE_VALUE;
#elif defined(__linux__)
    int file_fd = -1;
    int directory_fd = -1;
    bool anonymous_tmpfile = false;
    std::filesystem::path temporary_name;
    bool temporary_linked = false;
    std::filesystem::path staging_name;
    bool staging_linked = false;
#endif

    ~Impl() noexcept {
#ifdef _WIN32
        if (file_handle == INVALID_HANDLE_VALUE) {
            return;
        }
        if (!committed) {
            FILE_DISPOSITION_INFO disposition{};
            disposition.DeleteFile = TRUE;
            (void)::SetFileInformationByHandle(
                file_handle, FileDispositionInfo, &disposition,
                static_cast<DWORD>(sizeof(disposition)));
        }
        (void)::CloseHandle(file_handle);
#elif defined(__linux__)
        if (directory_fd >= 0) {
            if (temporary_linked && !temporary_name.empty()) {
                (void)::unlinkat(directory_fd,
                                 temporary_name.c_str(), 0);
            }
            if (!committed && staging_linked && !staging_name.empty()) {
                (void)::unlinkat(directory_fd, staging_name.c_str(), 0);
            }
        }
        if (file_fd >= 0) {
            (void)::close(file_fd);
        }
        if (directory_fd >= 0) {
            (void)::close(directory_fd);
        }
#endif
    }
};

OutputTransaction::OutputTransaction(std::filesystem::path destination,
                                     bool force)
    : impl_(std::make_unique<Impl>()) {
    if (destination.filename().empty()) {
        throw std::invalid_argument(
            "Output destination must have a nonempty filename: '" +
            display_path(destination) + "'");
    }

    impl_->destination = absolute_destination(destination);
    impl_->destination_name = impl_->destination.filename();
    impl_->parent = impl_->destination.parent_path();
    impl_->force = force;
    validate_parent(impl_->parent, impl_->destination);

#ifdef _WIN32
    if (!force && windows_path_entry_exists(impl_->destination)) {
        throw std::runtime_error("Output destination already exists: '" +
                                 display_path(impl_->destination) + "'");
    }

    for (std::uint64_t attempt = 0; attempt < kMaximumCreationAttempts;
         ++attempt) {
        const std::uint64_t counter =
            temporary_counter.fetch_add(1, std::memory_order_relaxed);
        const std::filesystem::path candidate =
            impl_->parent /
            make_candidate_name(impl_->destination_name, counter);

        HANDLE handle = ::CreateFileW(
            candidate.c_str(), GENERIC_READ | GENERIC_WRITE | DELETE,
            FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
            nullptr, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (handle == INVALID_HANDLE_VALUE) {
            const DWORD error = ::GetLastError();
            if (error == ERROR_FILE_EXISTS) {
                continue;
            }
            throw_path_error("Failed to exclusively create temporary output",
                             candidate, impl_->destination, error);
        }

        impl_->file_handle = handle;
        impl_->temporary = candidate;
        return;
    }
#elif defined(__linux__)
    impl_->directory_fd =
        ::open(impl_->parent.c_str(), O_RDONLY | O_DIRECTORY | O_CLOEXEC);
    if (impl_->directory_fd == -1) {
        const int error = errno;
        throw_path_error("Failed to open output parent directory",
                         impl_->parent, impl_->destination,
                         static_cast<unsigned long>(error));
    }

    if (!force) {
        struct stat status {};
        if (::fstatat(impl_->directory_fd,
                      impl_->destination_name.c_str(), &status,
                      AT_SYMLINK_NOFOLLOW) == 0) {
            throw std::runtime_error("Output destination already exists: '" +
                                     display_path(impl_->destination) + "'");
        }
        const int error = errno;
        if (error != ENOENT) {
            throw_path_error("Failed to inspect output destination",
                             impl_->parent / impl_->destination_name,
                             impl_->destination,
                             static_cast<unsigned long>(error));
        }
    }

    const std::uint64_t anonymous_counter =
        temporary_counter.fetch_add(1, std::memory_order_relaxed);
    impl_->temporary_name =
        make_candidate_name(impl_->destination_name, anonymous_counter,
                            "anonymous");
    impl_->temporary = impl_->parent / impl_->temporary_name;
    impl_->file_fd =
        ::openat(impl_->directory_fd, ".",
                 O_RDWR | O_TMPFILE | O_CLOEXEC, 0666);
    if (impl_->file_fd >= 0) {
        impl_->anonymous_tmpfile = true;
        return;
    }
    const int anonymous_error = errno;
    if (!tmpfile_is_unsupported(anonymous_error)) {
        throw_path_error("Failed to create anonymous Linux temporary output",
                         impl_->parent, impl_->destination,
                         static_cast<unsigned long>(anonymous_error));
    }

    for (std::uint64_t attempt = 0; attempt < kMaximumCreationAttempts;
         ++attempt) {
        const std::uint64_t counter =
            temporary_counter.fetch_add(1, std::memory_order_relaxed);
        impl_->temporary_name =
            make_candidate_name(impl_->destination_name, counter);
        impl_->temporary = impl_->parent / impl_->temporary_name;

        const int descriptor =
            ::openat(impl_->directory_fd, impl_->temporary_name.c_str(),
                     O_RDWR | O_CREAT | O_EXCL | O_CLOEXEC | O_NOFOLLOW,
                     0666);
        if (descriptor == -1) {
            const int error = errno;
            if (error == EEXIST) {
                continue;
            }
            throw_path_error("Failed to exclusively create temporary output",
                             impl_->temporary, impl_->destination,
                             static_cast<unsigned long>(error));
        }

        impl_->file_fd = descriptor;
        impl_->temporary_linked = true;
        return;
    }
#endif

    throw std::runtime_error(
        "Failed to find a unique temporary output name beside destination '" +
        display_path(impl_->destination) + "'");
}

OutputTransaction::~OutputTransaction() noexcept = default;

const std::filesystem::path& OutputTransaction::temporary_path() const noexcept {
    return impl_->temporary;
}

int OutputTransaction::take_sink_fd() {
    if (impl_->sink_fd_taken) {
        throw std::logic_error(
            "The transactional output sink descriptor was already taken");
    }

#ifdef _WIN32
    HANDLE duplicate = INVALID_HANDLE_VALUE;
    HANDLE process = ::GetCurrentProcess();
    if (::DuplicateHandle(process, impl_->file_handle, process, &duplicate,
                          0, FALSE, DUPLICATE_SAME_ACCESS) == 0) {
        const DWORD error = ::GetLastError();
        throw_path_error("Failed to duplicate temporary output identity",
                         impl_->temporary, impl_->destination, error);
    }

    const int descriptor = ::_open_osfhandle(
        reinterpret_cast<intptr_t>(duplicate), _O_BINARY | _O_RDWR);
    if (descriptor == -1) {
        const int conversion_error = errno;
        if (::CloseHandle(duplicate) == 0) {
            const DWORD close_error = ::GetLastError();
            throw std::runtime_error(
                "Failed to convert duplicated temporary output handle '" +
                display_path(impl_->temporary) + "' for destination '" +
                display_path(impl_->destination) + "': " +
                errno_message(conversion_error) +
                "; duplicate-handle cleanup also failed: " +
                system_message(close_error));
        }
        throw std::runtime_error(
            "Failed to convert duplicated temporary output handle '" +
            display_path(impl_->temporary) + "' for destination '" +
            display_path(impl_->destination) + "': " +
            errno_message(conversion_error));
    }
#elif defined(__linux__)
    const int descriptor =
        ::fcntl(impl_->file_fd, F_DUPFD_CLOEXEC, 0);
    if (descriptor == -1) {
        const int error = errno;
        throw_path_error("Failed to duplicate temporary output identity",
                         impl_->temporary, impl_->destination,
                         static_cast<unsigned long>(error));
    }
#endif

    impl_->sink_fd_taken = true;
    return descriptor;
}

void OutputTransaction::commit() {
    if (impl_->committed) {
        return;
    }

#ifdef _WIN32
    if (::FlushFileBuffers(impl_->file_handle) == 0) {
        const DWORD error = ::GetLastError();
        throw_path_error("Failed to flush retained temporary output identity",
                         impl_->temporary, impl_->destination, error);
    }

    if (impl_->force) {
        verify_windows_path_identity(impl_->file_handle, impl_->temporary,
                                     impl_->destination);
        if (::MoveFileExW(impl_->temporary.c_str(),
                          impl_->destination.c_str(),
                          MOVEFILE_REPLACE_EXISTING |
                              MOVEFILE_WRITE_THROUGH) == 0) {
            const DWORD error = ::GetLastError();
            throw_path_error(
                "Failed to durably replace output from verified temporary identity",
                impl_->temporary, impl_->destination, error);
        }
        impl_->committed = true;
        return;
    }

    const std::filesystem::path::string_type& target =
        impl_->destination.native();
    if (target.size() >
        static_cast<std::size_t>(
            std::numeric_limits<DWORD>::max() / sizeof(wchar_t))) {
        throw std::runtime_error("Output destination path is too long for handle-based publication: '" +
                                 display_path(impl_->destination) + "'");
    }

    const std::size_t name_bytes = target.size() * sizeof(wchar_t);
    const std::size_t header_bytes = offsetof(FILE_RENAME_INFO, FileName);
    if (name_bytes > std::numeric_limits<std::size_t>::max() - header_bytes ||
        header_bytes + name_bytes >
            static_cast<std::size_t>(std::numeric_limits<DWORD>::max())) {
        throw std::runtime_error("Output destination rename information is too large: '" +
                                 display_path(impl_->destination) + "'");
    }

    std::vector<unsigned char> storage(header_bytes + name_bytes, 0);
    auto* rename_info =
        reinterpret_cast<FILE_RENAME_INFO*>(storage.data());
    rename_info->ReplaceIfExists = FALSE;
    rename_info->RootDirectory = nullptr;
    rename_info->FileNameLength = static_cast<DWORD>(name_bytes);
    if (name_bytes != 0) {
        std::memcpy(rename_info->FileName, target.data(), name_bytes);
    }

    if (::SetFileInformationByHandle(
            impl_->file_handle, FileRenameInfo, rename_info,
            static_cast<DWORD>(storage.size())) == 0) {
        const DWORD error = ::GetLastError();
        throw_path_error("Failed to publish retained temporary output identity",
                         impl_->temporary, impl_->destination, error);
    }
#elif defined(__linux__)
    if (!impl_->anonymous_tmpfile) {
        bool identity_matches = false;
        try {
            identity_matches = linux_named_identity_matches(
                impl_->file_fd, impl_->directory_fd,
                impl_->temporary_name, impl_->temporary,
                impl_->destination);
        } catch (...) {
            // If verification itself cannot establish ownership, pathname
            // cleanup could target a substituted entry. Retain only the fd.
            impl_->temporary_linked = false;
            throw;
        }
        if (!identity_matches) {
            impl_->temporary_linked = false;
            throw std::runtime_error(
                "Linked temporary output no longer names the retained file identity: '" +
                display_path(impl_->temporary) + "' for destination '" +
                display_path(impl_->destination) + "'");
        }
        if (!impl_->force) {
            if (::linkat(impl_->directory_fd,
                         impl_->temporary_name.c_str(),
                         impl_->directory_fd,
                         impl_->destination_name.c_str(), 0) != 0) {
                const int error = errno;
                throw_path_error(
                    "Failed to publish verified linked Linux temporary output",
                    impl_->temporary, impl_->destination,
                    static_cast<unsigned long>(error));
            }
            if (::unlinkat(impl_->directory_fd,
                           impl_->temporary_name.c_str(), 0) != 0) {
                const int error = errno;
                throw_path_error(
                    "Published output but failed to remove verified linked temporary output",
                    impl_->temporary, impl_->destination,
                    static_cast<unsigned long>(error));
            }
            impl_->temporary_linked = false;
        } else {
            if (::renameat(impl_->directory_fd,
                           impl_->temporary_name.c_str(),
                           impl_->directory_fd,
                           impl_->destination_name.c_str()) != 0) {
                const int error = errno;
                throw_path_error(
                    "Failed to atomically replace output from verified linked temporary output",
                    impl_->temporary, impl_->destination,
                    static_cast<unsigned long>(error));
            }
            impl_->temporary_linked = false;
        }
    } else if (!impl_->force) {
        const LinkIdentityResult result = link_anonymous_identity(
            impl_->file_fd, impl_->directory_fd,
            impl_->destination_name);
        if (!result.linked) {
            throw_link_identity_error(
                "Failed to publish anonymous Linux output identity",
                impl_->temporary, impl_->destination, result);
        }
    } else {
        bool staged = false;
        for (std::uint64_t attempt = 0;
             attempt < kMaximumCreationAttempts; ++attempt) {
            const std::uint64_t counter =
                temporary_counter.fetch_add(1, std::memory_order_relaxed);
            impl_->staging_name = make_candidate_name(
                impl_->destination_name, counter, "publish");
            const LinkIdentityResult result = link_anonymous_identity(
                impl_->file_fd, impl_->directory_fd,
                impl_->staging_name);
            if (result.linked) {
                staged = true;
                impl_->staging_linked = true;
                break;
            }
            if (result.error == EEXIST) {
                impl_->staging_name.clear();
                continue;
            }
            throw_link_identity_error(
                "Failed to stage anonymous Linux output identity",
                impl_->temporary,
                impl_->parent / impl_->staging_name,
                result);
        }
        if (!staged) {
            throw std::runtime_error(
                "Failed to find a unique Linux publication staging name beside destination '" +
                display_path(impl_->destination) + "'");
        }

        const std::filesystem::path staging_path =
            impl_->parent / impl_->staging_name;
        if (::renameat(impl_->directory_fd, impl_->staging_name.c_str(),
                       impl_->directory_fd,
                       impl_->destination_name.c_str()) != 0) {
            const int rename_error = errno;
            if (::unlinkat(impl_->directory_fd,
                           impl_->staging_name.c_str(), 0) != 0) {
                const int cleanup_error = errno;
                throw std::runtime_error(
                    "Failed to atomically replace destination '" +
                    display_path(impl_->destination) + "' from staging path '" +
                    display_path(impl_->parent / impl_->staging_name) +
                    "': " +
                    system_message(static_cast<unsigned long>(rename_error)) +
                    "; staging cleanup also failed: " +
                    system_message(static_cast<unsigned long>(cleanup_error)));
            }
            impl_->staging_name.clear();
            impl_->staging_linked = false;
            throw_path_error("Failed to atomically replace output from staging path",
                             staging_path, impl_->destination,
                             static_cast<unsigned long>(rename_error));
        }
        impl_->staging_name.clear();
        impl_->staging_linked = false;
    }
#endif

    impl_->committed = true;
}

}  // namespace ctb

````````

## `src/vcf_io.cpp`

````````text
#include "convert_to_biallelic/vcf_io.hpp"

#include <htslib/bgzf.h>
#include <htslib/hfile.h>
#include <htslib/hts.h>
#include <htslib/kstring.h>

#include <cerrno>
#include <cstdlib>
#include <limits>
#include <stdexcept>
#include <string>
#include <system_error>

#ifdef _WIN32
#include <io.h>
#else
#include <unistd.h>
#endif

namespace ctb {
namespace {

std::string path_to_utf8(const std::filesystem::path& path) {
    return path.u8string();
}

int checked_thread_count(std::size_t io_workers,
                         const std::string& path_text) {
    if (io_workers > static_cast<std::size_t>(
                         std::numeric_limits<int>::max())) {
        throw std::runtime_error("I/O worker count is too large for HTSlib for '" +
                                 path_text + "'");
    }
    return static_cast<int>(io_workers);
}

std::runtime_error state_error(const char* operation, const char* state) {
    return std::runtime_error(std::string("Cannot ") + operation + " a " + state +
                              " VCF I/O object");
}

int close_descriptor(int descriptor) noexcept {
#ifdef _WIN32
    return ::_close(descriptor);
#else
    return ::close(descriptor);
#endif
}

std::string errno_message(int error) {
    return std::generic_category().message(error);
}

class DescriptorGuard {
public:
    explicit DescriptorGuard(int descriptor) noexcept
        : descriptor_(descriptor) {}

    ~DescriptorGuard() noexcept {
        if (descriptor_ >= 0) {
            (void)close_descriptor(descriptor_);
        }
    }

    DescriptorGuard(const DescriptorGuard&) = delete;
    DescriptorGuard& operator=(const DescriptorGuard&) = delete;

    int release() noexcept {
        const int descriptor = descriptor_;
        descriptor_ = -1;
        return descriptor;
    }

    int get() const noexcept { return descriptor_; }

private:
    int descriptor_;
};

}  // namespace

struct InputSource::Impl {
    explicit Impl(const std::filesystem::path& path, std::size_t io_workers)
        : path_text(path_to_utf8(path)) {
        file = hts_open(path_text.c_str(), "r");
        if (file == nullptr) {
            throw std::runtime_error("Failed to open input VCF '" + path_text +
                                     "'");
        }

        try {
            const htsFormat* detected = hts_get_format(file);
            if (detected == nullptr || detected->format != vcf) {
                throw std::runtime_error("Input '" + path_text +
                                         "' is not a text VCF file");
            }

            switch (detected->compression) {
                case no_compression:
                    is_compressed = false;
                    break;
                case gzip:
                case bgzf:
                    is_compressed = true;
                    break;
                default:
                    throw std::runtime_error(
                        "Input VCF '" + path_text +
                        "' uses unsupported compression; expected plain, gzip, or BGZF");
            }

            if (is_compressed && io_workers > 0) {
                const int thread_count =
                    checked_thread_count(io_workers, path_text);
                if (hts_set_threads(file, thread_count) != 0) {
                    throw std::runtime_error(
                        "Failed to enable threaded input decompression for '" +
                        path_text + "'");
                }
            }
        } catch (...) {
            (void)hts_close(file);
            file = nullptr;
            throw;
        }
    }

    ~Impl() noexcept {
        if (file != nullptr) {
            (void)hts_close(file);
        }
        std::free(line.s);
    }

    std::string path_text;
    htsFile* file = nullptr;
    kstring_t line{0, 0, nullptr};
    bool is_compressed = false;
};

InputSource::InputSource(const std::filesystem::path& path,
                         std::size_t io_workers)
    : impl_(std::make_unique<Impl>(path, io_workers)) {}

InputSource::~InputSource() = default;
InputSource::InputSource(InputSource&&) noexcept = default;
InputSource& InputSource::operator=(InputSource&&) noexcept = default;

bool InputSource::getline(std::string& line) {
    if (!impl_) {
        throw state_error("read from", "moved-from");
    }
    if (impl_->file == nullptr) {
        throw state_error("read from", "closed");
    }

    const int result = hts_getline(impl_->file, KS_SEP_LINE, &impl_->line);
    if (result == -1) {
        return false;
    }
    if (result < -1) {
        throw std::runtime_error("Failed while reading input VCF '" +
                                 impl_->path_text + "'");
    }

    if (impl_->line.l == 0) {
        line.clear();
    } else {
        line.assign(impl_->line.s, impl_->line.l);
    }
    return true;
}

bool InputSource::compressed() const noexcept {
    return impl_ != nullptr && impl_->is_compressed;
}

struct OutputSink::Impl {
    Impl(const std::filesystem::path& path,
         OutputFormat format,
         std::size_t io_workers)
        : pending_descriptor(-1), path_text(path_to_utf8(path)) {
        hFILE* stream = hopen(path_text.c_str(), "wb");
        if (stream == nullptr) {
            throw std::runtime_error("Failed to open VCF output '" +
                                     path_text + "'");
        }
        adopt_hfile(stream, format, io_workers);
    }

    Impl(int owned_fd,
         const std::filesystem::path& display_path,
         OutputFormat format,
         std::size_t io_workers)
        : pending_descriptor(owned_fd), path_text(path_to_utf8(display_path)) {
        if (owned_fd < 0) {
            throw std::invalid_argument(
                "Invalid owned output descriptor for '" + path_text + "'");
        }

        hFILE* stream = hdopen(pending_descriptor.get(), "wb");
        if (stream == nullptr) {
            const int adoption_error = errno;
            const int descriptor = pending_descriptor.release();
            if (close_descriptor(descriptor) != 0) {
                const int close_error = errno;
                throw std::runtime_error(
                    "Failed to adopt owned output descriptor for '" +
                    path_text + "': " + errno_message(adoption_error) +
                    "; descriptor cleanup also failed: " +
                    errno_message(close_error));
            }
            throw std::runtime_error(
                "Failed to adopt owned output descriptor for '" + path_text +
                "': " + errno_message(adoption_error));
        }
        (void)pending_descriptor.release();
        adopt_hfile(stream, format, io_workers);
    }

    void adopt_hfile(hFILE* stream,
                     OutputFormat format,
                     std::size_t io_workers) {
        try {
            switch (format) {
                case OutputFormat::vcf:
                    plain = stream;
                    break;

                case OutputFormat::vcf_gz:
                    compressed = bgzf_hopen(stream, "w");
                    if (compressed == nullptr) {
                        const int error = errno;
                        hclose_abruptly(stream);
                        throw std::runtime_error(
                            "Failed to wrap owned hFILE as BGZF VCF output '" +
                            path_text + "': " + errno_message(error));
                    }
                    if (io_workers > 0) {
                        const int thread_count =
                            checked_thread_count(io_workers, path_text);
                        constexpr int kThreadQueueBlocks = 256;
                        if (bgzf_mt(compressed, thread_count, kThreadQueueBlocks) !=
                            0) {
                            throw std::runtime_error(
                                "Failed to enable threaded BGZF output for '" +
                                path_text + "'");
                        }
                    }
                    break;

                default:
                    hclose_abruptly(stream);
                    throw std::runtime_error("Unsupported output format for '" +
                                             path_text + "'");
            }
        } catch (...) {
            if (plain != nullptr) {
                (void)hclose(plain);
                plain = nullptr;
            }
            if (compressed != nullptr) {
                (void)bgzf_close(compressed);
                compressed = nullptr;
            }
            throw;
        }
    }

    ~Impl() noexcept {
        if (plain != nullptr) {
            (void)hflush(plain);
            (void)hclose(plain);
        }
        if (compressed != nullptr) {
            (void)bgzf_flush(compressed);
            (void)bgzf_close(compressed);
        }
    }

    bool is_closed() const noexcept {
        return plain == nullptr && compressed == nullptr;
    }

    void write(std::string_view bytes) {
        if (is_closed()) {
            throw state_error("write to", "closed");
        }

        std::size_t offset = 0;
        while (offset < bytes.size()) {
            const std::size_t remaining = bytes.size() - offset;
            const auto written = plain != nullptr
                                     ? hwrite(plain, bytes.data() + offset, remaining)
                                     : bgzf_write(compressed,
                                                  bytes.data() + offset,
                                                  remaining);
            if (written <= 0) {
                throw std::runtime_error("Failed while writing output VCF '" +
                                         path_text + "'");
            }
            offset += static_cast<std::size_t>(written);
        }
    }

    void flush() {
        if (is_closed()) {
            throw state_error("flush", "closed");
        }

        const int status = plain != nullptr ? hflush(plain)
                                            : bgzf_flush(compressed);
        if (status != 0) {
            throw std::runtime_error("Failed to flush output VCF '" + path_text +
                                     "'");
        }
    }

    void close() {
        if (is_closed()) {
            return;
        }

        int flush_status = 0;
        int close_status = 0;
        if (plain != nullptr) {
            hFILE* handle = plain;
            plain = nullptr;
            flush_status = hflush(handle);
            close_status = hclose(handle);
        } else {
            BGZF* handle = compressed;
            compressed = nullptr;
            flush_status = bgzf_flush(handle);
            close_status = bgzf_close(handle);
        }

        if (flush_status != 0 && close_status != 0) {
            throw std::runtime_error("Failed to flush and close output VCF '" +
                                     path_text + "'");
        }
        if (flush_status != 0) {
            throw std::runtime_error("Failed to flush output VCF while closing '" +
                                     path_text + "'");
        }
        if (close_status != 0) {
            throw std::runtime_error("Failed to close output VCF '" + path_text +
                                     "'");
        }
    }

    // Declared first so descriptor ownership survives exceptions from display
    // path allocation and every later member/constructor operation.
    DescriptorGuard pending_descriptor;
    std::string path_text;
    hFILE* plain = nullptr;
    BGZF* compressed = nullptr;
};

OutputSink::OutputSink(const std::filesystem::path& path,
                       OutputFormat format,
                       std::size_t io_workers)
    : impl_(std::make_unique<Impl>(path, format, io_workers)) {}

OutputSink::OutputSink(int owned_fd,
                       const std::filesystem::path& display_path,
                       OutputFormat format,
                       std::size_t io_workers) {
    DescriptorGuard descriptor(owned_fd);
    // A new-expression allocates before evaluating its initializer. If the
    // allocation fails, the guard still owns the descriptor; once release()
    // runs, Impl is solely responsible for every constructor path.
    impl_.reset(new Impl(descriptor.release(), display_path, format,
                         io_workers));
}

OutputSink::~OutputSink() = default;
OutputSink::OutputSink(OutputSink&&) noexcept = default;
OutputSink& OutputSink::operator=(OutputSink&&) noexcept = default;

void OutputSink::write(std::string_view bytes) {
    if (!impl_) {
        throw state_error("write to", "moved-from");
    }
    impl_->write(bytes);
}

void OutputSink::flush() {
    if (!impl_) {
        throw state_error("flush", "moved-from");
    }
    impl_->flush();
}

void OutputSink::close() {
    if (!impl_) {
        throw state_error("close", "moved-from");
    }
    impl_->close();
}

}  // namespace ctb

````````

## `src/main.cpp`

````````text
#include "convert_to_biallelic/annotation_index.hpp"
#include "convert_to_biallelic/cli.hpp"
#include "convert_to_biallelic/output_transaction.hpp"
#include "convert_to_biallelic/pipeline.hpp"
#include "convert_to_biallelic/progress.hpp"
#include "convert_to_biallelic/vcf_io.hpp"

#include <exception>
#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <string>
#include <string_view>

namespace {

constexpr std::string_view kVersion = "convert-to-biallelic 0.1.0\n";

void write_stdout(std::string_view text) {
    std::cout << text << std::flush;
    if (!std::cout) {
        throw std::runtime_error("Failed to write standard output");
    }
}

void report_error(std::string_view message) noexcept {
    try {
        std::cerr << "Error: " << message << '\n' << std::flush;
    } catch (...) {
        // There is no secondary reporting channel. Preserve the primary
        // failure and its exit code if stderr itself is unavailable.
    }
}

ctb::AnnotationIndex load_annotation_from_config(const ctb::Config& config) {
    // Annotation loading is intentionally single-streamed and uses no HTSlib
    // I/O workers so the full configured thread budget remains available to
    // the subsequently opened conversion input and output.
    ctb::InputSource annotation_input(config.variants, 0);
    return ctb::load_annotation(annotation_input, config.memory_limit_bytes);
}

bool detect_input_compression(const std::filesystem::path& path) {
    // InputSource configures HTSlib workers only at construction, so probe
    // before calculating the allocation and then reopen with that allocation.
    ctb::InputSource probe(path, 0);
    return probe.compressed();
}

int run_conversion(const ctb::Config& config) {
    ctb::AnnotationIndex annotation = load_annotation_from_config(config);

    const bool compressed_input = detect_input_compression(config.input);
    const bool compressed_output =
        config.output_format == ctb::OutputFormat::vcf_gz;
    const ctb::ThreadAllocation threads =
        ctb::allocate_threads(config, compressed_input, compressed_output);

    ctb::InputSource input(config.input, threads.input_io_workers);
    if (input.compressed() != compressed_input) {
        throw std::runtime_error(
            "Input compression changed while reopening input '" +
            config.input.u8string() + "'");
    }

    ctb::PipelineOptions options;
    options.threads = threads;
    options.memory_limit_bytes = config.memory_limit_bytes;
    options.progress_interval = config.progress_interval;
    options.quiet = config.quiet;

    ctb::OutputTransaction transaction(config.output, config.force);
    const int sink_fd = transaction.take_sink_fd();
    ctb::OutputSink output(sink_fd, transaction.temporary_path(),
                           config.output_format,
                           threads.output_io_workers);

    ctb::PipelineStats stats =
        ctb::run_pipeline(input, output, annotation, options,
                          std::cout, std::cerr);

    output.flush();
    output.close();
    transaction.commit();

    if (!config.quiet) {
        const std::string summary = ctb::format_final_summary(stats);
        write_stdout(summary + "\n");
    }
    return 0;
}

}  // namespace

int main(int argc, char** argv) {
    ctb::Config config;
    try {
        try {
            config = ctb::parse_cli(argc, argv);
        } catch (const ctb::UsageRequested& request) {
            write_stdout(request.version() ? kVersion
                                           : std::string_view(ctb::usage_text()));
            return 0;
        } catch (const std::invalid_argument& error) {
            report_error(error.what());
            return 2;
        }

        return run_conversion(config);
    } catch (const std::exception& error) {
        report_error(error.what());
        return 1;
    } catch (...) {
        report_error("Unknown non-standard exception");
        return 1;
    }
}

````````

## `.superpowers/sdd/task-8-report.md`

````````text
# Task 8 Transactional Output and Main Orchestration Report

## Scope

Task 8 now retains native HANDLE/file-descriptor ownership from exclusive
creation through sink I/O and failure cleanup. Publication is handle-based
where the platform supports the required operation and otherwise uses checked
pathname operations under the documented non-adversarial output-directory
threat model. The ownership correction expanded the review boundary to:

- `include/convert_to_biallelic/output_transaction.hpp`
- `src/output_transaction.cpp`
- `include/convert_to_biallelic/vcf_io.hpp`
- `src/vcf_io.cpp`
- `src/main.cpp`
- `include/convert_to_biallelic/types.hpp`
- `include/convert_to_biallelic/progress.hpp`
- `src/progress.cpp`
- `src/pipeline.cpp`
- `README.md`
- `UNVERIFIED.md`
- `docs/superpowers/specs/2026-07-18-cpp-multithreaded-vcf-converter-design.md`
- `docs/superpowers/plans/2026-07-18-cpp-multithreaded-vcf-converter-source-only.md`
- `.superpowers/sdd/progress.md`
- `.superpowers/sdd/task-8-brief.md`
- `.superpowers/sdd/task-8-report.md`

The complete package is `.superpowers/sdd/task-8-review-package.md`. Python
was not changed.

## Root cause and corrected ownership model

The first Task 8 version exclusively created a temporary pathname but closed
its native handle, then let `OutputSink` reopen that pathname. An independent
directory-entry substitution could therefore redirect writes, cleanup, or
publication away from the exclusively created object.

The corrected model is:

1. `OutputTransaction` exclusively creates and retains the original native
   HANDLE/file descriptor.
2. Its one-shot `take_sink_fd()` duplicates that retained identity.
3. `OutputSink` adopts the duplicate through `hdopen`; BGZF wraps that exact
   hFILE through `bgzf_hopen`.
4. Main explicitly flushes and closes the sink duplicate.
5. Commit publishes after sink close, using the retained original identity
   directly where possible and checked pathname operations where required.
6. Uncommitted destruction disposes/closes the retained identity. Anonymous
   and Windows cleanup are handle-only; named Linux fallback unlinks only
   while its exclusive/verified ownership flag remains valid.

Main no longer opens the temporary path for sink I/O. It passes
`temporary_path()` only as descriptor-sink display/error context; Windows
forced publication and Linux named fallback verify that path against the
retained identity immediately before their required path-based publication
calls.

The transactional guarantee covers normal, non-adversarial operation,
including conversion/output failures, temporary-name collisions, and
concurrent creation of the final destination. It requires the output directory
not to be modified by an adversarial process during the transaction. Windows
forced `MoveFileExW` and Linux named-temporary/anonymous-stage fallbacks are
pathname-based and are not hardened against same-directory substitution
between a best-effort identity check and publication.

## OutputSink transfer and close audit

- The fd constructor owns every nonnegative descriptor from function entry.
  A local `DescriptorGuard` covers allocation failure before `Impl` exists;
  `Impl` then constructs its own descriptor guard as its first member, before
  display-path allocation, so member-initialization failure is also covered.
- C++ new-expression allocation occurs before the initializer invoking
  `release()`: allocation failure leaves the guard responsible; constructor
  entry transfers sole ownership to `Impl`'s first member.
- If `hdopen` fails, HTSlib has not adopted the fd; `Impl` closes it exactly
  once and reports any close failure with the adoption failure.
- After `hdopen` succeeds, hFILE solely owns the descriptor. Plain output
  stores that hFILE. BGZF output passes it to `bgzf_hopen`.
- If `bgzf_hopen` fails, it has not adopted the supplied hFILE, so
  `hclose_abruptly` closes it once. After success, BGZF solely owns hFILE.
- Thread-setup failure closes the BGZF object once in constructor unwind.
- Normal `OutputSink::close` nulls its owning pointer before the checked
  flush/close call, preventing destructor double-close after a reported close
  failure.
- The legacy pathname constructor remains, but main exclusively uses the
  owned-fd constructor and explicit final `OutputFormat`.

## Windows identity lifecycle

- `CreateFileW(..., CREATE_NEW, ...)` requests read/write plus DELETE access
  and share-read/write/delete, retaining the successful HANDLE. Only
  `ERROR_FILE_EXISTS` is treated as a name collision.
- `take_sink_fd` uses `DuplicateHandle`; `_open_osfhandle` transfers the
  duplicate to a binary read/write CRT descriptor. Conversion failure closes
  the duplicate HANDLE once.
- After sink close, commit checks `FlushFileBuffers` on the retained original.
- No-force commit builds checked-size `FILE_RENAME_INFO` for the absolute
  destination and calls `SetFileInformationByHandle(FileRenameInfo)` with
  replacement disabled, so it cannot overwrite a racing destination.
- Force commit compares volume/file-index identity from the retained HANDLE
  and a no-follow source-path HANDLE immediately before calling
  `MoveFileExW(MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)`. A mismatch
  fails without moving or deleting the substitute; replacement never
  pre-deletes the destination and retains the required write-through flag.
  `MoveFileExW` remains pathname-based, so this check detects an observed
  mismatch but is not an atomic defense against an adversarial same-directory
  substitution after the check.
- Uncommitted destruction requests `FileDispositionInfo` deletion on the
  retained HANDLE and then closes that HANDLE. Cleanup follows the file object
  even if its old directory entry was renamed or substituted.

## Linux identity lifecycle

- The existing parent is opened and retained as a directory fd. Destination
  preflight, temporary creation, identity checks, staging, and rename are
  relative to that fd; parent-path replacement cannot redirect them.
- The preferred creation route is `openat(directory_fd, ".", O_TMPFILE |
  O_RDWR | O_CLOEXEC, 0666)` without `O_EXCL`, retaining a linkable anonymous
  inode. Only documented kernel/filesystem unsupported errors enter fallback;
  resource, permission, and I/O errors remain primary failures.
- Anonymous publication first attempts `linkat(..., AT_EMPTY_PATH)`. On the
  documented privilege/flag failures it retries through
  `/proc/self/fd/<fd>` with `AT_SYMLINK_FOLLOW`, which binds the retained fd
  identity without requiring `CAP_DAC_READ_SEARCH`. Both errors are retained
  if neither route succeeds.
- If `O_TMPFILE` is unavailable, `openat` uses `O_CREAT | O_EXCL | O_NOFOLLOW
  | O_CLOEXEC`; only `EEXIST` is retried. The named entry stays linked. Just
  before publication, retained `fstat` and no-follow `fstatat` device/inode
  identities must match. Verification failure disables pathname cleanup before
  throwing so a substituted entry is never deleted; this can safely orphan an
  unverifiable original name.
- `take_sink_fd` uses `F_DUPFD_CLOEXEC`; sink close consumes only that
  duplicate. Anonymous cleanup closes the original; named cleanup unlinks only
  a still-owned verified/exclusive entry and closes the original.
- No-force anonymous commit links directly to the destination without
  replacement. Force anonymous commit creates a unique exclusive retained-
  identity stage and atomically `renameat`s it over the destination.
- Named no-force uses checked hard-link publication followed by checked temp
  unlink; named force atomically `renameat`s the verified temp over the
  destination. A linked-state flag ensures staging cleanup never targets a
  name the transaction failed to create.
- Anonymous staging and all named-fallback publication steps are pathname-
  based. Their exclusive creation, flags, and immediate identity checks cover
  normal collisions and accidental mismatch, but do not harden the directory
  against adversarial same-directory substitution.
- `<stdio.h>` supplies the POSIX `renameat` declaration under the Linux feature
  configuration; `_GNU_SOURCE` exposes `AT_EMPTY_PATH`.
- Preprocessor branches are explicit for `_WIN32` and `__linux__`; other
  platforms receive a source-level unsupported-platform error.

## Main, progress, and exit ordering

- Help/version return 0; parser `invalid_argument` returns 2; conversion and
  other exceptions return 1 with one primary stderr diagnostic.
- Annotation is loaded with zero I/O workers before transaction creation.
  Input is probed, thread allocation is calculated, and input is reopened with
  its allocated workers.
- Transaction is declared before sink, and therefore outlives it. Pipeline
  threads/reporter finish before return; sink flush/close precedes commit.
- `ProgressReporter::finish()` returns elapsed time without final text.
  `PipelineStats` carries it, `run_pipeline` returns it, and main calls the
  reusable formatter exactly once only after commit unless quiet.
- Pipeline/flush/close/commit failures cannot enter the final-summary branch.

## Static boundary

No compiler, CMake configuration, build, executable, test, benchmark,
installation, Git command, commit, worktree operation, or Python process was
used. C++/HTSlib ABI behavior, Windows HANDLE/path identity and durable replace
behavior, Linux `O_TMPFILE`, `AT_EMPTY_PATH`, procfs and named-fallback behavior,
all injected close/rename failures, and pipeline concurrency remain
runtime-unverified.

No sink path reopens the temporary pathname. Path-based publication branches
perform best-effort immediate retained-identity checks and fail on an observed
mismatch. Those checks are not claimed as atomic adversarial hardening. Source
authored and statically audited, but not compiled or tested at the user's
request.

````````


