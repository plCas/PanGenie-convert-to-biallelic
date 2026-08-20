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
