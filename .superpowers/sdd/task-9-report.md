# Task 9 Whole-Source Static Review Report

## Scope and constraints

The review inspected the governing design and source-only plan, CMake metadata,
all 10 public headers, all 9 C++ translation units, README, UNVERIFIED ledger,
progress ledger, prior source-only handoff context, and the Python oracle.

Only read-only text inspection and `apply_patch` were used. No dependency was
installed; no configuration, compilation, linking, executable, test, benchmark,
Python process, Git command, commit, or worktree was used. The Python oracle was
not edited.

The current reviewed `convert-to-biallelic.py` bytes have SHA-256
`4907B5D1D4DBA72CD38B75A578BB491EA09C1FC8050A661B860FB6D8A52BD80D`.
This is a review-file identity only; because Git and any independent baseline
were excluded, it does not prove historical byte identity.

## Inventory and declaration reconciliation

- `include/convert_to_biallelic/` contains exactly 10 `.hpp` files.
- `src/` contains exactly 9 `.cpp` files.
- `CMakeLists.txt` lists the eight non-main `.cpp` files exactly once in
  `ctb_core`, lists `src/main.cpp` exactly once in the executable, and enables
  the planned C and CXX languages.
- Public declarations and out-of-line definitions were reconciled for names,
  namespaces, signatures, constructors, moves, and types. Template queue
  definitions remain in their header.
- Visible standard-library and platform includes were reviewed. The Windows
  rename-buffer repair adds its directly required `<algorithm>` and `<new>`
  headers.

Compilation remains unverified, so this reconciliation is not a compiler or
linker result.

## Static source defects corrected

The following six whole-job defects were prioritized and corrected:

1. **Conversion input opened twice.** `main` previously opened a probe and then
   reopened the conversion input, contrary to the one-open design invariant.
   It now passes the compressed-input worker candidate to the single
   `InputSource` construction; plain input ignores that candidate, and final
   allocation returns the unused unit to conversion.
2. **Unchecked HTSlib input close.** `InputSource` previously discarded every
   `hts_close` result. A checked, idempotent `close()` now marks the handle
   closed before calling HTSlib, while the destructor remains best-effort for
   exception unwinding. Annotation close is checked before its index is
   returned, and conversion input close is checked before output publication.
3. **Wrong CLI interval units.** `--progress-interval` previously interpreted
   its integer as milliseconds. It now accepts whole seconds as specified,
   performs checked seconds-to-milliseconds conversion, defaults to 5 seconds,
   and documents `SECONDS` in help.
4. **Eight-column unknown record rejected.** The Python oracle can pass through
   a site-only, eight-column record with one unknown ID before touching FORMAT.
   Conversion now requires eight fields initially, performs unknown-ID
   passthrough, and requires FORMAT only for a record that will be expanded.
5. **Queue-local rather than whole-pipeline bound.** The writer can drain a
   bounded result queue into its reorder map, so bounded queues alone did not
   cap submitted-but-unwritten chunks. The reader now waits for a written slot
   before beginning another chunk and limits the complete pipeline to twice
   the conversion-worker count.
6. **Silent output-side memory overage.** An exceptional permit used while a
   required result or its scratch storage grew could exceed the tracked
   allowance without a diagnostic. A synchronized, once-per-job reporter now
   emits one complete stderr warning for either an oversized input record or
   result/scratch growth that actually takes tracked memory over the allowance.

Three additional static defects were corrected:

7. **Quadratic annotation accounting.** The loader recomputed the unique count
   and rescanned the complete nested index after every record. It now updates
   the count and owned-map estimate from actual node, string-capacity, and
   bucket-count deltas; full estimates remain at lookup preflight/completion.
8. **Windows rename-buffer alignment and lifetime.** A byte vector was cast to
   `FILE_RENAME_INFO*`, which did not guarantee alignment or establish a C++17
   object lifetime. The variable-size buffer now uses `max_align_t` storage,
   placement construction, checked byte sizing, and offset-based name copying.
9. **Indeterminate default position.** `VariantDefinition::position` now
   defaults to zero, matching the governing interface and removing a latent
   uninitialized public scalar.

The final exact-package review corrected three further static defects:

10. **Peak tracked memory omitted from the final summary.** The pipeline
    populated `PipelineStats::peak_tracked_bytes`, and the governing design
    required it in the final summary, but `format_final_summary` did not read
    it. The final line now emits `peak_tracked_memory=<MiB> MiB`.
11. **Reserve-time double allocation was not admitted.** `std::string::reserve`
    and `std::vector::reserve` keep the old allocation alive while obtaining a
    new allocation, but the permit replaced old capacity with requested
    capacity before each call. The 10% process reserve cannot generally bound
    that omitted old buffer. Reader record-vector, converter output-string,
    and converter scratch-vector growth now temporarily admit old plus
    requested allocation and reconcile actual capacity after `reserve()`.
12. **CMake language-list drift.** The required source-only target shape
    enables both C and CXX, while the root file enabled only CXX. The root
    declaration now matches `LANGUAGES C CXX`.

## CLI and main trace

- `UsageRequested` maps help/version to stdout and exit 0.
- `std::invalid_argument` from CLI parsing maps to one stderr diagnostic and
  exit 2.
- Annotation, input, conversion, memory, thread, HTSlib, output, and publication
  exceptions map to one primary stderr diagnostic and exit 1.
- Normal orchestration creates no output transaction until annotation load and
  close plus conversion-input open succeed.
- The final summary is formatted after the pipeline but written only after
  checked input/output close and transaction commit. Quiet suppresses periodic
  progress and that final summary; it does not suppress warnings/errors. The
  final summary includes peak tracked pipeline memory.

## HTSlib and transaction ownership trace

- `InputSource` owns one `htsFile*`, reads complete lines with `hts_getline`,
  optionally configures `hts_set_threads`, exposes compression detection, and
  has checked normal close plus nonthrowing best-effort destruction.
- Plain output adopts the duplicated transaction descriptor through `hdopen`
  and uses checked repeated `hwrite`, `hflush`, and `hclose` operations.
- BGZF output wraps the adopted hFILE with `bgzf_hopen`, optionally configures
  `bgzf_mt`, and checks repeated `bgzf_write`, flush, and close operations.
- The transaction retains the original HANDLE/fd while the sink owns only a
  duplicate. Main closes the duplicate-backed sink before commit.
- Windows no-force publication is handle-based and non-replacing; forced
  publication verifies the retained identity then uses pathname-based
  `MoveFileExW`. Linux prefers `O_TMPFILE`, uses no-overwrite link publication,
  and falls back to an exclusively created, immediately identity-checked named
  temporary. Forced anonymous publication stages a retained-fd link before
  rename replacement.
- These publication properties are reasoned only for the documented normal,
  non-adversarial output-directory model. Platform and fault behavior are
  unverified.

## Memory, concurrency, order, and progress trace

- The 2 GiB default is a whole-job soft target. Ten percent is reserved for
  allocator, HTSlib, stacks, and untracked transients. The built annotation
  estimate is subtracted from the remaining tracked allowance before pipeline
  buffering.
- Work/result queue capacities and the new whole-pipeline admission limit are
  twice the conversion-worker count. Reader chunks target 512 records or
  8 MiB and can end earlier under memory pressure; workers claim them
  dynamically.
- One permit follows each sequence through input storage, converter scratch,
  result storage, reordering, writing, and release. Only the next required
  sequence may use the single exceptional overage, preventing a later result
  from blocking an earlier result that must finish.
- Before each pipeline-owned string/vector `reserve()`, the permit includes
  both the old allocation and requested replacement while they can coexist;
  after return it reconciles the implementation-selected capacity. The 10%
  reserve remains for allocator/HTSlib/stack costs, capacity beyond a request,
  and other untracked transients.
- The coordinator writes filtered leading headers synchronously before any
  pipeline thread starts. Sequence numbers are assigned only by the reader,
  workers convert records in chunk order, and only the ordered writer writes
  data-result chunks; it publishes only the next sequence and advances after a
  complete write.
- First-failure capture cancels the budget and both queues, wakes all relevant
  waits, joins every created worker and writer, stops the reporter, and only
  then rethrows the retained exception.
- Periodic progress alone uses stdout. The single overage warning and all
  primary diagnostics use stderr. Quiet and zero interval suppress periodic
  output; only quiet suppresses the post-publication final summary, which
  includes peak tracked pipeline MiB.

Concurrency, liveness, memory pressure, stream routing, and ordering remain
runtime-unverified.

## Static Python-oracle comparison

For conventional newline-terminated, tab-delimited VCF text in the documented
scope, source branches were mapped as follows:

- the same four header substrings are filtered and all other headers retain
  order with LF output;
- annotation chromosome/ID entries are last-record-wins, with checked positive
  numeric positions and exactly one comma-free annotation ID;
- a sole unknown ID mapping is passed through unchanged, including a valid
  eight-column site-only record;
- known IDs are resolved, deduplicated, and ordered by position;
- emitted fields substitute annotation POS/ID/REF/ALT, preserve QUAL/FILTER,
  and keep only `MA`/`UK` INFO values in first-key encounter order with the last
  duplicate value;
- FORMAT emits exact `GT` and optional exact `GQ`; sample order and GQ text are
  preserved;
- phasing is normalized to `/`, missing alleles remain `.`, mappings containing
  the emitted ID become `1`, and other mapped alleles become `0`; and
- input-record and sample ordering are preserved by chunk sequence and local
  iteration.

One statically known ordering exception is now explicit in the design, README,
handoff, and UNVERIFIED ledger. Python collects `(ID, position)` in a hash set
and sorts only by position, leaving equal-position ties hash-dependent. C++
uses ID as a deterministic secondary key. Equal-position multi-ID records are
therefore outside the Python byte-equivalence scope.

Malformed VCF handling is intentionally stricter and descriptive rather than
reproducing Python assertion, indexing, negative-index, and conversion
failures. That error-text/type difference is not a byte-output compatibility
claim.

## Documentation and verification boundary

README, `SOURCE_ONLY_HANDOFF.md`, and `UNVERIFIED.md` make no build, test,
runtime, compatibility, memory, platform, or performance success claim.
`UNVERIFIED.md` lists every deferred category required by the source-only plan,
including API/ABI, round trips, concurrency, memory, routing, transaction fault
paths, and scaling.

Source authored but not compiled or tested at the user's request.
