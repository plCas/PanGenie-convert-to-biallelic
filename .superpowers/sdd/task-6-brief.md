### Task 6: Author Memory Budget and Bounded Queue Primitives

Create `include/convert_to_biallelic/memory_budget.hpp`, `src/memory_budget.cpp`, and header-only `include/convert_to_biallelic/bounded_queue.hpp`. Source-only: no install/build/run/test/Git; Python unchanged.

Memory API requirements:

1. `MemoryPermit` is default-constructible, movable not copyable, RAII-releases exactly once, exposes `bytes() noexcept`, and `resize(uint64_t, bool allow_one_overage)`.
2. `MemoryBudget(uint64_t tracked_limit)` rejects zero.
3. `acquire(uint64_t bytes, bool allow_one_overage)` blocks under condition variable until capacity, cancellation, or eligible single overage.
4. Normal capacity condition must be overflow-safe (`bytes <= limit-current`, not unchecked addition).
5. At most one active overage permit. It may exceed the soft limit to let one worker finish; other overage claimants wait. Clear overage state when permit shrinks within normal capacity or releases.
6. Permit move transfers owner/bytes/overage and nulls source; move assignment releases current reservation before taking source.
7. `cancel()` is idempotent, sets cancellation, and wakes all; blocked acquire/resize throws. `current_bytes()` and `peak_bytes()` lock and report tracked values.
8. Destructors are noexcept. `resize` on empty/moved permit throws. Saturate peak defensively and avoid underflow in release.

Queue API requirements:

1. `template<class T> class BoundedQueue` constructed with positive item capacity.
2. Noncopyable; destructor cancels.
3. `void push(T item)` waits for space; throws on cancelled or closed.
4. `bool pop(T& out)` waits for item/close/cancel; cancellation throws; closed+empty returns false; closed with items drains FIFO first.
5. `close()` idempotently prevents new pushes and wakes all.
6. `cancel()` idempotently marks cancelled, clears queue so RAII permits release, and wakes all.
7. Never hold a queue lock while acquiring/resizing memory; document caller lock order.
8. Condition-variable waits use predicates; no busy loops.
9. All concurrency behavior remains statically unverified.

Use apply_patch. Static self-review every state transition, overflow path, and wait predicate. Write report and complete exact-file review package `.superpowers/sdd/task-6-report.md` and `task-6-review-package.md`. No tests/commits.
