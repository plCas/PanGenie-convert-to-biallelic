# Task 7 Progress Reporter and Ordered Pipeline Report

## Scope

Authored or extended the eight source files in the final Task 7 review boundary:

- include/convert_to_biallelic/annotation_index.hpp
- src/annotation_index.cpp
- include/convert_to_biallelic/converter.hpp
- src/converter.cpp
- include/convert_to_biallelic/progress.hpp
- src/progress.cpp
- include/convert_to_biallelic/pipeline.hpp
- src/pipeline.cpp

The annotation/converter extensions remove per-record lookup-key allocation
and make both result and parser-scratch growth permit-aware. The complete exact-file package is in
.superpowers/sdd/task-7-review-package.md. Python was not changed.

## Fully accounted conversion storage

- append_converted_record accepts the destination string, cumulative output
  count, and a capacity-admission callback carrying both requested output
  capacity and requested scratch bytes.
- The pipeline callback performs checked size_t to uint64_t conversion and
  checked addition of input storage, output capacity plus terminator, and
  scratch capacity. The aggregate enters the sequence-aware permit resize path
  before any requested converter allocation.
- Converted bytes are appended directly to the result destination. There is no
  per-record ConversionResult::bytes in the pipeline and no post-return
  result-admission window. The compatibility convert_record wrapper invokes
  the same implementation with a two-argument no-op callback.
- Every destination append or push first calls the checked output-capacity
  helper. That helper calls admission before reserve(target) and immediately
  afterward with the implementation-selected actual capacity.

The only owning, successful record-conversion scratch allocations are seven
vectors:

| Vector | Element layout |
| --- | --- |
| fields | string_view |
| INFO entries | {string_view key, string_view value} |
| allele mappings | comma-level string_view |
| resolved variants | {string_view id, const VariantDefinition*} |
| FORMAT | string_view |
| samples | {size_t allele_offset, size_t allele_count, string_view gq} |
| alleles | flattened int64_t, with -1 for missing |

- Every vector push is preceded by an explicit capacity check. If growth is
  needed, requested scratch bytes are recomputed with checked multiplication
  and addition, admission is called with the planned capacity, reserve is
  performed, and admission is called again with all actual capacities.
- No vector relies on implicit growth. No nested per-sample vector, owning
  sample string, per-variant output-field vector, or heap-owning position
  string remains. Position formatting uses a fixed stack array and to_chars.
- INFO parsing retains first-key order and updates duplicate keys to the last
  value. Allele mappings retain comma-level views and scan colon components on
  demand. FORMAT and every sample are scanned once into the flattened
  metadata.
- On both successful conversion branches, explicit empty-vector swaps destroy
  the capacities of all seven scratch vectors first. Only after every owning
  allocation is gone does the callback receive actual output capacity and zero
  scratch. Exception paths do not issue the zero-scratch callback, so their
  permit remains conservative until caller cleanup.
- Allocator bookkeeping and the implementation's transient old/new allocation
  overlap during reserve are not container capacities and remain within the
  brief's 10% process reserve. Requested capacity is admitted first and actual
  capacity is reconciled immediately after each successful reserve.

## Allocation-free immutable annotation lookup

- Annotation loading still stores owned chromosome/ID strings and definitions
  in the original two-level unordered maps with last-record-wins semantics.
- After the input pass and all overwrites are complete, the loader builds an
  immutable two-level unordered view index. Its outer and inner maps use
  no-throw string_view hash/equality functors, reserve their final entry counts,
  and point into the owned chromosome/ID keys and VariantDefinition nodes.
- find_checked now performs only outer chromosome-view and inner ID-view
  lookups. It creates no temporary owning strings on successful or missing-key
  lookup. The noexcept find wrapper continues to delegate and catch.
- Copy is disabled. The owning maps are declared before the view index and are
  never mutated after publication. Default-allocator unordered-map move
  construction preserves the referenced nodes, and destruction order removes
  the view index before its owned targets.
- Before any view-map allocation, annotation loading computes a saturating
  preflight from the owned-index estimate, projected outer/inner node counts,
  and bucket counts rounded with a two-times-plus-one safety factor. It rejects
  an over-allowance preflight before reserve.
- Annotation estimated_bytes includes the built view index's actual outer/inner
  bucket arrays and node payload/overhead estimates. Actual post-reserve bucket
  counts are measured after construction and checked again against the 90%
  allowance; either failure destroys the local AnnotationIndex before loader
  return. These remain conservative allocation estimates, not hard RSS claims.

## Conversion semantic-preservation audit

The bounded-scratch implementation was compared statically with the Task 5
conversion contract and previous implementation:

- headers retain exact filtering and LF behavior;
- tab splitting, at-least-nine-field validation, INFO parsing, last duplicate
  value with first encounter order, and nonempty ID validation are unchanged;
- REF plus comma mappings, empty-component rejection, single-mapping unknown
  passthrough, required annotation resolution, ID deduplication, and
  position-then-ID sorting are preserved;
- output CHROM/POS/ID/REF/ALT/QUAL/FILTER/INFO/FORMAT field order is preserved;
- exact GT/GQ token selection, required sample fields, ploidy, missing alleles,
  full decimal parsing, range validation, colon-component membership, slash
  normalization, and exact GQ copying are preserved;
- output count and LF behavior are unchanged, and all record errors retain
  Input line N: context.

No byte-equivalence execution was permitted, so this is a semantic source
comparison rather than runtime proof.

## Progress reporter and stream review

- ProgressCounters contains relaxed atomic input, output, and tracked-memory
  counters. Workers and the writer never write the progress stream.
- start() records a steady_clock origin and creates at most one reporter
  thread. Quiet mode and a nonpositive interval suppress periodic work; a zero
  interval still permits the successful final line unless quiet.
- The reporter uses an interruptible condition-variable timeout and a
  recent-interval rate baseline. Periodic and final writes each flush and then
  check stream state.
- The first reporter exception is retained and invokes the pipeline callback
  outside the reporter mutex. It promptly enters the shared first-failure path
  that cancels memory admission and both queues. finish() rethrows it;
  stop_without_summary() retains it and remains noexcept.
- Progress and diagnostics are rejected before any I/O when they share either
  an ostream object or stream buffer.
- Reporter join recovery uses notify_all_at_thread_exit; retry/detach occurs
  only after native thread exit, so no detached reporter can retain access to
  a destroyed this.

## Header, reader, permit, and ordering review

- Required option invariants are checked: at least one conversion worker,
  positive record and byte targets, at least 64 MiB, and a strict positive
  allowance after annotation estimate and the 10% reserve.
- All leading headers are filtered, written, and byte-accounted before any
  reporter, writer, or worker starts. The first data line is retained; any
  header after data is rejected.
- Input is streamed once. Chunks end at record/byte targets or before tracked
  input storage would cross the allowance. One intrinsically oversized record
  drains behind earlier sequences, is admitted alone, and is warned once.
- Physical-line overflow is checked after a successful read, so exactly
  UINT64_MAX physical lines may end at EOF.
- Annotation loading uses the same read-then-overflow order while retaining a
  saturated next-line value for read/parse error context.
- Reader vector/string capacity is admitted before reserve or transfer and
  reconciled to actual capacity. Worker input, result, and all converter
  scratch share one permit. Source storage is destroyed before the permit
  shrinks to result-only capacity.
- Only the result sequence currently required by the writer may claim or
  continue the sole overage. Later sequences may grow only when their delta
  fits normally. Writer erasure advances the next-required sequence and wakes
  admission waiters.
- The writer alone calls OutputSink::write, rejects stale/duplicate sequences,
  emits only the next sequence, publishes checked counters only after
  successful output, releases permits by erasure, and rejects final gaps or
  submitted-count mismatch.

## Worker-spawn and result-close lifecycle

- Worker creation checks PipelineState cancellation before every additional
  thread.
- A mutex-protected WorkerLifecycle owns the active count, spawning-finished
  flag, and one-time result-close claim. A worker exit may claim result close
  only when spawning is already finished and the active count becomes zero.
- The main thread marks spawning finished immediately after the creation loop.
  If no worker remains, main claims and closes results. The same idempotent
  finish operation runs after the reader/construction try block to cover every
  early exception.
- A failed thread creation records the creation exception, rolls back only the
  unstarted lifecycle entry, and cannot create a nominal last-worker event
  while spawning continues.
- Every successfully started worker decrements exactly once. Normal completion,
  cancellation during creation, partial creation, and zero-active failure
  paths therefore have exactly one close claimant.

## Failure and thread-lifetime review

- One mutex-protected exception_ptr preserves the first pipeline failure. Its
  first transition publishes cancellation and wakes/cancels all budget, queue,
  ordered-writer, and reporter-related wait paths.
- Worker and writer wrappers hold shared completion state and publish it with
  notify_all_at_thread_exit, not a pre-return flag.
- The safe join helper catches system_error, records it as a failure, waits for
  native thread exit, retries join, and detaches only the already-exited handle
  as a final fallback. A completion-wait or detach failure terminates instead
  of permitting active detached access to stack-captured state.
- All created workers and the writer pass through safe join before captured
  pipeline state is destroyed or an exception is rethrown. The reporter is
  stopped without a summary on failure and finished once on success.

## Final static capacity-path audit

The eight-file boundary was reread after the bounded-scratch, lookup, and lifecycle fixes.
Static searches confirmed:

- no pipeline converted-record temporary, converted.bytes, nested per-sample
  owning storage, or post-return result admission remains;
- annotation find_checked contains no owning key construction and both lookup
  levels are string_view-keyed unordered maps built only after owned-map mutation;
- the converter declares exactly the seven documented owning scratch vectors;
- the only converter vector reserve is inside the checked generic helper, with
  admission immediately before and after;
- every vector push_back has an explicit corresponding ensure call;
- the only record-destination append and push_back are behind the checked
  output helper, whose sole destination reserve has pre/post admission;
- normal and passthrough exits both destroy all seven vector capacities before
  releasing scratch admission with actual output capacity and zero scratch;
- annotation view allocation has a saturating safety-factor preflight before
  reserve and an actual-bucket estimate check after construction;
- the pipeline callback includes input, output, and scratch with checked
  arithmetic and sequence-aware overage;
- result close can be claimed only through the spawning-finished lifecycle;
- reporter/pipeline detach paths remain guarded by at-thread-exit completion.

No successful per-record owning allocation remains outside the admitted result
destination and seven admitted scratch vectors.

## Not performed by instruction

No compiler, CMake configuration, build, linker, executable, test, benchmark,
installation, Git command, commit, worktree operation, or Python process was
used. C++ parsing, template instantiation, linkage, HTSlib integration,
byte-equivalence, fault injection, concurrency, liveness, and memory-pressure
behavior remain runtime-unverified.

Source authored and statically audited, but not compiled or tested at the
user's request.
