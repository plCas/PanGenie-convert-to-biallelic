# Source-only handoff

This repository contains authored C++17/HTSlib source for the intended
`convert-to-biallelic` command. The whole authored source boundary has received
a static text review against the governing design, source-only plan, and
Python oracle. Task 9 did not edit the oracle.

The reviewed current oracle bytes have SHA-256
`4907B5D1D4DBA72CD38B75A578BB491EA09C1FC8050A661B860FB6D8A52BD80D`.
This identifies the file included in the review package; without a Git or other
independent baseline it is not a claim about historical identity.

The authored lifecycle is: parse CLI; load and checked-close the annotation;
open the conversion input once; allocate conversion and compressed-I/O worker
budgets; create a retained-identity output transaction; run a bounded,
sequence-ordered pipeline; checked-close the input and output; publish the
destination; then print the final stdout summary unless quiet. Errors and the
single exceptional-memory warning are intended for stderr.

Task 9 corrected static defects in input ownership, progress-interval units,
site-only unknown-ID passthrough, whole-pipeline in-flight bounding,
exceptional-memory diagnostics, incremental annotation accounting, Windows
rename-buffer object lifetime/alignment, default scalar initialization,
reserve-time double-allocation admission, final peak-memory reporting, and the
planned C/C++ CMake language declaration.
See `.superpowers/sdd/task-9-report.md` for the complete audit record.

The Python byte-equivalence scope excludes one statically known case: when one
record emits distinct IDs with the same annotation position, Python leaves the
tie dependent on hash-set iteration, while the C++ source intentionally orders
the tie by ID. The C++ order is deterministic but is not claimed to match the
oracle for that case.

Source authored but not compiled or tested at the user's request. Compiler and
linker success, HTSlib compatibility, Windows/Linux behavior, byte equivalence,
round trips, concurrency and deadlock freedom, memory behavior, progress
routing, transactional fault behavior, and performance remain unverified. See
`UNVERIFIED.md` for the explicit deferred-verification ledger.
