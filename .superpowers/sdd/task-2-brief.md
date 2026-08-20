### Task 2: Author CLI Parsing and Thread Allocation

Create `include/convert_to_biallelic/cli.hpp` and `src/cli.cpp`. Do not install, compile, execute, test, use Git, or alter `convert-to-biallelic.py`.

Required interface in namespace `ctb`:

- `Config` with paths `variants`, `input`, `output`; `threads`; `memory_limit_bytes` default 2 GiB; `progress_interval` default 5000 ms; `quiet`; `force`; and `OutputFormat output_format`.
- `UsageRequested(bool version)`, `version()`, and `what()`.
- `Config parse_cli(int argc, char** argv)`.
- `ThreadAllocation allocate_threads(const Config&, bool compressed_input, bool compressed_output)`.
- `const char* usage_text()`.

Parser requirements:

1. Required: `--variants`, `--input`, `--output`.
2. Optional: `--threads`, `--memory-limit`, `--progress-interval`, `--quiet`, `--force`, `--output-format`, `--help`, `--version`.
3. Reject missing values, duplicate/unknown options, zero threads, memory below 64 MiB, negative progress interval, and unsupported output extension without override.
4. Parse case-insensitive binary K/M/G suffixes with overflow checks.
5. Infer `.vcf` or `.vcf.gz` case-insensitively; override accepts only `vcf` or `vcf.gz`.
6. If `--threads` is absent, use `std::thread::hardware_concurrency()`, falling back to 1.
7. Allocation: compressed output and threads > 1 gets one output I/O worker; compressed input and total threads >= 4 gets one input I/O worker if at least one conversion worker remains; all remainder conversion, minimum one.
8. `usage_text()` must name and describe every supported option and say progress is stdout, diagnostics stderr, and VCF output is the explicit file.
9. Static self-review only; record API and runtime behavior as unverified.

Use apply_patch for edits. Write `.superpowers/sdd/task-2-report.md` with authored files, static interface/branch review, concerns, and explicit no-test/no-commit statement. Return concise DONE/DONE_WITH_CONCERNS/BLOCKED/NEEDS_CONTEXT status.
