# Task 1 report: source tree and shared types

## Status

DONE

## Files authored

- `CMakeLists.txt`
- `include/convert_to_biallelic/types.hpp`
- `README.md`
- `UNVERIFIED.md`

## Static checks

- Inspected the authored CMake source list against the task brief: it contains
  all eight required future `ctb_core` sources and `src/main.cpp` for
  `convert-to-biallelic`.
- Inspected `types.hpp`: the required `ctb` namespace, one enum, and four structs,
  member names, types, and default values match the task brief.
- Inspected the README usage: it includes `--variants`, `--input`, `--output`,
  `--threads`, and `--memory-limit`, and links to `UNVERIFIED.md`.
- Inspected `UNVERIFIED.md`: it explicitly lists all requested unverified
  compilation, execution, compatibility, profiling, benchmarking, and
  Windows/Linux boundaries.
- Confirmed by source inspection that `convert-to-biallelic.py` was not edited.

## Self-review findings

No source-level discrepancies identified against Task 1's explicit interface
and project-description requirements.

## Concerns

The declared source files and HTSlib/CMake configuration are intentionally
unverified. No compiler, CMake configuration, executable, test, benchmark,
dependency installation, worktree, or Git command was run by instruction.

## Review fix

- Replaced the README and UNVERIFIED review-package summaries with their
  complete contents in fenced blocks.
- Corrected the shared-type count to one enum and four structs.
- Changed the project language list to `CXX` because this project's declared
  sources are C++ only; HTSlib is consumed as an imported dependency.
- Statically inspected the changed CMake line and review/report artifacts. No
  build, execution, test, installation, worktree, or Git action was performed.
