# C++ Multithreaded VCF Converter Design

Date: 2026-07-18

## Objective

Replace `convert-to-biallelic.py` with a cross-platform C++17 command-line program that preserves the Python converter's uncompressed VCF output, processes records in parallel, uses HTSlib for VCF text and BGZF I/O, reports progress to stdout, and targets at most 2 GiB of process memory under ordinary inputs.

The first release supports `.vcf` and `.vcf.gz` inputs and outputs on Windows and Linux. BCF, indexing, region queries, distributed processing, and a Python API are outside this scope.

## Success Criteria

1. For every supported valid fixture within the compatibility scope below, the C++ program's uncompressed VCF bytes equal the Python program's output bytes.
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
- Known IDs referenced by an input record are deduplicated and sorted by annotation position, then ID.
- Each known ID produces one record with annotation position, ID, REF, ALT, and `INFO/ID` substituted.
- Only `MA` and `UK` are copied from the remaining INFO entries.
- FORMAT output contains `GT` and, when present in the input FORMAT, `GQ`.
- Input phasing separators are normalized to `/`, as in the Python implementation.
- Missing alleles remain `.`; alleles containing the emitted ID become `1`; other alleles become `0`.
- The ordering of input records, emitted IDs within each input record, and samples is deterministic.

The compatibility target is the uncompressed VCF byte stream. BGZF block layout, compression metadata, and compressed bytes are not required to match another compressor.

The Python oracle first stores emitted IDs in a hash set and then sorts only by annotation position. Its tie order is therefore hash-iteration-dependent when one input record emits distinct IDs at the same annotation position. The C++ source deliberately uses ID as a deterministic secondary key. Such equal-position multi-ID records are outside the Python byte-equivalence scope; their C++ position-then-ID order is an intentional deterministic extension. This is a statically known boundary, not a property awaiting runtime verification.

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
Finished: input=13,442,817 output=18,791,203 elapsed=00:01:08 average=197,688 records/s peak_tracked_memory=812 MiB
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

Before a pipeline-owned string or vector calls `reserve()`, its permit temporarily admits both the still-live old allocation and the requested new allocation. After the call it reconciles the actual implementation-selected capacity. Capacity beyond the request, allocator metadata, HTSlib, stacks, and other untracked transients remain within the purpose of the separate 10% process reserve.

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
