# Unverified status

This source has been locally compiled and linked in the existing build tree,
and the registered deterministic Python parity test has passed in this
workspace. It has not been memory-profiled, benchmarked, or verified on
Windows. Linux verification beyond the local smoke and parity commands remains
limited.

The CMake configuration and authored HTSlib integration still need broader
environment verification before production use. Compatibility, correctness,
memory behavior, and performance must be verified on representative inputs.

The deferred verification work is explicit:

- compiler and linker success outside this local build tree;
- HTSlib 1.17+ API and ABI compatibility;
- Windows and Linux configuration, build, and execution;
- Python byte equivalence on large representative inputs within the documented
  compatibility scope;
- plain `.vcf` and BGZF `.vcf.gz` output round trips;
- multithreaded ordering, cancellation, liveness, and deadlock freedom;
- whole-job soft memory-limit and exceptional-overage behavior;
- progress routing to stdout, diagnostics routing to stderr, interval timing,
  and quiet behavior;
- transactional publication and cleanup under success, collision, race, and
  injected-failure paths; and
- throughput, compression behavior, scaling, and peak resident memory.

In strict mode, equal-position multi-ID records are not a Python-equivalence
claim: strict mode intentionally orders them by position then ID, whereas the
Python oracle's position-only sort leaves ties dependent on hash-set iteration.
In Python compatibility mode, the parity test fixes `PYTHONHASHSEED=0`; byte
identity for equal-position ties is not guaranteed unless the oracle order is
also fixed.

Transactional output is also unverified. Linux prefers `O_TMPFILE`, attempts
Linux-specific `AT_EMPTY_PATH`, and falls back on the documented unprivileged
`/proc/self/fd/<fd>` plus `AT_SYMLINK_FOLLOW` route when capability is absent.
Availability and behavior across kernels, filesystems, procfs mount/security
settings, and protected-hardlink policies must be validated. Filesystems
without `O_TMPFILE` use a linked exclusive temporary entry with immediate
pre-publication device/inode verification; mismatch/error cleanup and possible
safe orphaning require fault injection. Windows `DuplicateHandle`, CRT
descriptor adoption, handle-based no-force rename, verified-path forced
`MoveFileExW` replacement with write-through, and disposition cleanup likewise
require native fault-injection verification.

The intended transactional threat model is normal, non-adversarial operation,
including failures, temporary-name collisions, and concurrent creation of the
final destination. The output directory must not be modified by an adversarial
process during a transaction. Windows forced `MoveFileExW` and Linux named or
stage fallback publication are pathname-based; their immediate identity checks
are best-effort mismatch detection, not hardening against same-directory
substitution between check and publication.
