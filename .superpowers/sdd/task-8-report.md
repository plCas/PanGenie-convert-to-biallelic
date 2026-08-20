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
