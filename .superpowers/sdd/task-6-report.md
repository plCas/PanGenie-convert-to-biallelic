# Task 6 Memory Budget and Bounded Queue Report

## Scope

Authored the three source-only Task 6 primitives:

- `include/convert_to_biallelic/memory_budget.hpp`
- `src/memory_budget.cpp`
- `include/convert_to_biallelic/bounded_queue.hpp`

Also authored the required full-file static review package in
`.superpowers/sdd/task-6-review-package.md`. The Python implementation was not
changed.

## Public APIs

In namespace `ctb`, the memory API provides:

- default-constructible, movable, noncopyable `MemoryPermit`;
- `MemoryPermit::bytes() noexcept` and blocking/cancellation-aware
  `MemoryPermit::resize(uint64_t, bool)`;
- positive-limit `MemoryBudget` construction;
- `MemoryBudget::acquire(uint64_t, bool)`, `cancel()`, `current_bytes()`, and
  `peak_bytes()`.

The header-only queue API provides noncopyable and nonmovable
`BoundedQueue<T>` with a positive item capacity and `push`, `pop`, `close`, and
`cancel`. Instantiation requires nothrow move construction, nothrow move
assignment, and nothrow destruction for `T`. Its destructor is explicitly
`noexcept` and cancels the queue.

## Memory accounting model and invariants

All aggregate budget state is read or changed under `MemoryBudget::mutex_`.
The static review used these invariants:

1. Every nonempty permit contributes exactly `permit.bytes_` to the budget that
   owns it.
2. `overage_active_` is true only while one permit carries `overage_ == true`.
   A second non-normal admission cannot occur until that permit is normalized
   or released.
3. With no active overage, the aggregate is at most `limit_`.
4. The exact aggregate during one overage is at most twice `UINT64_MAX`, because
   the pre-overage aggregate is at most the uint64 limit and the one overage
   permit is itself at most uint64.
5. `current_high_` is the aggregate's 2^64 bit and `current_` is its low 64
   bits. Public `current_bytes()` and `peak_bytes()` saturate at
   `UINT64_MAX` when the exact aggregate cannot be represented by their public
   return type.
6. A nonempty permit's budget must outlive it, and one permit object is
   thread-confined or externally serialized. These raw-owner lifecycle
   preconditions are documented in the public header.

## Permit move and release transition review

| Transition | Reservation before | State change | Reservation after |
|---|---:|---|---:|
| Default construction | 0 | `owner_ == nullptr`, zero bytes, not overage | 0 |
| Move construction | source reservation | Destination copies owner/bytes/overage; source is nulled and zeroed | Unchanged, owned only by destination |
| Self move assignment | current reservation | Explicit identity check returns without release | Unchanged |
| Non-self move assignment | target plus source reservations | Target calls its no-throw release first, then takes source state; source is nulled and zeroed | Only the former source reservation remains in target |
| Destruction/release of empty or moved-from permit | 0 | Null-owner guard returns | 0 |
| Destruction/release of normal permit | permit bytes | Under lock, subtract without unsigned wrap; then wake waiters | Decreased once |
| Destruction/release of overage permit | permit bytes and one active overage | Subtract, clear `overage_active_`, then wake all | Decreased once; overage slot available |

`MemoryPermit::release()` nulls owner, bytes, and overage state after the budget
release. Move sources are also nulled. Consequently no normal destructor path
can release a transferred reservation twice. Both the permit destructor and
the queue destructor are explicitly `noexcept`.

## Acquire and resize transition review

### Acquire

- Construction rejects a zero tracked limit with `invalid_argument`.
- Cancellation is checked under the lock before any special case or wait.
- A zero-byte request returns a valid normal permit immediately after that
  cancellation check. It never waits behind an active overage and never claims
  the overage slot.
- The nonzero acquire predicate is cancellation, overflow-safe normal fit, or
  an explicitly allowed and currently unclaimed overage slot.
- Cancellation is checked again after a nonzero waiter wakes and throws before
  accounting.
- A normal reservation adds bytes without marking an overage.
- A non-normal allowed reservation adds bytes and atomically claims
  `overage_active_` while the same lock is held.
- A zero-byte permit can be resized and its destructor performs a zero-byte
  release exactly once.

### Resize

| Requested transition | Admission/action under the budget lock | Wake behavior |
|---|---|---|
| Empty or moved-from permit | `MemoryPermit::resize` throws `logic_error` before dereferencing an owner | None |
| Any resize after budget cancellation | Throws `runtime_error`; no permit or accounting state changes | Cancellation already woke all |
| Equal size, normal permit | Zero subtraction; no state change | None |
| Equal size, active permit now within normal aggregate | Zero subtraction, clear permit/global overage state | Wake all overage claimants |
| Shrink, aggregate still over limit | Subtract exact delta; retain active overage | Wake all because capacity improved |
| Shrink into normal aggregate | Subtract exact delta; clear permit/global overage | Wake all normal and overage waiters |
| Grow that fits normally | Wait predicate admits using `delta <= limit-current`; add delta | No capacity-increasing wake |
| Normal permit grows beyond limit with overage allowed and free | Atomically add delta and mark this permit/global state overage | No capacity-increasing wake |
| Normal permit requests overage while another owns it | Predicate remains false until normal fit, cancellation, or overage release/normalization | Existing owner shrink/release wakes all |
| Active overage permit grows with overage allowed | Existing ownership makes it eligible; it retains the sole slot | No capacity-increasing wake |
| Active overage permit grows without overage permission | Waits until the delta fits normally or cancellation occurs | Releases/shrinks/cancel wake it |
| Active permit grows after other releases make the delta normal | Add normally and clear its stale overage ownership | Wake competing overage claimants |

The resize delta is formed only on the `new > old` branch, so subtraction cannot
underflow. Shrink/release subtraction uses the exact high/low representation;
the non-high defensive fallback clamps an impossible oversized subtraction to
zero instead of wrapping.

## Overflow and saturation review

- Normal capacity never evaluates unchecked `current + request`. It first
  requires a non-high aggregate with `current_ <= limit_`, then evaluates
  `request <= limit_ - current_`.
- Crossing `UINT64_MAX` computes the low word as
  `bytes - (UINT64_MAX - current_) - 1` and sets `current_high_`.
- Adding while high stays within the valid two-uint64 aggregate bound. A
  defensive corrupted-state branch clamps the low word instead of wrapping.
- Subtracting while high either decreases the low word or borrows from the high
  bit with `UINT64_MAX - (bytes - current_ - 1)`.
- Any high aggregate sets peak to `UINT64_MAX`; peak remains saturated after a
  later shrink. A representable aggregate updates peak using `std::max`.
- Public current reporting also returns `UINT64_MAX` while high; after exact
  release brings the aggregate below 2^64, the reported value again becomes
  the exact low word.

## Condition-variable predicate review

| Wait | Predicate | Post-wake precedence |
|---|---|---|
| Nonzero memory acquire | cancelled OR normal fit OR allowed free overage | Cancellation throws before reservation; zero bypasses this wait only after an initial cancellation check |
| Memory grow-resize | cancelled OR normal delta fit OR allowed ownership/free overage | Cancellation throws before resize |
| Queue push | cancelled OR closed OR item count below capacity | Cancellation throws first, then closure; otherwise push |
| Queue pop | cancelled OR closed OR queue nonempty | Cancellation throws first; closed+empty returns false; otherwise pop front |

Every wait uses the predicate overload of `condition_variable::wait`; there is
no polling or busy loop. Capacity-releasing memory transitions notify all.
Queue push/pop notify the opposite waiter class after changing item count, and
queue close/cancel notify both waiter classes. A failed queue insertion restores
one `not_full_` wake because it consumed a capacity wake without consuming the
available slot.

## Queue state and FIFO transition review

- Construction rejects zero item capacity.
- Class-level assertions reject element types whose move construction, move
  assignment, or destruction can throw. These are the exact element operations
  performed across the queue's lock-sensitive transitions.
- `push(T item)` waits only while the queue is open, not cancelled, and full.
  It moves the item to the deque back, preserving FIFO insertion order. If the
  deque insertion itself throws, such as on allocation failure, the catch path
  unlocks, notifies one producer on `not_full_`, and rethrows.
- `pop(T&)` waits only while open, not cancelled, and empty. It move-constructs
  the front item under the queue lock, removes that front, unlocks, advertises
  the free slot, and only then move-assigns the caller's output. Both element
  operations are statically required to be nonthrowing.
- `close()` is idempotent, retains queued items, rejects every later push, and
  wakes producers and consumers. Repeated pops drain front-to-back; only the
  closed-and-empty state returns `false`.
- `cancel()` is idempotent and takes precedence over closure. It marks the
  state under lock, swaps the live FIFO with an empty discard buffer, unlocks,
  immediately wakes both waiter classes, and only then destroys all discarded
  items so their memory permits release.
- The destructor calls `cancel()`, so any undrained values are discarded by
  the same outside-lock release path.
- If a push/pop linearizes under the queue lock immediately before a concurrent
  cancellation, that already completed queue transition may succeed; all
  operations that observe cancellation throw.

## Lock-order review

The header documents that permit acquisition and resizing must happen outside
queue operations and that users must stop before queue destruction. The queue
does not know or call `MemoryBudget` directly.

Two implementation details prevent known RAII releases from nesting a memory
lock inside the queue lock:

1. `pop` transfers the front to a local object under the queue lock, but assigns
   to `out` only after unlocking. Replacing a caller-held permit therefore
   releases that older reservation outside the queue lock.
2. `cancel` swaps queued values into a preconstructed discard buffer under the
   queue lock and clears that buffer only after unlocking. Cancelling queued
   chunks therefore releases their permits outside the queue lock.

For the pipeline's intended movable chunk types, moving into/out of the deque
only transfers a `MemoryPermit` and nulls its source without locking the budget.
The compile-time nothrow checks make those element transitions nonthrowing. The
header also states that a custom element's move construction or moved-from
destruction must not acquire/resize memory while the queue owns its lock;
`noexcept` alone cannot enforce lock ordering. Runtime deadlock behavior remains
unverified.

## Static review performed

- Reconciled every requested public declaration with its visible definition or
  header-only implementation.
- Inspected every move, release, resize, overage, cancellation, close, and wait
  transition listed above.
- Checked standard-library includes against the visible types and operations.
- Checked that the existing CMake source list already names
  `src/memory_budget.cpp`; CMake was inspected and not changed.
- Requested a fresh read-only arithmetic/concurrency audit. Its actionable
  deque moved-from-state concern was fixed by using a preconstructed discard
  buffer and exact swap; the public permit lifetime and serialization
  preconditions were confirmed in the header.

## Not performed by instruction

No test was authored or executed. No compiler, CMake configuration, build,
linker, executable, Python process, benchmark, installation, Git command,
commit, or worktree operation was used.

## Concerns and deferred verification

- C++17 compilation, template instantiation with actual pipeline chunk types,
  linking, and platform-library behavior remain unverified.
- Blocking, wake-up, cancellation, one-overage liveness, FIFO behavior, and
  absence of deadlock/data races remain concurrency-runtime unverified.
- The owner budget must outlive all of its permits; destroying a budget first
  violates the documented raw-owner precondition. Individual permit operations
  likewise require thread confinement or external serialization.
- `BoundedQueue<T>` deliberately rejects throwing element move/destruction
  types. Actual future pipeline chunk instantiation against those assertions is
  compilation-unverified.
- Deque insertion can still throw for allocation or container reasons. The
  source restores a consumed producer wake before propagating, but this failure
  path remains runtime-unverified.
- Condition variables do not promise fairness. The implementation bounds
  active overages but does not guarantee acquisition order among waiters.
- Destruction of a queue or budget while another thread is using or waiting on
  it violates the documented synchronization-object lifetime discipline.
- As with standard RAII synchronization wrappers, a low-level mutex failure in
  a `noexcept` cleanup path would terminate rather than propagate.

## Review-fix evidence

The follow-up review requested six changes. Each was applied and statically
traced as follows:

1. `BoundedQueue<T>` now has three class-level `static_assert` checks for
   nothrow move construction, move assignment, and destruction. The lock-order
   comment explains both why queue transitions require them and why they do not
   permit element operations to acquire/resize the memory budget.
2. `push` now retains its `unique_lock` across admission and insertion. Its
   catch-all insertion-failure path unlocks, calls `not_full_.notify_one()`, and
   rethrows, restoring the wake consumed for the still-free slot.
3. The pop-front move construction and output move assignment satisfy the new
   nonthrowing constraints. Assignment remains after the queue unlock so an old
   permit held by `out` releases outside the queue critical section.
4. `cancel` still marks cancellation and swaps the live FIFO under the mutex.
   After unlocking it now notifies both condition variables before clearing the
   discard buffer, so queue waiters observe cancellation without waiting for
   permit destruction and memory-lock contention.
5. `MemoryBudget::acquire` now checks cancellation immediately under its lock,
   then returns a normal zero-byte permit without entering the condition
   variable wait or touching `overage_active_`. Nonzero behavior retains the
   original predicate and post-wake cancellation check.
6. The Task 6 exact-file package was refreshed from the current three source
   files after these edits. No incomplete-work marker remained in the source,
   report, or package during the permitted static scan.

A fresh read-only follow-up audit reported no remaining Critical, Important, or
Minor issue in these six transitions. Its conclusions remain static only.

No runtime evidence was produced: compilation, tests, execution, and Git remain
prohibited for this source-only fix pass.
