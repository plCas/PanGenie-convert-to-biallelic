### Task 7: Author Progress Reporting and the Ordered Pipeline

Create `include/convert_to_biallelic/progress.hpp`, `src/progress.cpp`, `include/convert_to_biallelic/pipeline.hpp`, and `src/pipeline.cpp`. Source-only: no install/build/run/test/Git; Python unchanged.

Public requirements:

```cpp
struct ProgressCounters {
  atomic<uint64_t> input_records, output_records, tracked_memory;
};
class ProgressReporter {
public:
  ProgressReporter(ProgressCounters&, chrono::milliseconds, ostream&, bool quiet);
  ~ProgressReporter();
  void start();
  void finish(const PipelineStats&);
  void stop_without_summary() noexcept;
};

struct WorkChunk { RawWorkChunk data; MemoryPermit permit; };
struct ResultChunk { RawResultChunk data; MemoryPermit permit; };
struct PipelineOptions {
  ThreadAllocation threads;
  uint64_t memory_limit_bytes = 2 GiB;
  size_t target_records_per_chunk = 512;
  uint64_t target_bytes_per_chunk = 8 MiB;
  chrono::milliseconds progress_interval{5000};
  bool quiet = false;
};
PipelineStats run_pipeline(InputSource&, OutputSink&, const AnnotationIndex&,
                           const PipelineOptions&, ostream& progress,
                           ostream& diagnostics);
```

Progress requirements:

1. One reporter thread only; workers/writer update atomics but never write stdout.
2. Periodic complete flushed line: `[HH:MM:SS] input=N output=N rate=N records/s memory=N MiB`.
3. Use steady_clock and recent interval rate. Zero interval disables periodic reports but final remains unless quiet.
4. `quiet` suppresses periodic/final output. `finish` stops/joins then prints exactly one final line; `stop_without_summary` stops/joins without output and is noexcept; destructor calls it.
5. Use CV for interruptible waits, not sleep/busy loop. Protect start/finish state from double start/finish.

Pipeline requirements:

1. Validate conversion worker >=1, targets >=1, memory >=64 MiB, annotation estimate plus 10% reserve below limit.
2. Tracked pipeline allowance = memory limit - 10% reserve - annotation estimated bytes. Queue capacity = max(1, 2*conversion_workers) with overflow safety.
3. Read/filter/write all leading headers before starting worker/writer concurrency. Retain the first data line for chunking. Reject header lines after data begins.
4. Reader assigns consecutive sequence numbers and physical line numbers. Chunk ends at 512 records or 8 MiB, whichever first; one oversized record allowed. Do not pre-count or load whole file.
5. Account vector/string capacities through a `MemoryPermit`; resize permit before planned buffer capacity growth. Never acquire/resize while holding queue mutex.
6. Workers dynamically pop, convert records sequentially with physical line numbers, build one result per chunk, track result capacity before reserve/growth, release input storage, shrink permit to result capacity, update input counter, and push same sequence.
7. `WorkChunk`/`ResultChunk` must satisfy queue nothrow move/destruct requirements; assert this.
8. Last exiting worker closes result queue. Result writer alone calls `OutputSink::write` after header phase; stores out-of-order chunks in `std::map`; writes only next sequence; updates output/output-byte counters after successful write; releases permit by erasing/destroying result.
9. Reject missing/duplicate sequences and closed results with a gap.
10. Store first exception_ptr under mutex. First failure cancels memory budget and both queues; all paths join reader-context workers/writer/reporter before rethrow. Do not deadlock if failure happens during acquire/push/pop/write.
11. On success fill PipelineStats and call reporter.finish once. On failure call stop_without_summary and rethrow after joins.
12. Diagnostics stream is reserved for warnings/errors; do not emit unsupported warnings in this task except one-overage memory warning if observable without races.
13. Do not add single-thread special path; one worker uses same pipeline.
14. Static review invariants, ownership, counter overflow, thread lifecycle, and lock ordering. Runtime concurrency remains unverified.

Use apply_patch. Write report and complete exact-file review package `task-7-report.md` and `task-7-review-package.md`. No tests/commits.
