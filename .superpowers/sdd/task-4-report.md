# Task 4 Annotation Index Report

## Scope

Created the source-only immutable annotation index requested by Task 4:

- `include/convert_to_biallelic/annotation_index.hpp`
- `src/annotation_index.cpp`

No existing project source was modified. `convert-to-biallelic.py` was not changed.

## Implemented behavior

- Loads through `InputSource::getline` in one physical-line pass and skips lines whose first character is `#`.
- Splits annotation records on tab characters without regular expressions and requires at least eight fields.
- Requires a fully consumed, positive signed 64-bit POS parsed with `std::from_chars`.
- Scans semicolon-delimited INFO entries for exactly one nonempty `ID=` value and rejects comma-containing IDs.
- Stores chromosome, ID, POS, REF, and ALT. `insert_or_assign` gives duplicate chromosome/ID pairs last-record-wins semantics.
- Recalculates unique-key count and a conservative memory estimate after every insertion or overwrite.
- Rejects memory limits below 64 MiB and reports the source physical annotation line when a data-line parse or estimate failure occurs.
- Exposes only const lookup and accounting accessors after loading. `find` uses C++17 temporary `std::string` keys because this `unordered_map` setup does not provide transparent lookup.

## Memory-accounting boundary

The estimate includes map object storage, bucket-pointer arrays, approximated hash-node links/payloads, and capacities plus terminators for all stored strings. It is deliberately conservative and saturates rather than performing unsigned subtraction. It reserves ten percent of the supplied memory limit and rejects the index when the estimate is greater than the remaining 90 percent allowance.

This is not, and is not claimed to be, an exact resident-set-size measurement.

## Static review performed

- At the original Task 4 delivery, public declarations in the new header matched the three out-of-line method definitions and `load_annotation` in the new source; the later additive checked method is documented below.
- The header obtains the complete `InputSource` declaration from `vcf_io.hpp` and includes all standard-library types it exposes.
- All parsing and accounting helper functions are internal to `ctb`'s anonymous namespace.
- The project CMake source list already contains `src/annotation_index.cpp`; it was inspected only and not changed.
- No public mutator is present on `AnnotationIndex`; concurrent read safety is by immutable post-load use of the standard containers.

## Not performed by instruction

No test was authored or executed. No compiler, CMake configuration, build, linker, benchmark, installation, Git command, commit, or worktree operation was performed. Python remains unchanged.

## Concerns and deferred verification

- C++17/standard-library compilation, especially the exact unordered-map allocation shape and `std::from_chars` availability, remains unverified.
- HTSlib-backed `InputSource` behavior and input error propagation remain unverified.
- Runtime memory use can exceed this intentionally approximate index-only estimate because allocator, `InputSource`, and process overhead are outside it.
- The `find` method must allocate temporary C++17 key strings for non-SSO views; it returns `nullptr` if that allocation fails to preserve its required `noexcept` contract.
- No runtime check has established parsing compatibility, duplicate behavior, thread safety in actual callers, or memory-limit behavior.

## Review fix

- Replaced the review package's summaries with exact fenced copies of the Task 4 header and source, and added a no-Git-base note.
- Statically re-inspected the over-limit path: it throws from inside `load_annotation` before the local `AnnotationIndex` can be returned, so no over-budget index escapes the loader.
- Statically re-inspected duplicate handling: `insert_or_assign` replaces the value at an existing chromosome/ID key and `count_variants` recomputes the sum of nested-map sizes, so the count remains a unique-current-key count.
- Statically re-inspected `find`: its `try` block begins before either temporary `std::string` key is constructed, preserving the required `noexcept` behavior for those allocations.
- Statically re-inspected parsing failures: data-line validation and accounting failures are caught and rethrown through `annotation_line_error` with the physical line number. The pre-read 64 MiB configuration error has no annotation line because no physical annotation line has yet been read.

## Error-context review fix

- Moved every `InputSource::getline` call inside the per-line `try` block. A read exception now becomes `Annotation line <next physical line>: <reason>`, matching data-line validation and accounting errors.
- Moved physical-line-counter overflow into the same `try` block. It is translated by the same `annotation_line_error` path as `Annotation line <maximum representable line>: physical line number overflow`.
- Preserved EOF behavior: a false `getline` result breaks from the loop before assigning `line_number`, parsing, or indexing.
- Updated the exact fenced source copy in the review package to match the loader change.

## Cross-task checked lookup addition

Task 5 review exposed that the original `find(...) noexcept` intentionally
collapses temporary-key allocation failures into `nullptr`, which is suitable
for its preserved Task 4 contract but is ambiguous to conversion code that uses
`nullptr` as genuine annotation absence.

- Added the const, potentially throwing `find_checked(chromosome, id)` API. It
  performs the same nested lookup and returns `nullptr` only for a genuinely
  absent chromosome or ID; temporary-key allocation and lookup exceptions
  propagate.
- Reimplemented the original `find(...) noexcept` as a `try`/`catch` wrapper
  around `find_checked`, preserving its signature and prior failure behavior.
- Task 5 conversion uses `find_checked`; no Task 4 loader, accounting, duplicate,
  or immutability behavior changed.
- The Task 4 review package remains the historical exact package for the Task 4
  delivery. The current exact annotation header/source are included in the Task
  5 review package that introduced this additive API.
- This cross-task change received static inspection only. Compilation and
  allocation-failure behavior remain unverified by instruction.

## Task 7 allocation-free lookup extension

Task 7's complete converter-capacity audit found that `find_checked` still
created owning chromosome and ID key strings on every successful record
lookup. The immutable index now builds a secondary view index after all owned
annotation insert/overwrite operations are complete.

- The secondary structure remains two-level `unordered_map`, keyed by
  `string_view`, with no-throw view hash and equality functors. It references
  the chromosome/ID strings and `VariantDefinition` nodes owned by the original
  maps; no ordered-map or logarithmic lookup replacement was introduced.
- Outer and per-chromosome lookup maps reserve their final entry counts before
  insertion. `find_checked` now performs only view lookups and creates no
  owning key strings. The original noexcept `find` continues to delegate and
  catch.
- The owned maps are never mutated after lookup publication. Copy is disabled;
  move construction relies on the standard default-allocator unordered-map
  guarantee that references to transferred nodes remain valid. Declaration
  order destroys the view lookup before the owned nodes.
- Before secondary-map allocation, a saturating preflight adds projected
  outer/inner nodes and bucket arrays using a two-times-plus-one bucket-rounding
  safety factor to the owned-index estimate. It rejects an over-allowance index
  before reserve. The estimate is then recalculated from actual bucket counts
  after construction and checked again against the 90% allowance. Either
  failure unwinds and destroys the local index before `load_annotation`
  returns; neither estimate is presented as a hard RSS bound.
- Physical-line overflow is now tested only after a successful `getline`.
  Exactly `UINT64_MAX` physical lines may therefore reach EOF; a successfully
  read additional line is rejected, while read/parse errors retain a saturated
  next-line context value.
- This supersedes the earlier temporary-key allocation concern while
  preserving last-record-wins data, unique counts, immutable concurrent reads,
  and the intentionally approximate rather than exact-RSS estimate.
- The historical Task 4 package remains unchanged. The current exact
  annotation header/source are included in the Task 7 eight-file review
  package. This extension received static inspection only; compilation and
  runtime lookup/accounting behavior remain unverified by instruction.
