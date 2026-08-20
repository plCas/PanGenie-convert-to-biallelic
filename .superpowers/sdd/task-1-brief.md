### Task 1: Create the Source Tree and Shared Types

This task establishes the future build description, shared types, and explicit unverified status. Do not install anything, compile, run CMake, execute code, test, create a worktree, or commit.

Files to create:

- `CMakeLists.txt`
- `include/convert_to_biallelic/types.hpp`
- `README.md`
- `UNVERIFIED.md`

Requirements:

1. Create a CMake 3.20+ C++17 project `convert_to_biallelic`, version `0.1.0`.
2. Describe future discovery of `Threads`, `PkgConfig`, and `htslib>=1.17`.
3. Declare `ctb_core` with these future sources: `annotation_index.cpp`, `cli.cpp`, `converter.cpp`, `memory_budget.cpp`, `output_transaction.cpp`, `pipeline.cpp`, `progress.cpp`, and `vcf_io.cpp`.
4. Declare executable `convert-to-biallelic` from `src/main.cpp` linked to `ctb_core`.
5. In `types.hpp`, define in namespace `ctb`:
   - `enum class OutputFormat { vcf, vcf_gz };`
   - `ThreadAllocation` with conversion workers default 1 and input/output I/O workers default 0.
   - `PipelineStats` with input/output record, output byte, and peak tracked byte counters, all `uint64_t` default 0.
   - `RawWorkChunk` with sequence, first line number, and `vector<string>` records.
   - `RawResultChunk` with sequence, output bytes, input record count, and output record count.
6. README must show intended CLI usage with `--variants`, `--input`, `--output`, `--threads`, and `--memory-limit`, and link to `UNVERIFIED.md`.
7. `UNVERIFIED.md` must state that the source has not been compiled, linked, executed, tested against Python, memory-profiled, benchmarked, or verified on Windows/Linux.
8. Keep `convert-to-biallelic.py` unchanged.
9. Perform static self-review only.

Write the detailed report to `.superpowers/sdd/task-1-report.md`. Report status, files authored, static checks, self-review findings, and concerns. The short return must state that no tests or commits were performed by user instruction.
