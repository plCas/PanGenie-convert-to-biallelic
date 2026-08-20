# Task 2 static implementation report

## Authored files

- `include/convert_to_biallelic/cli.hpp`
- `src/cli.cpp`
- `.superpowers/sdd/task-2-review-package.md` (complete source snapshot for review;
  no Git base/head comparison because Git use is prohibited for this source-only task)

## Static interface review

- `Config` exposes the three filesystem paths, thread budget, 2 GiB memory default,
  5000 ms progress default, quiet/force flags, and `OutputFormat` from shared types.
- `UsageRequested` retains the requested help/version mode and provides the specified
  exception message. The declared public functions are `parse_cli`, `allocate_threads`,
  and `usage_text` in namespace `ctb`.

## Static parser and allocation branch review

- Every option named in `usage_text()` has a parser branch: required `--variants`,
  `--input`, `--output`; optional `--threads`, `--memory-limit`,
  `--progress-interval`, `--quiet`, `--force`, `--output-format`, `--help`, and
  `--version`.
- The parser rejects absent option values, duplicate options, unknown/positional
  arguments, zero or out-of-range thread counts, memory values below 64 MiB,
  malformed/overflowed decimal values, overflowed K/M/G byte conversion, negative
  progress intervals, and unsupported inferred output extensions.
- Output inference accepts `.vcf` and `.vcf.gz` case-insensitively. Explicit format
  overrides accept only `vcf` or `vcf.gz`. Missing `--threads` uses logical CPU count
  with a fallback of one worker.
- Allocation reserves an output I/O worker for compressed output only when more than
  one total worker is available; it reserves an input I/O worker for compressed input
  at total thread budgets of four or more only if a conversion worker remains. The
  remaining workers are conversion workers, with a defensive minimum of one.

## Verification status and concerns

No installation, configuration, compilation, execution, tests, benchmarks, Git
commands, or commits were performed, as directed. API and runtime behavior remain
unverified. Static-only concern: platform/compiler integration and actual command-line
behavior still require a later build and test pass.

## Review fix: terminal option validation

- `--help` and `--version` are now recorded while parsing, rather than throwing
  immediately. This ensures all later command-line tokens are parsed, including
  duplicate and unknown option rejection.
- After token parsing, simultaneous `--help` and `--version` produces a clear
  `std::invalid_argument`; otherwise the corresponding `UsageRequested` is thrown
  before required-input validation. The review package was synchronized with the
  exact updated source. This remains static-only and unverified.
