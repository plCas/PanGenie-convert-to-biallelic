# Task 3 Report: HTSlib Text and BGZF I/O Wrappers

## Status

Implemented the source-only Task 3 API in:

- `include/convert_to_biallelic/vcf_io.hpp`
- `src/vcf_io.cpp`

No installation, configuration, compilation, execution, tests, benchmarks, Git
operations, commits, or Python changes were performed.

## Static implementation summary

- Added move-only `ctb::InputSource` and `ctb::OutputSink` PIMPL interfaces with
  the required public signatures.
- Input opens through `hts_open(path, "r")`, requires detected `vcf` format, and
  accepts only `no_compression`, `gzip`, or `bgzf` compression.
- Compressed input enables `hts_set_threads` when `io_workers > 0`.
- Input lines use `hts_getline(..., KS_SEP_LINE, ...)`; `-1` is EOF, values below
  `-1` throw, and successful bytes are copied from the `kstring_t` length without
  copying a terminator.
- Plain output is selected only by `OutputFormat::vcf` and uses `hopen`, repeated
  `hwrite`, `hflush`, and `hclose` calls.
- Compressed output is selected only by `OutputFormat::vcf_gz` and uses
  `bgzf_open`, optional `bgzf_mt`, repeated `bgzf_write`, `bgzf_flush`, and
  `bgzf_close` calls.
- Output `close()` releases the handle even when flush fails, reports flush and/or
  close errors, and is a no-op after an actual close.
- Destructors perform best-effort non-throwing cleanup of still-open handles.
- Defaulted `noexcept` move operations transfer the owning `std::unique_ptr`;
  replacing a live destination invokes best-effort cleanup through `Impl`.
- Constructor exceptions after a handle is opened explicitly close that handle,
  because an `Impl` destructor does not run when its constructor throws.
- Worker counts are range-checked before conversion to HTSlib's `int` thread
  arguments.
- Paths are retained as `std::filesystem::path` at the public boundary and
  converted in one isolated `path_to_utf8` helper before calling HTSlib.

## Static ownership and error-path review

- `InputSource::Impl` owns one `htsFile*` and one `kstring_t` allocation. The file
  is closed with `hts_close`; the string storage allocated by HTSlib is released
  with `std::free`.
- `OutputSink::Impl` owns exactly one of `hFILE*` or `BGZF*`. Construction selects
  the handle solely from explicit `OutputFormat`; the temporary filename or its
  suffix is never inspected.
- Failed input validation and failed input thread setup close the opened
  `htsFile*` before rethrowing.
- Failed BGZF thread setup closes the opened `BGZF*` before rethrowing. The output
  constructor's catch path also covers future exceptions after either output
  handle is acquired.
- Output write loops treat zero and negative progress as errors, preventing an
  infinite loop and ensuring a successful return means every requested byte was
  passed to HTSlib.
- `close()` nulls the owned pointer before invoking the close function so an
  exception reported afterward cannot cause a destructor to close an already
  consumed handle again.
- `getline`, `write`, and `flush` reject unavailable state descriptively.
  `OutputSink::close()` rejects a moved-from object but is idempotent after a real
  close.
- The required `compressed() const noexcept` signature cannot throw on a
  moved-from object. Per the task-owner resolution, it returns `false` in that
  state.

## Public signature review

The declarations match the supplied Task 3 API, including constructor argument
types, `noexcept` move operations, `std::string_view` output, and the
`compressed() const noexcept` qualifier. `OutputFormat` comes from the existing
`include/convert_to_biallelic/types.hpp`.

## Unverified concerns

All HTSlib API, build, linkage, and runtime behavior is unverified because the
task explicitly forbids configuration, compilation, execution, and tests. In
particular:

- Public header availability and the source-assumed signatures/return contracts
  for `hts_get_format`, `hts_set_threads`, `hts_getline`, `hwrite`, `hflush`,
  `hclose`, `bgzf_mt`, `bgzf_write`, `bgzf_flush`, and `bgzf_close` are unverified
  against the project's eventual HTSlib installation.
- The `bgzf_mt` queue argument of 256 blocks follows common HTSlib usage but is
  unverified for the eventual runtime and workload.
- Detection behavior for plain, gzip, BGZF, BCF, malformed, empty, and unrelated
  input files is unverified at runtime.
- `std::filesystem::path::u8string()` supplies UTF-8 bytes in this C++17 source,
  but whether the eventual Windows HTSlib build accepts UTF-8 `char*` paths is
  unverified. Unicode-path portability therefore remains unverified.
- Close-after-flush error behavior depends on HTSlib's documented ownership
  convention that each close call consumes its handle even when it returns an
  error; this remains unverified against the eventual library build.

The complete static review snapshot is in
`.superpowers/sdd/task-3-review-package.md`.
