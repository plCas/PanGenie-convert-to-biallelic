# Task 5 Python-Compatible Text Conversion Report

## Scope

Authored the source-only record-conversion API requested by Task 5:

- `include/convert_to_biallelic/converter.hpp`
- `src/converter.cpp`

Also authored the required full-file static review package in
`.superpowers/sdd/task-5-review-package.md`. The original
`convert-to-biallelic.py` was inspected and left unchanged.

## Public API

The new header declares, in namespace `ctb`:

- `ConversionResult`, containing output bytes and an output-record count;
- `convert_header(std::string_view)` for filtering or LF-terminating a header;
- `convert_record(std::string_view, const AnnotationIndex&, std::uint64_t)` for
  converting one tab-delimited input record with physical-line error context.

The implementation is stateless and performs only const annotation lookups.

## Implemented conversion semantics

- Drops a header when it contains any of the four required AF, AK, GL, or KC
  declaration substrings. Every retained header is copied exactly and receives
  one LF.
- Splits data records only on tab characters and requires at least nine fields.
- Parses INFO entries at their first equals sign. Keys retain first-encounter
  order while a duplicate replaces the earlier value, matching ordered
  last-value-wins dictionary behavior.
- Requires a nonempty `INFO/ID`, creates an empty REF mapping followed by the
  comma-delimited allele mappings, and parses each mapping's colon-delimited
  component IDs once.
- Passes through a record with exactly one comma-level mapping as soon as any
  component is absent from the chromosome annotation. The passthrough is the
  exact input line plus LF and reports one output record; FORMAT and samples are
  deliberately not parsed on this Python-compatible branch.
- Requires every component of any convertible record to resolve. IDs are
  deduplicated, paired with their annotation definitions, and sorted by
  annotation position and then ID.
- Starts each emitted record from the first nine input fields and replaces POS,
  the VCF ID column, REF, ALT, INFO, and FORMAT as specified. POS uses normalized
  decimal text from the signed annotation position.
- Emits INFO as `ID=<id>` followed only by MA and UK, in their first key
  encounter order and with the last value of any duplicate key.
- Detects exact `GT` and `GQ` FORMAT tokens once per input record. Output FORMAT
  is `GT` or `GT:GQ`.
- Splits every sample once on colons and its GT once on either slash separator.
  Sample structures are reused for all emitted IDs rather than reparsed inside
  the emitted-variant loop.
- Preserves ploidy, normalizes both `/` and `|` to `/`, preserves a `.` allele,
  and otherwise requires a fully consumed digits-only nonnegative index within
  the INFO allele-mapping range. Exact component membership produces `1`; all
  other valid allele mappings, including REF, produce `0`.
- Copies the selected GQ substring without numeric parsing or normalization.
- Appends one LF per emitted record and increments the output count once per
  emission.
- Wraps record conversion exceptions as `Input line N: <reason>`.

## Static comparison with Python lines 26-101

| Python lines | Python operation | Task 5 source-only mapping |
|---|---|---|
| 26-33 | Filter four header declaration substrings and print retained headers | `convert_header` applies the same four substring filters and returns retained text plus LF. |
| 34-40 | Split fields, parse INFO, require ID, create REF-plus-allele mapping | `convert_record_impl`, `parse_info`, `require_identifier`, and `parse_allele_mappings` perform these operations once with explicit structural validation. |
| 41-46 | Pass through a single unknown comma-level ID mapping | `resolve_variants` signals this branch when any colon component of the sole mapping is unknown; `passthrough` returns before FORMAT/sample parsing. |
| 47-56 | Collect unique IDs, retain annotation coordinate, and sort | `resolve_variants` resolves and deduplicates exact component IDs, then sorts by position and deterministic ID tie-break. |
| 57-78 | Copy the first nine fields, replace variant fields, retain MA/UK, and choose GT/GQ | The emitted-record loop copies fields 0-8 and replaces indices 1, 2, 3, 4, 7, and 8. `build_info`, `find_required_gt`, and `find_gq` implement the retained fields. |
| 79-100 | Convert each sample genotype and optionally copy GQ | `parse_samples` validates and stores each sample once; `convert_sample` performs exact component membership and slash-normalized rendering for each emitted ID. |
| 101 | Tab-join and print each record | `append_record` tab-joins all fields and appends LF while the caller increments `output_records`. |

## Deliberate stricter validation and deterministic behavior

The following differences are required by the Task 5 brief and replace Python
assertions, implicit exceptions, or ambiguous behavior with contextual errors:

- C++ record parsing is tab-specific and requires all nine fixed VCF fields,
  rather than Python whitespace splitting with only an eight-field assertion.
- INFO is split at the first equals sign and retains the complete remaining
  value. Duplicate key updates do not move that key's encounter position.
- Missing or empty ID values and empty comma/colon component IDs are rejected.
- GT and GQ are recognized only as exact colon-delimited FORMAT tokens; Python's
  surrounding substring checks could accept misleading text before a later
  failure.
- Every required sample FORMAT position is checked. GT alleles must be
  digits-only, fully consumed, nonnegative, representable as `std::size_t`, and
  within the allele-mapping range.
- Missing annotations are errors for convertible records; only the explicitly
  permitted one-mapping unknown-ID branch passes through.
- Coordinate ties are ordered by variant ID after position. Python converts a
  set to a list and sorts only by position, leaving equal-coordinate order
  dependent on set iteration. The C++ tie-break makes the output deterministic.

These stricter checks and the coordinate-tie rule are intentional. Runtime byte
equivalence to Python remains unverified.

## Static review performed

- Compared the implementation line by line with the requirements and Python
  lines 26-101.
- Reconciled the two public declarations with one definition each and checked
  the `ConversionResult` fields against the requested API.
- Checked standard-library includes against the visible types and algorithms.
- Checked that all `std::string_view` values refer to the caller-owned record
  only during the synchronous conversion call; returned bytes own their text.
- Checked that INFO, FORMAT indices, mapping components, sample fields, and GT
  allele indices are parsed outside the emitted-ID rendering loop.
- Checked that the existing CMake source list already names `src/converter.cpp`;
  CMake was inspected only and not changed.
- Checked that annotation access is const and no converter-global mutable state
  was introduced.
- Before the checked-lookup finding, requested an independent read-only static
  pass over the original converter delivery, annotation API, and Python lines
  26-101. The follow-up lookup fix is documented and statically inspected in the
  appended section below; runtime properties remain outside both reviews.

## Not performed by instruction

No test was authored or executed. No compiler, CMake configuration, build,
linker, executable, Python oracle, benchmark, installation, Git command, commit,
or worktree operation was used.

## Concerns and deferred verification

- C++17 compilation, standard-library API availability, linker integration, and
  behavior in a real pipeline remain unverified.
- Python/C++ differential byte equivalence, including duplicate INFO keys,
  composite mappings, equal-coordinate IDs, missing alleles, mixed separators,
  and empty GQ text, remains unverified.
- The legacy `AnnotationIndex::find` still returns `nullptr` on lookup-key
  allocation failure to preserve its `noexcept` API. Conversion now uses the
  additive throwing `find_checked` API instead; its intended exception
  propagation and line-context translation remain runtime-unverified.
- Memory use and the cost of linear first-order INFO/ID deduplication have not
  been measured.
- Malformed text embedded in IDs or INFO values is validated only to the extent
  required by this task; full VCF semantic validation is deferred.

## Important review fix: checked annotation lookup

- Added `AnnotationIndex::find_checked(chromosome, id)` as a const, potentially
  throwing lookup. It returns `nullptr` only after the chromosome or ID lookup
  completes and genuinely finds no matching key.
- Moved the two temporary C++17 string-key constructions into `find_checked`, so
  allocation and other standard-library lookup exceptions are not collapsed
  into absence.
- Reimplemented the existing `find(...) noexcept` as a compatibility wrapper
  around `find_checked` with the original catch-all-to-`nullptr` behavior.
- Changed Task 5 resolution to use `find_checked`. A genuine absent component
  still drives sole-mapping passthrough or the existing convertible-record
  missing-annotation error. A lookup exception now exits resolution and is
  translated by `convert_record` to `Input line N: <reason>`.
- Statically checked one declaration and one definition for each lookup, the
  preserved `noexcept` signature on `find`, the absence of a catch in
  `find_checked`, and the converter call site. No runtime exception injection or
  compilation was permitted.
