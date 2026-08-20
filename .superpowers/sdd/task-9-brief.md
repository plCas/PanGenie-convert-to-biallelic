# Task 9 Brief: Whole-Source Static Review and Handoff

## Governing documents

- `docs/superpowers/specs/2026-07-18-cpp-multithreaded-vcf-converter-design.md`
- `docs/superpowers/plans/2026-07-18-cpp-multithreaded-vcf-converter-source-only.md`

## Scope

Inspect the entire authored C++ source tree, CMake file, README, and unverified ledger. Reconcile declarations/definitions, includes, namespaces, source-list membership, CLI-to-main wiring, conversion semantics, memory accounting, concurrency/order invariants, HTSlib ownership, output transactions, and documentation claims. Fix static defects found. Create a concise `SOURCE_ONLY_HANDOFF.md` and update the progress ledger.

## Required checks

1. Inventory every `include/convert_to_biallelic/*.hpp` and `src/*.cpp`; ensure CMake lists every `.cpp` exactly once.
2. Cross-check declarations and definitions, constructors, move operations, namespaces, types, and visibly required includes.
3. Trace help/version and error exit mapping through `main`.
4. Trace HTSlib input and plain/BGZF output ownership and checked close behavior.
5. Trace the 2 GiB whole-job soft budget, bounded queues, dynamic chunks, ordered writer, cancellation, progress stdout, diagnostics stderr, and quiet behavior.
6. Compare converter logic statically with `convert-to-biallelic.py`, including header filters, annotation handling, MA/UK, GT/GQ, ordering, and malformed input behavior.
7. Trace transactional publication for normal non-adversarial output-directory operation on Windows and Linux.
8. Ensure README, UNVERIFIED, and handoff make no compile/test/performance/compatibility success claims.

## Constraints

- Do not install, configure, compile, link, execute, test, benchmark, invoke Python, invoke Git, or create commits/worktrees.
- Use read-only inspection commands and `apply_patch` for edits.
- Keep `convert-to-biallelic.py` byte-for-byte unchanged.
- Static reasoning only. Every runtime property remains unverified.

## Deliverables

- Any necessary source/doc fixes.
- `SOURCE_ONLY_HANDOFF.md`.
- `.superpowers/sdd/task-9-report.md`.
- `.superpowers/sdd/task-9-review-package.md` containing exact current copies needed for an independent final review.
- Updated `.superpowers/sdd/progress.md`.

