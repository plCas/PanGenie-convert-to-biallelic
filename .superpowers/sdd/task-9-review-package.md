# Task 9 Exact Whole-Source Static Review Package

## Review boundary

This package contains exact current text copies of the complete authored C++
source/header/CMake boundary, the Python oracle, the governing/current plans,
and the current Task 9 documentation needed for independent final static
review. Historical Task 1-8 briefs, reports, and snapshot packages are not
recursively embedded because they do not define the current source boundary.
This package cannot contain itself.

No Git base, diff, status, branch, commit, or worktree was used. No dependency,
configuration, compilation, link, executable, test, benchmark, or Python
process was used. Every runtime property remains unverified.

## Included files

- `CMakeLists.txt`
- `convert-to-biallelic.py`
- `README.md`
- `UNVERIFIED.md`
- `SOURCE_ONLY_HANDOFF.md`
- `.superpowers/sdd/progress.md`
- `.superpowers/sdd/task-9-brief.md`
- `.superpowers/sdd/task-9-report.md`
- `docs/superpowers/specs/2026-07-18-cpp-multithreaded-vcf-converter-design.md`
- `docs/superpowers/plans/2026-07-18-cpp-multithreaded-vcf-converter-source-only.md`
- `docs/superpowers/plans/2026-07-18-cpp-multithreaded-vcf-converter.md`
- `include/convert_to_biallelic/annotation_index.hpp`
- `include/convert_to_biallelic/bounded_queue.hpp`
- `include/convert_to_biallelic/cli.hpp`
- `include/convert_to_biallelic/converter.hpp`
- `include/convert_to_biallelic/memory_budget.hpp`
- `include/convert_to_biallelic/output_transaction.hpp`
- `include/convert_to_biallelic/pipeline.hpp`
- `include/convert_to_biallelic/progress.hpp`
- `include/convert_to_biallelic/types.hpp`
- `include/convert_to_biallelic/vcf_io.hpp`
- `src/annotation_index.cpp`
- `src/cli.cpp`
- `src/converter.cpp`
- `src/main.cpp`
- `src/memory_budget.cpp`
- `src/output_transaction.cpp`
- `src/pipeline.cpp`
- `src/progress.cpp`
- `src/vcf_io.cpp`

## `CMakeLists.txt`

````````text
cmake_minimum_required(VERSION 3.20)

project(convert_to_biallelic VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

find_package(Threads REQUIRED)
find_package(PkgConfig REQUIRED)
pkg_check_modules(HTSLIB REQUIRED IMPORTED_TARGET htslib>=1.17)

add_library(ctb_core STATIC
  src/annotation_index.cpp
  src/cli.cpp
  src/converter.cpp
  src/memory_budget.cpp
  src/output_transaction.cpp
  src/pipeline.cpp
  src/progress.cpp
  src/vcf_io.cpp
)

target_include_directories(ctb_core PUBLIC
  ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_link_libraries(ctb_core PUBLIC
  PkgConfig::HTSLIB
  Threads::Threads
)

add_executable(convert-to-biallelic src/main.cpp)
target_link_libraries(convert-to-biallelic PRIVATE ctb_core)
````````

## `convert-to-biallelic.py`

````````text
#!/usr/bin/env python

import sys
import argparse
from collections import defaultdict
import gzip

parser = argparse.ArgumentParser(prog='convert-to-biallelic.py', description='cat <multiallelic VCF> | python convert-to-biallelic.py <biallelic VCF>')
parser.add_argument('vcf', metavar='VCF', help='original VCF containing REF/ALT of each Variant ID.')
args = parser.parse_args()

# chromosome ->  ID -> [start, REF, ALT] per chromosome
chrom_to_variants = defaultdict(lambda: defaultdict(list))

# read the biallelic VCF containing REF/ALT for all variant IDs and store them
for line in gzip.open(args.vcf, 'rt'):
	if line.startswith('#'):
		continue
	fields = line.split()
	info_field = { i.split('=')[0] : i.split('=')[1] for i in fields[7].split(';') if "=" in i}
	assert 'ID' in info_field
	ids = info_field['ID'].split(',')
	assert len(ids) == 1
	chrom_to_variants[fields[0]][ids[0]] = [fields[1], fields[3], fields[4]]

for line in sys.stdin:
	if line.startswith('#'):
		# header line
		if any([i in line for i in ['INFO=<ID=AF', 'INFO=<ID=AK', 'FORMAT=<ID=GL', 'FORMAT=<ID=KC']]):
			# these fields will not be contained in biallelic VCF
			continue
		print(line[:-1])
		continue
	fields = line.split()
	assert len(fields) > 7
	# parse the INFO field
	info_field = { i.split('=')[0] : i.split('=')[1] for i in fields[7].split(';') if "=" in i}
	assert 'ID' in info_field
	# determine ID string belonging to each allele (keep empty string for REF, as it does not have an ID)
	allele_to_ids = [''] + info_field['ID'].split(',')
	info_ids = info_field['ID'].split(',')
	# allow bi-allelic records with unknown IDs (that are not in annotation VCF)
	if (len(info_ids) == 1) and any([x not in chrom_to_variants[fields[0]] for x in info_ids[0].split(':')]):
		# unknown ID, leave record as is
		print(line[:-1])
		continue
	# collect all variant IDs in this region
	ids = set([])
	for i in info_field['ID'].split(','):
		for j in i.split(':'):
			ids.add((j,int(chrom_to_variants[fields[0]][j][0])))
	# sort the ids by the starting coordinate (to ensure the VCF is sorted)
	ids = list(ids)
	ids.sort(key=lambda x : x[1])
	# create a single, biallelic VCF record for each ID
	for (var_id, coord) in ids:
		vcf_line = fields[:9]
		# set start coordinate
		vcf_line[1] = str(coord)
		# also add ID to ID column of the VCF
		vcf_line[2] = var_id
		# set REF
		vcf_line[3] = chrom_to_variants[fields[0]][var_id][1]
		# set ALT
		vcf_line[4] = chrom_to_variants[fields[0]][var_id][2]
		# set INFO
		vcf_line[7] = 'ID=' + var_id
		# also add other INFO fields (except ID which was replaced)
		for k,v in info_field.items():
			if k == 'ID':
				continue
			if k in ['MA', 'UK']:
				values = ';' + k + '=' + v
				vcf_line[7] = vcf_line[7] + values
		# keep only GT and GQ
		vcf_line[8] = 'GT'
		if 'GQ' in fields[8]:
			vcf_line[8] += ':GQ'
		# determine the genotype of each sample
		for sample_field in fields[9:]:
			# determine position of GT and GQ from FORMAT
			assert 'GT' in fields[8]
			format_field = fields[8].split(':')
			index_of_gt = format_field.index('GT')
			genotype = sample_field.split(':')
			biallelic_genotype = []
			for allele in genotype[index_of_gt].replace('|', '/').split('/'):
				if allele == '.':
					# missing allele
					biallelic_genotype.append('.')
				else:
					if var_id in allele_to_ids[int(allele)].split(':'):
						biallelic_genotype.append('1')
					else:
						biallelic_genotype.append('0')
			if 'GQ' in fields[8]:
				index_of_gq = format_field.index('GQ')
				vcf_line.append('/'.join(biallelic_genotype) + ':' + genotype[index_of_gq])
			else:
				vcf_line.append('/'.join(biallelic_genotype))
		print('\t'.join(vcf_line))
````````

## `README.md`

````````text
# convert_to_biallelic

`convert_to_biallelic` is an authored, source-only C++17/HTSlib converter for
transforming multiallelic VCF input into biallelic VCF output using an
annotation VCF.

## Intended usage

```text
convert-to-biallelic \
  --variants annotation.vcf.gz \
  --input multiallelic.vcf.gz \
  --output biallelic.vcf.gz \
  --threads 8 \
  --memory-limit 2G
```

The intended executable requires explicit input, annotation, and output paths.
`--progress-interval` accepts whole seconds and defaults to 5; zero disables
periodic reports but not the final summary. It writes periodic progress and a
final peak-tracked-memory summary to stdout and diagnostics to stderr. Output is
written through a duplicated descriptor for an exclusively created native
file identity retained by the transaction; main never reopens the temporary
pathname for sink I/O. The requested path is published only after the pipeline
and output close succeed. Existing output is rejected unless `--force` is
supplied, and no-force publication does not replace a destination created
concurrently. The final success summary is intended to appear only after
publication succeeds, and `--quiet` suppresses both periodic progress and that
summary.

The authored soft-memory accounting temporarily admits both the existing and
requested new allocations while a pipeline-owned string or vector grows with
`reserve()`, then reconciles the implementation-selected capacity. The
separate 10% process reserve remains for allocator overhead, HTSlib, thread
stacks, and other untracked transients. This behavior remains runtime-unverified.

For records that emit multiple distinct IDs at one annotation position, the
authored C++ source uses a deterministic position-then-ID order. The Python
oracle sorts a hash set only by position, so its tie order is hash-dependent;
those equal-position records are explicitly outside the byte-equivalence
scope. All other compatibility claims still require future differential
execution.

The Linux implementation is intentionally Linux-specific. It prefers an
unnamed `O_TMPFILE` and publishes that retained identity with `AT_EMPTY_PATH`
when permitted, or through the documented unprivileged
`/proc/self/fd/<fd>` plus `AT_SYMLINK_FOLLOW` route. If the filesystem or
kernel does not support `O_TMPFILE`, it keeps an exclusively created named
temporary entry and verifies its device/inode identity against the retained
descriptor immediately before link/rename publication.

The transaction guarantee assumes normal, non-adversarial operation. It covers
failures, temporary-name collisions, and concurrent creation of the final
destination, but the output directory must not be modified by an adversarial
process while a transaction is active. Windows forced `MoveFileExW` and Linux
named-temporary or anonymous-stage fallbacks are pathname-based and are not
hardened against same-directory substitution between the best-effort identity
check and publication. These routes remain source-only and must be verified on
target kernels, filesystems, procfs mounts, and Windows installations.

This source tree is authored only; its build, compatibility, and performance
properties remain unverified. See [UNVERIFIED.md](UNVERIFIED.md) for the
explicit verification boundary.
````````

## `UNVERIFIED.md`

````````text
# Unverified status

This source has not been compiled, linked, or executed. It has not been tested
against the Python implementation, memory-profiled, or benchmarked. It has not
been verified on Windows or Linux.

The CMake configuration and authored HTSlib integration are source descriptions
only. Buildability, compatibility, correctness, memory behavior, and
performance must be verified in a suitable environment before use.

The deferred verification work is explicit:

- compiler and linker success;
- HTSlib 1.17+ API and ABI compatibility;
- Windows and Linux configuration, build, and execution;
- Python byte equivalence within the documented compatibility scope;
- plain `.vcf` and BGZF `.vcf.gz` output round trips;
- multithreaded ordering, cancellation, liveness, and deadlock freedom;
- whole-job soft memory-limit and exceptional-overage behavior;
- progress routing to stdout, diagnostics routing to stderr, interval timing,
  and quiet behavior;
- transactional publication and cleanup under success, collision, race, and
  injected-failure paths; and
- throughput, compression behavior, scaling, and peak resident memory.

Equal-position multi-ID records are not deferred as a Python-equivalence
claim: the source intentionally orders them by position then ID, whereas the
Python oracle's position-only sort leaves ties dependent on hash-set iteration.

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
````````

## `SOURCE_ONLY_HANDOFF.md`

````````text
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
````````

## `.superpowers/sdd/progress.md`

````````text
# Source-only SDD progress

Plan: `docs/superpowers/plans/2026-07-18-cpp-multithreaded-vcf-converter-source-only.md`

Constraints: no installation, configuration, compilation, linking, execution,
tests, benchmarks, Python process, Git command, worktree, or commit. Reviews are
static and all runtime properties remain unverified.

Task 1: complete (source-only; Task 9 restored the planned C/C++ CMake language declaration; no commits)
Task 2: complete (source-only; Task 9 corrected progress-interval units; no commits)
Task 3: complete (source-only; Task 9 added checked input close; no commits; future C++20 u8string portability remains deferred)
Task 4: complete (source-only; Task 9 replaced quadratic per-record recount/re-estimation with incremental accounting; no commits; theoretical uint64 line-count boundary remains)
Task 5: complete (source-only; Task 9 restored eight-column unknown-ID passthrough and documented the equal-position ordering boundary; no commits)
Task 6: complete (source-only, static review clean; no commits; queue requires nothrow movable/destructible elements)
Task 7: complete (source-only; Task 9 added a whole-pipeline in-flight cap, output-side overage warning, reserve-time double-allocation admission, and final peak-memory reporting; no commits)
Task 8: complete (source-only; Task 9 corrected Windows rename-buffer alignment/object lifetime; retained-identity transaction remains runtime-unverified; no commits)
Task 9: complete (whole-source static audit and source-only handoff; final findings incorporated and exact review package regenerated; no commits; every runtime property remains unverified)
````````

## `.superpowers/sdd/task-9-brief.md`

````````text
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

````````

## `.superpowers/sdd/task-9-report.md`

````````text
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
````````

## `docs/superpowers/specs/2026-07-18-cpp-multithreaded-vcf-converter-design.md`

````````text
# C++ Multithreaded VCF Converter Design

Date: 2026-07-18

## Objective

Replace `convert-to-biallelic.py` with a cross-platform C++17 command-line program that preserves the Python converter's uncompressed VCF output, processes records in parallel, uses HTSlib for VCF text and BGZF I/O, reports progress to stdout, and targets at most 2 GiB of process memory under ordinary inputs.

The first release supports `.vcf` and `.vcf.gz` inputs and outputs on Windows and Linux. BCF, indexing, region queries, distributed processing, and a Python API are outside this scope.

## Success Criteria

1. For every supported valid fixture within the compatibility scope below, the C++ program's uncompressed VCF bytes equal the Python program's output bytes.
2. Runs with different thread counts produce identical uncompressed VCF output and record ordering.
3. `.vcf.gz` output is BGZF-compressed and decompresses to the same bytes as `.vcf` output.
4. The program opens each input and output stream once and does not pre-count VCF records.
5. The default memory target is 2 GiB for the entire process, not per worker.
6. Progress is printed to stdout without contaminating the VCF, which is always written to an explicit output file.
7. The project builds and its tests run on Windows and Linux.

## Command-Line Interface

The executable is named `convert-to-biallelic`.

```text
convert-to-biallelic \
  --variants annotation.vcf.gz \
  --input multiallelic.vcf.gz \
  --output result.vcf.gz \
  --threads 16 \
  --memory-limit 2G
```

Required options:

- `--variants PATH`: annotation VCF containing the position, REF, ALT, and one `INFO/ID` value for each known variant.
- `--input PATH`: multiallelic VCF to convert.
- `--output PATH`: destination ending in `.vcf` or `.vcf.gz`.

Optional options:

- `--threads N`: CPU-intensive thread budget. The default is the number of logical CPUs reported by the standard library, with a minimum of one.
- `--memory-limit SIZE`: process-wide soft memory target. The default is `2G`; suffixes `K`, `M`, and `G` use powers of 1024.
- `--progress-interval SECONDS`: progress-report interval. The default is 5 seconds. Zero disables periodic reports but not the final summary.
- `--quiet`: disable periodic progress and the final summary.
- `--force`: allow replacement of an existing output file. Without it, an existing destination is an error.
- `--output-format vcf|vcf.gz`: override extension-based output selection. Without this option, `.vcf` selects plain VCF and `.vcf.gz` selects BGZF-compressed VCF; other extensions are rejected.
- `--help` and `--version`.

Standard input and standard output are not VCF data channels in the first release. This keeps stdout exclusively available for requested progress output. Diagnostics go to stderr.

## Compatibility Contract

The converter preserves the behavior of `convert-to-biallelic.py`:

- Header lines containing `INFO=<ID=AF`, `INFO=<ID=AK`, `FORMAT=<ID=GL`, or `FORMAT=<ID=KC` are removed.
- Other header lines retain their text and order.
- The annotation's `INFO/ID` value must contain exactly one comma-delimited ID.
- The last annotation entry wins if the same chromosome and ID occur more than once, matching Python dictionary assignment.
- A biallelic record with a single unknown ID is passed through unchanged.
- Known IDs referenced by an input record are deduplicated and sorted by annotation position, then ID.
- Each known ID produces one record with annotation position, ID, REF, ALT, and `INFO/ID` substituted.
- Only `MA` and `UK` are copied from the remaining INFO entries.
- FORMAT output contains `GT` and, when present in the input FORMAT, `GQ`.
- Input phasing separators are normalized to `/`, as in the Python implementation.
- Missing alleles remain `.`; alleles containing the emitted ID become `1`; other alleles become `0`.
- The ordering of input records, emitted IDs within each input record, and samples is deterministic.

The compatibility target is the uncompressed VCF byte stream. BGZF block layout, compression metadata, and compressed bytes are not required to match another compressor.

The Python oracle first stores emitted IDs in a hash set and then sorts only by annotation position. Its tie order is therefore hash-iteration-dependent when one input record emits distinct IDs at the same annotation position. The C++ source deliberately uses ID as a deterministic secondary key. Such equal-position multi-ID records are outside the Python byte-equivalence scope; their C++ position-then-ID order is an intentional deterministic extension. This is a statically known boundary, not a property awaiting runtime verification.

LF and CRLF input terminators are accepted through HTSlib and output is normalized to LF, matching Python text-mode behavior. Differential equivalence is defined for conventional newline-terminated VCF text. An unterminated final line is outside the byte-equivalence contract because the Python script's `line[:-1]` behavior can truncate such a line.

The C++ implementation replaces Python `assert` failures with descriptive validation errors that identify the file and record number. A validation failure makes the command fail rather than silently changing the record.

## HTSlib Strategy

HTSlib provides transparent plain/BGZF input and BGZF output. Conversion remains text-oriented rather than decoding and reserializing every record through `bcf1_t`, because text-oriented processing gives direct control over the Python-compatible output representation.

An `InputSource` abstraction uses `hts_open`, `hts_getline`, `hts_set_threads` when allocated I/O workers, and `hts_close`. It yields complete text lines without trailing newline characters. An `OutputSink` abstraction accepts complete output buffers. For transactional output, it adopts a duplicated native descriptor rather than reopening a temporary pathname. Its plain implementation uses HTSlib hFILE operations (`hdopen`, `hwrite`, and `hclose`); its compressed implementation wraps that hFILE with `bgzf_hopen`, then uses `bgzf_mt`, `bgzf_write`, `bgzf_flush`, and `bgzf_close`. Both implementations check write, flush, and close errors.

HTSlib 1.17 or newer is required. CI records and tests a specific HTSlib release rather than silently accepting an older library.

HTSlib structured VCF APIs may be used for validation where they do not alter the text, but structured reserialization is not part of the output path. BCF support is deliberately excluded.

## Components

### CLI and configuration

Parses arguments, validates paths and numeric limits, chooses the output format, calculates the thread allocation, and produces an immutable configuration object.

### Annotation index

Streams the annotation file once and builds a read-only lookup:

```text
chromosome -> variant ID -> {position, REF, ALT}
```

Position is stored as a checked 64-bit integer for sorting and as normalized decimal text for output. Strings are owned by the index. After construction, workers only perform concurrent reads, so the index needs no locks.

The loader estimates index memory from string capacities, table bucket counts, and element overhead. If the index and safety reserve consume the entire 2 GiB target, the program fails before opening the output.

### Record converter

A pure conversion unit accepts one input record and the immutable annotation index, then returns zero or more complete VCF lines. It has no file handles and no shared mutable state. This boundary allows direct unit and differential testing.

Parsing improvements over the Python implementation are allowed only when output behavior remains identical. In particular, FORMAT is parsed once per record; GT and GQ indices are reused for all samples; INFO and allele-to-ID relationships are also parsed once per record.

### Work and result chunks

A work chunk contains a monotonically increasing sequence number and a vector of source records. Chunking is scheduling only; it never merges biological records.

A result chunk contains the same sequence number, an output byte buffer, and input/output record counters. Records inside a chunk are converted sequentially, so their local ordering is stable.

The reader starts with a target of 512 records or 8 MiB of input text, whichever comes first. The memory controller may reduce these limits. The final partial chunk is valid. A single oversized VCF record forms its own chunk.

### Ordered pipeline

The pipeline has one reader/coordinator, conversion workers, and one ordered writer:

```text
HTSlib reader -> bounded work queue -> conversion workers
              -> bounded result queue -> reorder buffer -> HTSlib writer
```

The reader assigns sequence numbers and blocks when the work queue is full. Workers claim chunks dynamically, which avoids the load imbalance caused by assigning one fixed file partition to each CPU. The writer stores early results until the next expected sequence arrives, then writes consecutive ready chunks in order.

No line-counting pass is performed. The input and output are each opened once.

### Progress reporter

Workers and the writer update atomic counters. A single reporter samples them at the configured interval and writes one complete line at a time to stdout. It reports elapsed time, input records converted, output records written, recent throughput, and tracked buffered memory. It does not claim an exact percentage because the program does not pre-count records.

Example:

```text
[00:00:30] input=4,821,120 output=6,903,445 rate=160,704 records/s memory=742 MiB
Finished: input=13,442,817 output=18,791,203 elapsed=00:01:08 average=197,688 records/s peak_tracked_memory=812 MiB
```

Annotation-loading progress may report records and estimated memory before conversion starts.

## Thread Allocation

`--threads N` is the approximate budget for CPU-intensive conversion and compressed-I/O work. Reader, writer, and progress coordination threads exist in addition but usually block on queues or timers.

- With `N=1`, conversion and HTSlib I/O are synchronous.
- With `N>1` and `.vcf.gz` output, one unit of the budget is assigned to HTSlib BGZF compression.
- With `N>=4` and compressed input, one additional unit is assigned to HTSlib decompression.
- All remaining units become conversion workers, with at least one conversion worker.
- Plain input or output returns the corresponding I/O unit to conversion workers.

`hts_set_threads` is used for compressed input and `bgzf_mt` for compressed output with the allocations above. Benchmark results may justify a later explicit `--io-threads` tuning option, but that option is not in the first release.

## Memory Management

The 2 GiB default is a soft target for the whole process. It includes the annotation index estimate, queued source text, queued result text, reorder buffers, and tracked worker scratch buffers. It is not multiplied by the number of workers.

The controller reserves 10% of the configured limit for allocator overhead, thread stacks, HTSlib, and untracked temporary allocations. The remaining tracked budget is divided between the immutable index and pipeline buffers.

At most twice the number of conversion workers may be in flight, and fewer are allowed when their reserved bytes reach the budget. Enqueuing a chunk acquires memory-budget capacity; writing and destroying it releases capacity. This backpressure pauses reading rather than allowing unbounded buffering.

Before a pipeline-owned string or vector calls `reserve()`, its permit temporarily admits both the still-live old allocation and the requested new allocation. After the call it reconciles the actual implementation-selected capacity. Capacity beyond the request, allocator metadata, HTSlib, stacks, and other untracked transients remain within the purpose of the separate 10% process reserve.

Output expansion is data-dependent. Under pressure, the reader reduces future chunks as far as one record. If a single input record or its complete converted output exceeds the remaining tracked budget, it is permitted as one exceptional chunk and a warning is printed to stderr. Thus the target cannot be an absolute RSS ceiling for arbitrary records. The final summary reports peak tracked pipeline memory.

## Output Safety and Errors

The program validates all input paths and the destination before conversion. The output transaction exclusively creates and retains a native file identity in the destination directory, and `OutputSink` writes through a duplicate of that retained descriptor or HANDLE. The final destination is published only after the pipeline and output close succeed. This avoids publishing a partial final VCF after a worker, HTSlib, disk, or validation failure. `--force` controls replacement of an existing destination; no-force publication must not overwrite a destination created concurrently.

These transactional guarantees cover normal, non-adversarial operation, including failures, temporary-name collisions, and concurrent creation of the final destination. The output directory must not be modified by an adversarial process while a transaction is active. Windows forced replacement uses pathname-based `MoveFileExW`, and Linux named-temporary and anonymous-staging fallbacks use pathname-based link/rename operations. Those branches retain best-effort immediate identity checks where available, but they are not hardened against same-directory pathname substitution between the check and publication.

Any thread may publish the first failure through shared cancellation state. Queues wake, producers and consumers stop, all threads join, HTSlib handles close, and transaction-owned temporary state is removed best-effort. The program emits one primary diagnostic to stderr and exits nonzero.

Errors include:

- unsupported extension or invalid option;
- unreadable input or unwritable destination;
- malformed VCF fields or missing `INFO/ID`;
- non-numeric or invalid position;
- unknown composite variant ID that the Python program would fail to resolve;
- invalid genotype allele index;
- HTSlib read, write, flush, compression, or close failure;
- configured memory too small for the annotation index and safety reserve.

## Testing

Tests use CTest and a small Python harness. The existing Python script remains the compatibility oracle during development.

### Unit tests

- CLI size and extension parsing.
- INFO and FORMAT parsing.
- GT/GQ conversion, phasing normalization, and missing alleles.
- ID deduplication and positional ordering.
- unknown single-ID passthrough.
- annotation duplicate handling.
- chunk-size and memory-budget decisions.
- ordered writer behavior under deliberately reversed worker completion.

### Differential tests

Generate the Python output for fixtures, run the C++ tool, decompress when necessary, and compare bytes. Fixtures cover headers, MA/UK, unknown IDs, multiple ALT alleles, composite allele IDs, missing alleles, GT with and without GQ, multiple samples, duplicate IDs, LF and CRLF input, and one input record expanding to several outputs.

Every fixture runs with one and multiple threads. Repeated multithreaded runs verify deterministic output.

### Integration tests

- All four supported input/output combinations: VCF-to-VCF, VCF-to-VCF.GZ, VCF.GZ-to-VCF, and VCF.GZ-to-VCF.GZ.
- Progress captured from stdout and diagnostics captured from stderr.
- Existing-output behavior with and without `--force`.
- Cancellation and temporary-file cleanup after injected conversion and write failures.
- Small memory limits that force queue backpressure and one-record chunks.

### Performance tests

A generated, representative multi-sample VCF is processed with 1, 2, 4, 8, and 16 requested threads. Reports include elapsed time, input records per second, output bytes per second, peak tracked memory, and scaling efficiency. Chunk targets of 128, 512, and 1,024 records plus byte limits are benchmarked before retaining the default. Correctness comparisons are mandatory; a speedup threshold is reported rather than hard-coded until representative real data is available.

### Cross-platform checks

CI builds and runs tests on current Windows and Linux runners. Windows uses the MSYS2 UCRT64 MinGW toolchain and `mingw-w64-ucrt-x86_64-htslib`; the current vcpkg HTSlib port cannot be used because it excludes Windows. Linux discovers HTSlib 1.17 or newer through pkg-config. The build records the exact compilers and HTSlib release exercised by CI.

## Proposed Project Layout

```text
CMakeLists.txt
.github/workflows/ci.yml
include/convert_to_biallelic/
  annotation_index.hpp
  bounded_queue.hpp
  cli.hpp
  converter.hpp
  memory_budget.hpp
  pipeline.hpp
  progress.hpp
  vcf_io.hpp
src/
  annotation_index.cpp
  cli.cpp
  converter.cpp
  main.cpp
  memory_budget.cpp
  pipeline.cpp
  progress.cpp
  vcf_io.cpp
tests/
  fixtures/
  differential_test.py
  unit/
benchmarks/
  generate_fixture.py
docs/
  build-windows.md
  build-linux.md
```

## Delivery Sequence

1. Establish CMake, HTSlib discovery, a minimal executable, and cross-platform smoke tests.
2. Capture the Python behavior in differential fixtures before implementing conversion.
3. Implement HTSlib text I/O and the annotation index.
4. Implement and validate the single-record converter.
5. Add single-thread streaming output and prove differential equivalence.
6. Add bounded queues, ordered multithreading, cancellation, and the memory controller.
7. Add `.vcf.gz` output and HTSlib I/O thread allocation.
8. Add stdout progress reporting and final statistics.
9. Complete failure-path, cross-platform, determinism, memory, and performance verification.

## Design References

- HTSlib documentation: https://www.htslib.org/doc/
- HTSlib official repository: https://github.com/samtools/htslib
- BGZF behavior and threading: https://www.htslib.org/doc/bgzip.html
- MSYS2 Windows HTSlib package: https://packages.msys2.org/base/mingw-w64-htslib
- vcpkg HTSlib manifest showing Windows exclusion: https://github.com/microsoft/vcpkg/blob/master/ports/htslib/vcpkg.json
````````

## `docs/superpowers/plans/2026-07-18-cpp-multithreaded-vcf-converter-source-only.md`

````````text
# C++ Multithreaded VCF Converter Source-Only Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Author an uncompiled C++17/HTSlib replacement for `convert-to-biallelic.py` that supports ordered multithreading, `.vcf` and `.vcf.gz` output, a process-wide 2 GiB memory target, and progress on stdout.

**Architecture:** Load the annotation VCF once into an immutable chromosome/ID index, then stream the multiallelic input through a bounded reader, conversion-worker pool, and ordered writer. Keep conversion text-oriented for Python-compatible formatting while HTSlib handles line input and plain/BGZF output.

**Tech Stack:** C++17 source, CMake project metadata, HTSlib 1.17+ APIs, standard C++ threads and synchronization.

## Global Constraints

- This is a source-authoring-only plan because the user declined toolchain installation.
- Do not install Git, MSYS2, HTSlib, CMake, Ninja, GCC, pkg-config, Python packages, or any other dependency.
- Do not compile, link, execute, benchmark, or test the C++ program.
- Do not create a Git worktree or make commits because Git is unavailable.
- Do not claim that the source builds, passes tests, improves performance, or matches Python output until verified later in a suitable environment.
- Preserve the approved design in `docs/superpowers/specs/2026-07-18-cpp-multithreaded-vcf-converter-design.md` except where this plan explicitly removes testing and build execution.
- Keep `convert-to-biallelic.py` unchanged.
- Support explicit `--input`, `--variants`, and `--output` file paths; do not use stdin or stdout for VCF data.
- Send progress to stdout and errors/warnings to stderr.
- Infer plain VCF from `.vcf` and BGZF-compressed VCF from `.vcf.gz`.
- Treat 2 GiB as a soft process-wide target, not a per-thread allocation.
- Preserve record ordering with sequence-numbered chunks and an ordered writer.
- Use HTSlib text-line input rather than `bcf1_t` reserialization.
- Mark every delivered build, compatibility, memory, and performance property as unverified.

## Static Review Rules

Static review is allowed because it does not require installing or executing a development toolchain. Reviewers may use `Get-Content`, `rg`, and file listings to inspect authored text. They must not run CMake, a compiler, the Python oracle, the generated executable, or dependency commands.

For every task, the reviewer checks:

1. Every declared function has exactly one declaration and one source definition unless it is a template.
2. Namespaces, types, includes, and signatures match between files.
3. The task does not alter `convert-to-biallelic.py`.
4. No test, build, benchmark, installation, worktree, or Git command is executed.
5. Any uncertainty is recorded in `UNVERIFIED.md` rather than silently resolved by claiming success.

## File Map

- `CMakeLists.txt`: unexecuted future build description.
- `include/convert_to_biallelic/types.hpp`: shared output, thread, chunk, and statistics types.
- `include/convert_to_biallelic/cli.hpp`, `src/cli.cpp`: CLI and deterministic thread allocation.
- `include/convert_to_biallelic/vcf_io.hpp`, `src/vcf_io.cpp`: HTSlib/hFILE/BGZF wrappers.
- `include/convert_to_biallelic/annotation_index.hpp`, `src/annotation_index.cpp`: immutable lookup table.
- `include/convert_to_biallelic/converter.hpp`, `src/converter.cpp`: Python-compatible textual conversion logic.
- `include/convert_to_biallelic/memory_budget.hpp`, `src/memory_budget.cpp`: tracked-byte permits and one-overage escape.
- `include/convert_to_biallelic/bounded_queue.hpp`: cancellation-aware queue template.
- `include/convert_to_biallelic/progress.hpp`, `src/progress.cpp`: atomic counters and stdout reporting.
- `include/convert_to_biallelic/pipeline.hpp`, `src/pipeline.cpp`: reader/workers/ordered writer.
- `include/convert_to_biallelic/output_transaction.hpp`, `src/output_transaction.cpp`: retained native output ownership and platform publication.
- `src/main.cpp`: top-level orchestration and exit codes.
- `README.md`: intended usage and explicit unverified status.
- `UNVERIFIED.md`: compilation, correctness, performance, and platform checks deferred by request.

---

### Task 1: Create the Source Tree and Shared Types

**Files:**
- Create: `CMakeLists.txt`
- Create: `include/convert_to_biallelic/types.hpp`
- Create: `README.md`
- Create: `UNVERIFIED.md`

**Interfaces:**
- Produces: `OutputFormat`, `ThreadAllocation`, `PipelineStats`, `RawWorkChunk`, and `RawResultChunk` names used by later tasks.

- [ ] **Step 1: Create directories and an unexecuted CMake description**

Create `include/convert_to_biallelic/` and `src/`. Author `CMakeLists.txt` with C++17, `Threads`, `PkgConfig`, `htslib>=1.17`, a `ctb_core` static library containing every planned `.cpp`, and a `convert-to-biallelic` executable containing `src/main.cpp`. Do not configure or build it.

Required target shape:

```cmake
cmake_minimum_required(VERSION 3.20)
project(convert_to_biallelic VERSION 0.1.0 LANGUAGES C CXX)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
find_package(Threads REQUIRED)
find_package(PkgConfig REQUIRED)
pkg_check_modules(HTSLIB REQUIRED IMPORTED_TARGET htslib>=1.17)
add_library(ctb_core STATIC
  src/annotation_index.cpp src/cli.cpp src/converter.cpp
  src/memory_budget.cpp src/output_transaction.cpp src/pipeline.cpp
  src/progress.cpp src/vcf_io.cpp)
target_include_directories(ctb_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(ctb_core PUBLIC PkgConfig::HTSLIB Threads::Threads)
add_executable(convert-to-biallelic src/main.cpp)
target_link_libraries(convert-to-biallelic PRIVATE ctb_core)
```

- [ ] **Step 2: Define shared types**

```cpp
// include/convert_to_biallelic/types.hpp
#pragma once
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace ctb {
enum class OutputFormat { vcf, vcf_gz };

struct ThreadAllocation {
    std::size_t conversion_workers = 1;
    std::size_t input_io_workers = 0;
    std::size_t output_io_workers = 0;
};

struct PipelineStats {
    std::uint64_t input_records = 0;
    std::uint64_t output_records = 0;
    std::uint64_t output_bytes = 0;
    std::uint64_t peak_tracked_bytes = 0;
};

struct RawWorkChunk {
    std::uint64_t sequence = 0;
    std::uint64_t first_line_number = 0;
    std::vector<std::string> records;
};

struct RawResultChunk {
    std::uint64_t sequence = 0;
    std::string bytes;
    std::uint64_t input_records = 0;
    std::uint64_t output_records = 0;
};
}
```

- [ ] **Step 3: Document the unverified boundary**

`README.md` must show intended CLI usage and link to `UNVERIFIED.md`. `UNVERIFIED.md` must state that the source has not been compiled, linked, executed, tested against Python, memory-profiled, benchmarked, or verified on Windows/Linux.

- [ ] **Step 4: Perform static file review only**

Use file inspection to confirm the expected paths exist and the CMake source list matches the file map. Do not run CMake.

---

### Task 2: Author CLI Parsing and Thread Allocation

**Files:**
- Create: `include/convert_to_biallelic/cli.hpp`
- Create: `src/cli.cpp`

**Interfaces:**
- Consumes: `argc`, `argv`, filename extensions, and logical CPU count.
- Produces: `Config parse_cli(int, char**)`, `ThreadAllocation allocate_threads(...)`, and `usage_text()`.

- [ ] **Step 1: Declare configuration**

```cpp
// include/convert_to_biallelic/cli.hpp
#pragma once
#include "types.hpp"
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <stdexcept>

namespace ctb {
struct Config {
    std::filesystem::path variants;
    std::filesystem::path input;
    std::filesystem::path output;
    std::size_t threads = 1;
    std::uint64_t memory_limit_bytes = 2ULL * 1024ULL * 1024ULL * 1024ULL;
    std::chrono::milliseconds progress_interval{5000};
    bool quiet = false;
    bool force = false;
    OutputFormat output_format = OutputFormat::vcf;
};

class UsageRequested final : public std::exception {
public:
    explicit UsageRequested(bool version) : version_(version) {}
    bool version() const noexcept { return version_; }
    const char* what() const noexcept override { return "usage requested"; }
private:
    bool version_;
};

Config parse_cli(int argc, char** argv);
ThreadAllocation allocate_threads(const Config&, bool compressed_input,
                                  bool compressed_output);
const char* usage_text();
}
```

- [ ] **Step 2: Implement argument parsing**

Implement required options `--variants`, `--input`, and `--output`; optional `--threads`, `--memory-limit`, `--progress-interval`, `--quiet`, `--force`, `--output-format`, `--help`, and `--version`. Reject missing/duplicate/unknown options, zero threads, memory below 64 MiB, negative progress intervals, and unsupported output extensions. Infer `.vcf` or `.vcf.gz` case-insensitively unless overridden.

- [ ] **Step 3: Implement deterministic thread allocation**

Use one output I/O worker when output is compressed and `threads > 1`; use one input I/O worker when input is compressed and `threads >= 4`; assign the remainder to conversion with a minimum of one. Treat `--threads` as an approximate CPU-intensive budget because reader, writer, and progress coordination threads also exist.

- [ ] **Step 4: Static-review CLI completeness**

Compare every option named in `usage_text()` with the parser branches. Record parser behavior as unverified; do not execute `--help` or `--version`.

---

### Task 3: Author HTSlib Text and BGZF I/O Wrappers

**Files:**
- Create: `include/convert_to_biallelic/vcf_io.hpp`
- Create: `src/vcf_io.cpp`

**Interfaces:**
- Consumes: paths, `OutputFormat`, and I/O thread counts.
- Produces: RAII `InputSource` and `OutputSink`.

- [ ] **Step 1: Declare pimpl wrappers**

```cpp
namespace ctb {
class InputSource {
public:
    InputSource(const std::filesystem::path&, std::size_t io_workers);
    ~InputSource();
    InputSource(InputSource&&) noexcept;
    InputSource& operator=(InputSource&&) noexcept;
    bool getline(std::string& line);
    bool compressed() const noexcept;
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

class OutputSink {
public:
    OutputSink(const std::filesystem::path&, OutputFormat,
               std::size_t io_workers);
    OutputSink(int owned_fd, const std::filesystem::path& display_path,
               OutputFormat, std::size_t io_workers);
    ~OutputSink();
    OutputSink(OutputSink&&) noexcept;
    OutputSink& operator=(OutputSink&&) noexcept;
    void write(std::string_view bytes);
    void flush();
    void close();
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
}
```

- [ ] **Step 2: Implement input using HTSlib line APIs**

Use `hts_open(path, "r")`, inspect `hts_get_format`, optionally call `hts_set_threads`, read through `hts_getline(..., KS_SEP_LINE, ...)`, treat `-1` as EOF and values below `-1` as error, copy the `kstring_t` content into `std::string`, and check `hts_close`.

- [ ] **Step 3: Implement explicit-format output**

For standalone path output, retain the path constructor. For transactional output, adopt an owned descriptor with `hdopen` so the sink never reopens the temporary pathname. For `OutputFormat::vcf`, use repeated `hwrite` until complete, `hflush`, and `hclose`. For `OutputFormat::vcf_gz`, wrap the adopted hFILE with `bgzf_hopen`, then use optional `bgzf_mt`, repeated `bgzf_write`, `bgzf_flush`, and `bgzf_close`. Use the explicit format rather than temporary filename extension.

- [ ] **Step 4: Static-review ownership and failure paths**

Confirm every handle is initialized once, explicitly closable, and released by a nonthrowing destructor. Mark exact HTSlib signature compatibility unverified.

---

### Task 4: Author the Annotation Index

**Files:**
- Create: `include/convert_to_biallelic/annotation_index.hpp`
- Create: `src/annotation_index.cpp`

**Interfaces:**
- Consumes: annotation `InputSource` and process memory limit.
- Produces: immutable lookup `chromosome -> ID -> VariantDefinition`.

- [ ] **Step 1: Declare lookup types**

```cpp
namespace ctb {
struct VariantDefinition {
    std::int64_t position = 0;
    std::string ref;
    std::string alt;
};

class AnnotationIndex {
public:
    const VariantDefinition* find(std::string_view chromosome,
                                  std::string_view id) const noexcept;
    std::uint64_t estimated_bytes() const noexcept;
    std::uint64_t variant_count() const noexcept;
private:
    using ById = std::unordered_map<std::string, VariantDefinition>;
    std::unordered_map<std::string, ById> variants_;
    std::uint64_t estimated_bytes_ = 0;
    std::uint64_t variant_count_ = 0;
    friend AnnotationIndex load_annotation(InputSource&, std::uint64_t);
};

AnnotationIndex load_annotation(InputSource&, std::uint64_t memory_limit);
}
```

- [ ] **Step 2: Implement streaming annotation parsing**

Skip headers; split data by tabs; require at least eight fields; parse positive POS with `std::from_chars`; find exact `ID=` in INFO; reject missing or comma-separated IDs; store POS, REF, and ALT; and overwrite an existing chromosome/ID so the last definition wins.

- [ ] **Step 3: Implement conservative memory estimation**

Include string capacities, hash-node payloads, and bucket arrays. Reserve 10% of the configured process target for allocator/HTSlib/thread overhead. Fail before output creation if the annotation estimate consumes the tracked allowance.

- [ ] **Step 4: Static-review index immutability**

Confirm no public mutating method exists after `load_annotation` returns and all worker access is through `const AnnotationIndex&`.

---

### Task 5: Author Python-Compatible Text Conversion

**Files:**
- Create: `include/convert_to_biallelic/converter.hpp`
- Create: `src/converter.cpp`

**Interfaces:**
- Consumes: one header/data line, `AnnotationIndex`, and physical source line number.
- Produces: optional header text or one-to-many newline-terminated records.

- [ ] **Step 1: Declare conversion API**

```cpp
namespace ctb {
struct ConversionResult {
    std::string bytes;
    std::uint64_t output_records = 0;
};

std::optional<std::string> convert_header(std::string_view line);
ConversionResult convert_record(std::string_view line,
                                const AnnotationIndex& annotation,
                                std::uint64_t line_number);
}
```

- [ ] **Step 2: Implement header filtering**

Return no value for header lines containing `INFO=<ID=AF`, `INFO=<ID=AK`, `FORMAT=<ID=GL`, or `FORMAT=<ID=KC`. Return all other headers plus LF.

- [ ] **Step 3: Implement record parsing once per record**

Split tab fields; require at least nine fields; parse INFO once; require `ID`; create `allele_to_ids` as REF empty entry plus comma-separated allele IDs; parse FORMAT once; locate GT and optional GQ once; validate every sample field and allele index.

- [ ] **Step 4: Implement one-to-many emission**

For a single unknown ID, return the original line plus LF. Otherwise expand colon-separated component IDs, resolve each annotation, deduplicate by ID, sort by POS then ID, substitute POS/ID/REF/ALT, replace INFO with `ID=<id>` plus encountered MA/UK entries, retain only GT and optional GQ, normalize `|` to `/`, map emitted-ID alleles to `1`, other alleles to `0`, and preserve `.`.

- [ ] **Step 5: Perform line-by-line static comparison with Python**

Read `convert-to-biallelic.py` lines 26-101 and map each branch to a named C++ block in review notes. Record byte equivalence as unverified because neither implementation is executed.

---

### Task 6: Author Memory Budget and Bounded Queue Primitives

**Files:**
- Create: `include/convert_to_biallelic/memory_budget.hpp`
- Create: `src/memory_budget.cpp`
- Create: `include/convert_to_biallelic/bounded_queue.hpp`

**Interfaces:**
- Consumes: tracked-byte requests and item-capacity limits.
- Produces: movable permits, cancellation, peak accounting, and blocking FIFO queues.

- [ ] **Step 1: Implement movable memory permits**

`MemoryPermit` owns a byte reservation and releases it exactly once. `MemoryBudget::acquire` waits under a condition variable, `MemoryPermit::resize` adjusts before buffer growth, `cancel` wakes waiters, and `peak_bytes` reports the maximum tracked count.

- [ ] **Step 2: Implement one-overage escape**

Permit at most one worker to exceed the soft limit while growing a result. Track `overage_active_`; all other overage requests wait. Clear the flag when that permit shrinks below the limit or is destroyed so a worker can always finish and unblock the ordered writer.

- [ ] **Step 3: Implement the queue template**

Use `std::deque<T>`, one mutex, not-empty/not-full condition variables, fixed item capacity, `push`, `pop`, `close`, and `cancel`. Closing drains existing items; cancellation clears items and wakes all producers/consumers.

- [ ] **Step 4: Static-review lock ordering**

Document the only permitted nesting order as queue lock then no memory lock, or memory lock then no queue lock. Require callers to acquire/resize permits outside queue critical sections to avoid lock inversion. Record runtime deadlock behavior as unverified.

---

### Task 7: Author Progress Reporting and the Ordered Pipeline

**Files:**
- Create: `include/convert_to_biallelic/progress.hpp`
- Create: `src/progress.cpp`
- Create: `include/convert_to_biallelic/pipeline.hpp`
- Create: `src/pipeline.cpp`

**Interfaces:**
- Consumes: input/output streams, annotation, thread allocation, memory limit, progress interval.
- Produces: deterministic `PipelineStats run_pipeline(...)` and stdout progress.

- [ ] **Step 1: Implement progress counters and reporter**

Use atomic input/output/memory counters and one reporter thread. Every interval, emit one flushed stdout line containing elapsed time, input records, written output records, recent rate, and tracked MiB. The final stdout summary includes peak tracked pipeline MiB. `--quiet` produces no periodic or final output. Errors never use stdout.

- [ ] **Step 2: Define pipeline options**

```cpp
namespace ctb {
struct WorkChunk {
    RawWorkChunk data;
    MemoryPermit permit;
};

struct ResultChunk {
    RawResultChunk data;
    MemoryPermit permit;
};

struct PipelineOptions {
    ThreadAllocation threads;
    std::uint64_t memory_limit_bytes = 2ULL * 1024ULL * 1024ULL * 1024ULL;
    std::size_t target_records_per_chunk = 512;
    std::uint64_t target_bytes_per_chunk = 8ULL * 1024ULL * 1024ULL;
    std::chrono::milliseconds progress_interval{5000};
    bool quiet = false;
};

PipelineStats run_pipeline(InputSource&, OutputSink&, const AnnotationIndex&,
                           const PipelineOptions&, std::ostream& progress,
                           std::ostream& diagnostics);
}
```

- [ ] **Step 3: Implement reader/chunk creation**

Write filtered leading headers before starting data results. Assign increasing chunk sequence numbers. End a chunk at 512 records or 8 MiB, whichever comes first; a single oversized record becomes one chunk. Limit in-flight chunks to twice the conversion-worker count and reduce through memory backpressure.

- [ ] **Step 4: Implement workers and ordered writer**

Workers pop dynamically, convert records sequentially, resize their permits before output-buffer growth, release source storage, and push one result per sequence. A pipeline-owned string or vector `reserve()` temporarily admits both its old allocation and the requested new allocation, then reconciles actual capacity. The writer stores results in `std::map<uint64_t, ResultChunk>`, writes only the next expected sequence, releases permits after writing, and rejects a missing final sequence.

- [ ] **Step 5: Implement cancellation**

Store the first `exception_ptr`, cancel queues and memory waits, let the last worker close results, join every thread, stop the progress reporter, and rethrow only after joins. Record concurrency correctness as unverified.

- [ ] **Step 6: Static-review ordering invariants**

Confirm the coordinator writes filtered leading headers synchronously before any pipeline thread starts, and only the ordered writer writes data-result chunks afterward. Workers never write files or stdout; sequence increments only in the reader; and writer increments `next_sequence` only after a successful complete chunk write.

---

### Task 8: Author Transactional Output and Main Orchestration

**Files:**
- Create: `include/convert_to_biallelic/output_transaction.hpp`
- Create: `src/output_transaction.cpp`
- Create: `src/main.cpp`

**Interfaces:**
- Consumes: `Config` and all completed components.
- Produces: executable lifecycle, final file publication, and exit codes.

- [ ] **Step 1: Implement output transaction**

Exclusively create and retain a native output identity in the destination directory. Duplicate that HANDLE/file descriptor for `OutputSink`; main must not reopen a temporary pathname. Prefer Linux `O_TMPFILE`, with an exclusive named fallback where unsupported. Without `--force`, reject an existing destination during preflight and use no-overwrite publication so a concurrently created destination is not replaced. Force publication may replace the final destination without pre-deleting it. A nonthrowing destructor disposes transaction-owned state best-effort, and `commit()` is idempotent.

The threat model is normal, non-adversarial operation, including failures, temporary-name collisions, and concurrent creation of the final destination. The output directory must not be modified by an adversarial process while the transaction is active. Windows forced `MoveFileExW` and Linux named-temporary/anonymous-stage publication are pathname-based and are not hardened against same-directory substitution between best-effort identity checks and publication.

- [ ] **Step 2: Implement main lifecycle**

Order operations exactly:

1. Parse CLI and handle help/version.
2. Open and load annotation.
3. Open input and detect compression.
4. Allocate conversion/input/output threads.
5. Create output transaction.
6. Duplicate the transaction's retained native output and construct `OutputSink` from the owned descriptor using the requested format, not temporary extension.
7. Run pipeline with `std::cout` progress and `std::cerr` diagnostics.
8. Flush/close output.
9. Commit transaction.
10. Print final progress summary unless quiet.

- [ ] **Step 3: Implement exit mapping**

Return 0 for help/version/success, 2 for CLI errors, and 1 for annotation, input, conversion, output, HTSlib, memory, or thread failures. Emit one primary exception message to stderr.

- [ ] **Step 4: Static-review resource order**

Confirm C++ object construction/destruction order closes reporter/threads before streams and streams before transaction cleanup. Confirm descriptor/HANDLE ownership and best-effort path-identity checks. Record Windows/POSIX publication behavior and the non-adversarial output-directory threat model as unverified.

---

### Task 9: Perform Whole-Source Static Review and Handoff

**Files:**
- Modify: `README.md`
- Modify: `UNVERIFIED.md`
- Inspect: all `include/convert_to_biallelic/*.hpp`
- Inspect: all `src/*.cpp`

**Interfaces:**
- Consumes: complete authored source tree.
- Produces: an honest source-only handoff with unresolved verification work.

- [ ] **Step 1: Check file and declaration coverage**

Use `rg` to list every `ctb::` declaration and definition. Reconcile misspelled names, missing includes visible from inspection, duplicate types, namespace mismatches, and CMake source-list omissions without running the compiler.

- [ ] **Step 2: Check approved requirements statically**

Confirm source text contains HTSlib line I/O, BGZF output, `.vcf` output, a 2 GiB default, stdout progress, stderr diagnostics, bounded queues, dynamic chunks, sequence reordering, Python header filters, MA/UK handling, GT/GQ handling, and explicit output paths.

- [ ] **Step 3: Update the unverified ledger**

List every deferred requirement:

- compiler and linker success;
- HTSlib API/ABI compatibility;
- Windows and Linux builds;
- Python byte equivalence;
- `.vcf` and `.vcf.gz` round trips;
- multithread ordering and deadlock freedom;
- memory-limit behavior;
- progress routing;
- transactional cleanup;
- throughput and scaling.

- [ ] **Step 4: Deliver without success claims**

Report the created source files and explicitly state: “Source authored but not compiled or tested at the user’s request.” Do not describe the converter as working, complete, passing, fast, or compatible until future execution verifies those properties.
````````

## `docs/superpowers/plans/2026-07-18-cpp-multithreaded-vcf-converter.md`

````````text
# C++ Multithreaded VCF Converter Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Build a Windows/Linux C++17 replacement for `convert-to-biallelic.py` that uses HTSlib, produces Python-compatible `.vcf` or `.vcf.gz`, converts records in parallel without reordering them, targets 2 GiB total memory, and reports progress to stdout.

**Architecture:** Stream annotation data into one immutable index, then stream the input through a bounded reader/worker/ordered-writer pipeline. Conversion workers operate only on text records and shared read-only annotation state; HTSlib supplies line-oriented input and plain/BGZF output, while a process-wide budget applies backpressure to all queued buffers.

**Tech Stack:** C++17, CMake 3.20+, HTSlib 1.17+, CTest, Python 3 differential-test harness, MSYS2 UCRT64/MinGW on Windows, pkg-config on Linux.

## Global Constraints

- Preserve byte-identical uncompressed output relative to `convert-to-biallelic.py` for supported newline-terminated VCF inputs.
- Support `.vcf` and `.vcf.gz` input and output; `.vcf.gz` output must be BGZF.
- Keep VCF output in the explicit `--output` file, progress on stdout, and diagnostics on stderr.
- Default `--memory-limit` to a process-wide soft target of 2 GiB, not 2 GiB per thread.
- Never pre-count input records and never load the multiallelic input VCF in full.
- Preserve deterministic input and expansion order at every thread count.
- Use C++17 and HTSlib 1.17 or newer.
- On Windows, build with MSYS2 UCRT64 and `mingw-w64-ucrt-x86_64-htslib`; the current vcpkg HTSlib port excludes Windows.
- On Linux, discover HTSlib through pkg-config.
- Keep `convert-to-biallelic.py` unchanged as the differential oracle.
- Execution prerequisite: install Git or place it on PATH before implementation; do not skip the commit checkpoints below.

## File Map

- `CMakeLists.txt`: root build, HTSlib/Threads discovery, executable, tests.
- `.github/workflows/ci.yml`: Linux and MSYS2 UCRT64 build/test matrix.
- `include/convert_to_biallelic/cli.hpp`, `src/cli.cpp`: configuration and argument validation.
- `include/convert_to_biallelic/vcf_io.hpp`, `src/vcf_io.cpp`: HTSlib line input and plain/BGZF output.
- `include/convert_to_biallelic/annotation_index.hpp`, `src/annotation_index.cpp`: immutable chromosome/ID lookup.
- `include/convert_to_biallelic/converter.hpp`, `src/converter.cpp`: pure header and record conversion.
- `include/convert_to_biallelic/memory_budget.hpp`, `src/memory_budget.cpp`: process-wide tracked-byte permits.
- `include/convert_to_biallelic/bounded_queue.hpp`: cancellation-aware bounded queue template.
- `include/convert_to_biallelic/pipeline.hpp`, `src/pipeline.cpp`: chunk creation, workers, reordering, cancellation.
- `include/convert_to_biallelic/progress.hpp`, `src/progress.cpp`: atomic counters and stdout reporter.
- `include/convert_to_biallelic/output_transaction.hpp`, `src/output_transaction.cpp`: same-directory temporary output and final rename.
- `src/main.cpp`: lifecycle orchestration and exit codes only.
- `tests/test_support.hpp`: minimal assertion helpers.
- `tests/unit/*.cpp`: focused C++ unit tests.
- `tests/fixtures/*`: small text fixtures generated into VCF and VCF.GZ forms.
- `tests/differential_test.py`: Python-versus-C++ byte comparison.
- `tests/integration_test.py`: formats, progress, failures, and determinism.
- `benchmarks/generate_fixture.py`, `benchmarks/run_benchmark.py`: reproducible scaling benchmark.
- `docs/build-linux.md`, `docs/build-windows.md`: dependency and build commands.

---

### Task 1: Establish the Cross-Platform Build and Test Harness

**Files:**
- Create: `CMakeLists.txt`
- Create: `src/main.cpp`
- Create: `tests/CMakeLists.txt`
- Create: `tests/smoke_test.py`

**Interfaces:**
- Consumes: HTSlib exposed as `PkgConfig::HTSLIB` and platform threads as `Threads::Threads`.
- Produces: executable target `convert-to-biallelic`; CTest test `smoke-version`.

- [ ] **Step 1: Write the failing smoke test**

```python
# tests/smoke_test.py
import subprocess
import sys

exe = sys.argv[1]
result = subprocess.run([exe, "--version"], text=True, capture_output=True)
assert result.returncode == 0, result.stderr
assert result.stdout == "convert-to-biallelic 0.1.0\n"
assert result.stderr == ""
```

- [ ] **Step 2: Add the initial CMake configuration without an executable implementation**

```cmake
# CMakeLists.txt
cmake_minimum_required(VERSION 3.20)
project(convert_to_biallelic VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

find_package(Threads REQUIRED)
find_package(PkgConfig REQUIRED)
pkg_check_modules(HTSLIB REQUIRED IMPORTED_TARGET htslib>=1.17)
find_package(Python3 REQUIRED COMPONENTS Interpreter)

enable_testing()
add_subdirectory(tests)
```

```cmake
# tests/CMakeLists.txt
add_test(
  NAME smoke-version
  COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/smoke_test.py
          $<TARGET_FILE:convert-to-biallelic>
)
```

- [ ] **Step 3: Configure and verify the missing-target failure**

Run on Linux or MSYS2 UCRT64:

```bash
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Debug
```

Expected: configuration fails because `convert-to-biallelic` is referenced but not defined.

- [ ] **Step 4: Add the minimal executable**

```cpp
// src/main.cpp
#include <iostream>
#include <string_view>

int main(int argc, char** argv) {
    if (argc == 2 && std::string_view(argv[1]) == "--version") {
        std::cout << "convert-to-biallelic 0.1.0\n";
        return 0;
    }
    std::cerr << "usage: convert-to-biallelic --help\n";
    return 2;
}
```

Append before `enable_testing()` in `CMakeLists.txt`:

```cmake
add_executable(convert-to-biallelic src/main.cpp)
target_link_libraries(convert-to-biallelic PRIVATE PkgConfig::HTSLIB Threads::Threads)
target_compile_options(convert-to-biallelic PRIVATE
  $<$<CXX_COMPILER_ID:GNU,Clang>:-Wall -Wextra -Wpedantic -Werror>
  $<$<CXX_COMPILER_ID:MSVC>:/W4 /WX>
)
```

- [ ] **Step 5: Build and run the smoke test**

```bash
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Debug
cmake --build build
ctest --test-dir build --output-on-failure -R smoke-version
```

Expected: `100% tests passed, 0 tests failed`.

- [ ] **Step 6: Commit the build bootstrap**

```bash
git add CMakeLists.txt src/main.cpp tests/CMakeLists.txt tests/smoke_test.py
git commit -m "build: add HTSlib C++ project bootstrap"
```

---

### Task 2: Implement CLI Parsing and Thread Allocation

**Files:**
- Create: `include/convert_to_biallelic/cli.hpp`
- Create: `src/cli.cpp`
- Create: `tests/test_support.hpp`
- Create: `tests/unit/test_cli.cpp`
- Modify: `CMakeLists.txt`
- Modify: `tests/CMakeLists.txt`

**Interfaces:**
- Consumes: raw `argc` and `argv` values.
- Produces: `ctb::Config ctb::parse_cli(int, char**)`, `ctb::ThreadAllocation ctb::allocate_threads(const Config&, bool, bool)`, and `ctb::UsageRequested`.

- [ ] **Step 1: Declare the exact configuration interface**

```cpp
// include/convert_to_biallelic/cli.hpp
#pragma once
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <stdexcept>

namespace ctb {
enum class OutputFormat { vcf, vcf_gz };

struct Config {
    std::filesystem::path variants;
    std::filesystem::path input;
    std::filesystem::path output;
    std::size_t threads = 1;
    std::uint64_t memory_limit_bytes = 2ULL * 1024ULL * 1024ULL * 1024ULL;
    std::chrono::milliseconds progress_interval{5000};
    bool quiet = false;
    bool force = false;
    OutputFormat output_format = OutputFormat::vcf;
};

struct ThreadAllocation {
    std::size_t conversion_workers;
    std::size_t input_io_workers;
    std::size_t output_io_workers;
};

class UsageRequested final : public std::exception {
public:
    explicit UsageRequested(bool version) : version_(version) {}
    bool version() const noexcept { return version_; }
    const char* what() const noexcept override { return "usage requested"; }
private:
    bool version_;
};

Config parse_cli(int argc, char** argv);
ThreadAllocation allocate_threads(const Config& config, bool compressed_input,
                                  bool compressed_output);
const char* usage_text();
}
```

- [ ] **Step 2: Write failing CLI tests**

```cpp
// tests/unit/test_cli.cpp
#include "convert_to_biallelic/cli.hpp"
#include "../test_support.hpp"
#include <string>
#include <vector>

static ctb::Config parse(std::vector<std::string> args) {
    std::vector<char*> raw;
    for (auto& arg : args) raw.push_back(arg.data());
    return ctb::parse_cli(static_cast<int>(raw.size()), raw.data());
}

int main() {
    const auto cfg = parse({"tool", "--variants", "a.vcf.gz", "--input", "i.vcf",
                            "--output", "o.vcf.gz", "--threads", "8",
                            "--memory-limit", "2G", "--progress-interval", "3"});
    CHECK(cfg.output_format == ctb::OutputFormat::vcf_gz);
    CHECK(cfg.threads == 8);
    CHECK(cfg.memory_limit_bytes == 2147483648ULL);
    CHECK(cfg.progress_interval.count() == 3000);

    const auto allocation = ctb::allocate_threads(cfg, true, true);
    CHECK(allocation.conversion_workers == 6);
    CHECK(allocation.input_io_workers == 1);
    CHECK(allocation.output_io_workers == 1);

    CHECK_THROWS(parse({"tool", "--variants", "a.vcf.gz", "--input", "i.vcf",
                        "--output", "o.txt"}));
    CHECK_THROWS(parse({"tool", "--variants", "a.vcf.gz", "--input", "i.vcf",
                        "--output", "o.vcf", "--threads", "0"}));
    return 0;
}
```

```cpp
// tests/test_support.hpp
#pragma once
#include <iostream>
#include <stdexcept>

#define CHECK(expr) do { if (!(expr)) { \
    std::cerr << __FILE__ << ':' << __LINE__ << ": CHECK failed: " #expr "\n"; \
    return 1; } } while (false)

#define CHECK_THROWS(expr) do { bool caught_ = false; try { (void)(expr); } \
    catch (const std::exception&) { caught_ = true; } \
    if (!caught_) { std::cerr << __FILE__ << ':' << __LINE__ \
    << ": expected exception: " #expr "\n"; return 1; } } while (false)
```

- [ ] **Step 3: Register and run the failing test**

Replace the Task 1 executable definition in `CMakeLists.txt` with:

```cmake
add_library(ctb_core STATIC src/cli.cpp)
target_include_directories(ctb_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(ctb_core PUBLIC PkgConfig::HTSLIB Threads::Threads)
target_compile_options(ctb_core PRIVATE
  $<$<CXX_COMPILER_ID:GNU,Clang>:-Wall -Wextra -Wpedantic -Werror>
  $<$<CXX_COMPILER_ID:MSVC>:/W4 /WX>
)

add_executable(convert-to-biallelic src/main.cpp)
target_link_libraries(convert-to-biallelic PRIVATE ctb_core)
```

Append to `tests/CMakeLists.txt`:

```cmake
add_executable(test_cli unit/test_cli.cpp)
target_link_libraries(test_cli PRIVATE ctb_core)
add_test(NAME unit-cli COMMAND test_cli)
```

Then run:

```bash
cmake --build build
ctest --test-dir build --output-on-failure -R unit-cli
```

Expected: link failure for `parse_cli` and `allocate_threads`.

- [ ] **Step 4: Implement exact parsing rules**

Implement `src/cli.cpp` with these rules:

```cpp
namespace ctb {
static std::uint64_t parse_size(std::string text);
static std::size_t parse_positive_count(std::string_view option, std::string_view text);
static OutputFormat infer_output_format(const std::filesystem::path& path);

ThreadAllocation allocate_threads(const Config& c, bool in_gz, bool out_gz) {
    std::size_t remaining = c.threads;
    std::size_t out = out_gz && remaining > 1 ? 1 : 0;
    remaining -= out;
    std::size_t in = in_gz && c.threads >= 4 && remaining > 1 ? 1 : 0;
    remaining -= in;
    return {remaining, in, out};
}
}
```

`parse_cli` must reject duplicate options, missing values, `threads < 1`, sizes below 64 MiB, negative progress intervals, unknown extensions without `--output-format`, and missing required paths. `K`, `M`, and `G` are case-insensitive binary suffixes. `--quiet`, `--force`, `--help`, and `--version` take no value.

- [ ] **Step 5: Run CLI tests and the full suite**

```bash
cmake --build build
ctest --test-dir build --output-on-failure
```

Expected: smoke and CLI tests pass.

- [ ] **Step 6: Commit CLI behavior**

```bash
git add CMakeLists.txt tests/CMakeLists.txt include/convert_to_biallelic/cli.hpp src/cli.cpp tests/test_support.hpp tests/unit/test_cli.cpp
git commit -m "feat: add converter command line configuration"
```

---

### Task 3: Add HTSlib Text Input and VCF/BGZF Output

**Files:**
- Create: `include/convert_to_biallelic/vcf_io.hpp`
- Create: `src/vcf_io.cpp`
- Create: `tests/unit/test_vcf_io.cpp`
- Modify: `CMakeLists.txt`
- Modify: `tests/CMakeLists.txt`

**Interfaces:**
- Consumes: filesystem paths, `OutputFormat`, and allocated I/O worker counts.
- Produces: `ctb::InputSource::getline`, `ctb::OutputSink::write`, `flush`, and `close`.

- [ ] **Step 1: Declare RAII I/O interfaces**

```cpp
// include/convert_to_biallelic/vcf_io.hpp
#pragma once
#include "cli.hpp"
#include <filesystem>
#include <memory>
#include <string>
#include <string_view>

namespace ctb {
class InputSource {
public:
    InputSource(const std::filesystem::path& path, std::size_t io_workers);
    ~InputSource();
    InputSource(InputSource&&) noexcept;
    InputSource& operator=(InputSource&&) noexcept;
    bool getline(std::string& line);
    bool compressed() const noexcept;
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

class OutputSink {
public:
    OutputSink(const std::filesystem::path& path, OutputFormat format,
               std::size_t io_workers);
    ~OutputSink();
    OutputSink(OutputSink&&) noexcept;
    OutputSink& operator=(OutputSink&&) noexcept;
    void write(std::string_view bytes);
    void flush();
    void close();
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
}
```

- [ ] **Step 2: Write failing plain/BGZF round-trip tests**

```cpp
// tests/unit/test_vcf_io.cpp
#include "convert_to_biallelic/vcf_io.hpp"
#include "../test_support.hpp"
#include <filesystem>
#include <string>

static int round_trip(const std::filesystem::path& path, ctb::OutputFormat format) {
    {
        ctb::OutputSink out(path, format, 2);
        out.write("##fileformat=VCFv4.2\n#CHROM\tPOS\n1\t10\n");
        out.close();
    }
    ctb::InputSource in(path, 2);
    std::string line;
    CHECK(in.getline(line)); CHECK(line == "##fileformat=VCFv4.2");
    CHECK(in.getline(line)); CHECK(line == "#CHROM\tPOS");
    CHECK(in.getline(line)); CHECK(line == "1\t10");
    CHECK(!in.getline(line));
    std::filesystem::remove(path);
    return 0;
}

int main() {
    CHECK(round_trip("io-test.vcf", ctb::OutputFormat::vcf) == 0);
    CHECK(round_trip("io-test.vcf.gz", ctb::OutputFormat::vcf_gz) == 0);
    return 0;
}
```

- [ ] **Step 3: Run the test and confirm missing symbols**

```bash
cmake --build build
ctest --test-dir build --output-on-failure -R unit-vcf-io
```

Expected: link failure for `InputSource` and `OutputSink`.

- [ ] **Step 4: Implement HTSlib wrappers**

Use `hts_open(path, "r")`, verify `hts_get_format()` reports VCF/text with no compression or gzip/BGZF, call `hts_set_threads` only when `io_workers > 0`, read with `hts_getline`, and close with `hts_close`. Treat `-1` as EOF and values below `-1` as errors.

For plain output use `hopen(path, "wb")`, `hwrite`, `hflush`, and `hclose`. For compressed output use `bgzf_open(path, "w")`, `bgzf_mt` when `io_workers > 0`, `bgzf_write`, `bgzf_flush`, and `bgzf_close`. Loop until all requested bytes are written and throw `std::runtime_error` with the path on every failure. Destructors must not throw; explicit `close()` propagates close errors.

- [ ] **Step 5: Run I/O and full tests**

```bash
cmake --build build
ctest --test-dir build --output-on-failure
```

Expected: all tests pass and both temporary files are removed.

- [ ] **Step 6: Commit HTSlib I/O**

```bash
git add CMakeLists.txt tests/CMakeLists.txt include/convert_to_biallelic/vcf_io.hpp src/vcf_io.cpp tests/unit/test_vcf_io.cpp
git commit -m "feat: add HTSlib VCF and BGZF streams"
```

---

### Task 4: Build the Immutable Annotation Index

**Files:**
- Create: `include/convert_to_biallelic/annotation_index.hpp`
- Create: `src/annotation_index.cpp`
- Create: `tests/unit/test_annotation_index.cpp`
- Modify: `CMakeLists.txt`
- Modify: `tests/CMakeLists.txt`

**Interfaces:**
- Consumes: `InputSource&`, annotation record text, and a memory limit.
- Produces: immutable `AnnotationIndex`, `VariantDefinition`, `load_annotation`, and `estimated_bytes()`.

- [ ] **Step 1: Declare the index API**

```cpp
// include/convert_to_biallelic/annotation_index.hpp
#pragma once
#include "vcf_io.hpp"
#include <cstdint>
#include <string>
#include <string_view>
#include <unordered_map>

namespace ctb {
struct VariantDefinition {
    std::int64_t position;
    std::string ref;
    std::string alt;
};

class AnnotationIndex {
public:
    const VariantDefinition* find(std::string_view chromosome,
                                  std::string_view id) const noexcept;
    std::uint64_t estimated_bytes() const noexcept;
    std::uint64_t variant_count() const noexcept;
private:
    using ById = std::unordered_map<std::string, VariantDefinition>;
    std::unordered_map<std::string, ById> variants_;
    std::uint64_t estimated_bytes_ = 0;
    friend AnnotationIndex load_annotation(InputSource&, std::uint64_t);
};

AnnotationIndex load_annotation(InputSource& source, std::uint64_t memory_limit);
}
```

- [ ] **Step 2: Write failing loader tests**

Create an annotation containing comments, two chromosomes, and a duplicate chromosome/ID whose last definition differs. Assert lookup, last-definition-wins, missing lookup, variant count, and nonzero memory estimate. Add failure cases for fewer than eight columns, missing `ID=`, comma-separated multiple IDs, invalid position, and an estimate exceeding the supplied limit.

Core assertions:

```cpp
const auto* v = index.find("chr1", "v1");
CHECK(v != nullptr);
CHECK(v->position == 12);
CHECK(v->ref == "G");
CHECK(v->alt == "T");
CHECK(index.find("chr2", "missing") == nullptr);
CHECK(index.variant_count() == 2);
CHECK(index.estimated_bytes() > 0);
```

- [ ] **Step 3: Run and verify the failing test**

```bash
cmake --build build
ctest --test-dir build --output-on-failure -R unit-annotation
```

Expected: unresolved `load_annotation` and `AnnotationIndex` methods.

- [ ] **Step 4: Implement streaming parsing and accounting**

Split records on tabs, require at least eight columns, scan semicolon-separated INFO entries for the first exact `ID=` key, require exactly one comma-delimited ID, and parse POS with `std::from_chars` into positive `std::int64_t`. Insert with `variants_[chromosome][id] = definition` so the last entry wins. Recompute the conservative estimate after each insertion using string capacities, `sizeof` node payloads, and bucket arrays; fail before insertion would exceed `memory_limit - memory_limit / 10`.

- [ ] **Step 5: Run annotation and full tests**

```bash
cmake --build build
ctest --test-dir build --output-on-failure
```

Expected: all tests pass.

- [ ] **Step 6: Commit the annotation index**

```bash
git add CMakeLists.txt tests/CMakeLists.txt include/convert_to_biallelic/annotation_index.hpp src/annotation_index.cpp tests/unit/test_annotation_index.cpp
git commit -m "feat: load immutable variant annotation index"
```

---

### Task 5: Implement Python-Compatible Record Conversion

**Files:**
- Create: `include/convert_to_biallelic/converter.hpp`
- Create: `src/converter.cpp`
- Create: `tests/unit/test_converter.cpp`
- Modify: `CMakeLists.txt`
- Modify: `tests/CMakeLists.txt`

**Interfaces:**
- Consumes: one header or data line, immutable `AnnotationIndex`, and source line number.
- Produces: `convert_header` and `convert_record` with complete newline-terminated output text.

- [ ] **Step 1: Declare conversion results**

```cpp
// include/convert_to_biallelic/converter.hpp
#pragma once
#include "annotation_index.hpp"
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

namespace ctb {
struct ConversionResult {
    std::string bytes;
    std::uint64_t output_records = 0;
};

std::optional<std::string> convert_header(std::string_view line);
ConversionResult convert_record(std::string_view line,
                                const AnnotationIndex& annotation,
                                std::uint64_t line_number);
}
```

- [ ] **Step 2: Write focused failing tests**

Test all four filtered header declarations and an unchanged header. Build a tiny annotation and assert exact strings for:

- one known ID;
- multiple IDs sorted by annotation POS;
- repeated IDs deduplicated;
- `MA`/`UK` kept while other INFO is removed;
- `GT` with and without `GQ`;
- `|` normalized to `/`;
- missing alleles;
- a single unknown ID passed through;
- unknown ID inside a composite mapping rejected;
- allele index outside `allele_to_ids` rejected.

Use a full exact assertion such as:

```cpp
const auto result = ctb::convert_record(
    "chr1\t100\t.\tA\tC,G\t.\tPASS\tID=v2,v1;MA=1;DROP=x\tGT:GQ\t1|2:42",
    index, 9);
CHECK(result.bytes ==
      "chr1\t10\tv1\tA\tC\t.\tPASS\tID=v1;MA=1\tGT:GQ\t0/1:42\n"
      "chr1\t20\tv2\tA\tG\t.\tPASS\tID=v2;MA=1\tGT:GQ\t1/0:42\n");
CHECK(result.output_records == 2);
```

- [ ] **Step 3: Run and verify failure**

```bash
cmake --build build
ctest --test-dir build --output-on-failure -R unit-converter
```

Expected: unresolved conversion functions.

- [ ] **Step 4: Implement conversion as a pure function**

Parse the tab fields once, parse INFO once while preserving its encounter order for `MA` and `UK`, parse FORMAT once, and parse each sample only once. Build `allele_to_ids` as REF empty string plus comma-separated `INFO/ID` values. Collect `(position, id)` pairs in a deduplicating set, sort by position and then ID for deterministic ties, and emit lines with tabs and LF. Preserve the original unknown single-ID line exactly except for LF normalization. Every error includes `input line <number>`.

- [ ] **Step 5: Compare the unit cases with the Python oracle manually**

```bash
python convert-to-biallelic.py --help
cmake --build build
ctest --test-dir build --output-on-failure -R unit-converter
```

Expected: Python help exits successfully and converter tests pass.

- [ ] **Step 6: Commit pure conversion**

```bash
git add CMakeLists.txt tests/CMakeLists.txt include/convert_to_biallelic/converter.hpp src/converter.cpp tests/unit/test_converter.cpp
git commit -m "feat: reproduce Python biallelic conversion"
```

---

### Task 6: Prove Single-Thread End-to-End Equivalence

**Files:**
- Create: `tests/fixtures/annotation.vcf`
- Create: `tests/fixtures/input.vcf`
- Create: `tests/make_fixtures.py`
- Create: `tests/differential_test.py`
- Create: `include/convert_to_biallelic/pipeline.hpp`
- Create: `src/pipeline.cpp`
- Modify: `src/main.cpp`
- Modify: `CMakeLists.txt`
- Modify: `tests/CMakeLists.txt`

**Interfaces:**
- Consumes: `Config`, `AnnotationIndex`, `InputSource`, `OutputSink`.
- Produces: `PipelineStats run_single_threaded(...)` and a working executable for `--threads 1`.

- [ ] **Step 1: Create representative text fixtures and gzip generator**

The annotation fixture must include every ID used by the known records and at least one duplicate definition. The input fixture must include filtered headers, retained headers, unknown biallelic passthrough, composite allele IDs, phased/unphased/missing genotypes, GQ present/absent, MA/UK, and one record expanding to multiple lines.

```python
# tests/make_fixtures.py
import gzip
import pathlib
import sys

root = pathlib.Path(sys.argv[1])
source = root / "annotation.vcf"
target = root / "annotation.vcf.gz"
with source.open("rb") as incoming:
    with gzip.GzipFile(filename=str(target), mode="wb", mtime=0) as outgoing:
        outgoing.write(incoming.read())
```

- [ ] **Step 2: Write the failing differential harness**

```python
# tests/differential_test.py
import gzip
import pathlib
import subprocess
import sys
import tempfile

exe, python_script, fixture_dir = map(pathlib.Path, sys.argv[1:4])
with tempfile.TemporaryDirectory() as tmp:
    tmp = pathlib.Path(tmp)
    annotation_gz = tmp / "annotation.vcf.gz"
    with (fixture_dir / "annotation.vcf").open("rb") as src:
        with gzip.GzipFile(filename=str(annotation_gz), mode="wb", mtime=0) as dst:
            dst.write(src.read())
    source_bytes = (fixture_dir / "input.vcf").read_bytes()
    oracle = subprocess.run(
        [sys.executable, str(python_script), str(annotation_gz)],
        input=source_bytes, capture_output=True, check=True).stdout
    output = tmp / "result.vcf"
    run = subprocess.run(
        [str(exe), "--variants", str(annotation_gz), "--input",
         str(fixture_dir / "input.vcf"), "--output", str(output),
         "--threads", "1", "--quiet"], capture_output=True, check=True)
    assert run.stdout == b""
    assert run.stderr == b""
    assert output.read_bytes() == oracle
```

- [ ] **Step 3: Register and run the failing differential test**

```bash
cmake --build build
ctest --test-dir build --output-on-failure -R differential-single
```

Expected: failure because the executable does not yet run conversion.

- [ ] **Step 4: Add the single-thread pipeline interface and implementation**

```cpp
// include/convert_to_biallelic/pipeline.hpp
#pragma once
#include "annotation_index.hpp"
#include "cli.hpp"
#include "vcf_io.hpp"
#include <cstdint>

namespace ctb {
struct PipelineStats {
    std::uint64_t input_records = 0;
    std::uint64_t output_records = 0;
    std::uint64_t output_bytes = 0;
    std::uint64_t peak_tracked_bytes = 0;
};

PipelineStats run_single_threaded(InputSource& input, OutputSink& output,
                                  const AnnotationIndex& annotation);
}
```

`run_single_threaded` reads leading headers, calls `convert_header`, then converts each data record with an increasing physical line number. `main` parses the CLI, loads annotation before opening output, creates input/output objects with zero I/O workers, runs this function, closes output explicitly, and maps usage to exit 0, CLI errors to 2, and processing errors to 1.

- [ ] **Step 5: Run the differential and complete suite**

```bash
cmake --build build
ctest --test-dir build --output-on-failure
```

Expected: Python and C++ plain outputs match byte-for-byte.

- [ ] **Step 6: Commit the first working converter**

```bash
git add CMakeLists.txt tests/CMakeLists.txt src/main.cpp include/convert_to_biallelic/pipeline.hpp src/pipeline.cpp tests/fixtures tests/make_fixtures.py tests/differential_test.py
git commit -m "feat: add single-thread differential converter"
```

---

### Task 7: Add Process-Wide Memory Permits and a Bounded Queue

**Files:**
- Create: `include/convert_to_biallelic/memory_budget.hpp`
- Create: `src/memory_budget.cpp`
- Create: `include/convert_to_biallelic/bounded_queue.hpp`
- Create: `tests/unit/test_memory_budget.cpp`
- Create: `tests/unit/test_bounded_queue.cpp`
- Modify: `CMakeLists.txt`
- Modify: `tests/CMakeLists.txt`

**Interfaces:**
- Consumes: configured memory bytes and queue item byte reservations.
- Produces: movable `MemoryPermit`, `MemoryBudget::acquire`, `cancel`, `peak_bytes`, and `BoundedQueue<T>::push/pop/close/cancel`.

- [ ] **Step 1: Declare memory ownership**

```cpp
// include/convert_to_biallelic/memory_budget.hpp
#pragma once
#include <condition_variable>
#include <cstdint>
#include <mutex>

namespace ctb {
class MemoryBudget;
class MemoryPermit {
public:
    MemoryPermit() = default;
    ~MemoryPermit();
    MemoryPermit(MemoryPermit&&) noexcept;
    MemoryPermit& operator=(MemoryPermit&&) noexcept;
    MemoryPermit(const MemoryPermit&) = delete;
    MemoryPermit& operator=(const MemoryPermit&) = delete;
    std::uint64_t bytes() const noexcept;
    void resize(std::uint64_t bytes, bool allow_one_overage);
private:
    MemoryPermit(MemoryBudget* owner, std::uint64_t bytes, bool overage);
    MemoryBudget* owner_ = nullptr;
    std::uint64_t bytes_ = 0;
    bool overage_ = false;
    friend class MemoryBudget;
};

class MemoryBudget {
public:
    explicit MemoryBudget(std::uint64_t tracked_limit);
    MemoryPermit acquire(std::uint64_t bytes, bool allow_oversized_single);
    void cancel();
    std::uint64_t current_bytes() const;
    std::uint64_t peak_bytes() const;
private:
    void release(std::uint64_t bytes);
    mutable std::mutex mutex_;
    std::condition_variable changed_;
    std::uint64_t limit_;
    std::uint64_t current_ = 0;
    std::uint64_t peak_ = 0;
    bool cancelled_ = false;
    bool overage_active_ = false;
    friend class MemoryPermit;
};
}
```

- [ ] **Step 2: Write blocking, release, cancellation, and queue tests**

Tests must prove a second acquisition blocks until the first permit is destroyed, moving a permit releases exactly once, resizing changes tracked bytes, only one permitted overage can be active, releasing that overage wakes the next waiter, cancellation wakes a blocked acquisition with an exception, FIFO order is preserved, close drains queued items then returns `false`, and cancel wakes both blocked producers and consumers.

- [ ] **Step 3: Run and verify failures**

```bash
cmake --build build
ctest --test-dir build --output-on-failure -R "unit-(memory|queue)"
```

Expected: missing type or link failures.

- [ ] **Step 4: Implement permit accounting and the header-only queue**

Use predicates on every condition-variable wait. `MemoryPermit::~MemoryPermit` calls `owner_->release(bytes_)` only when `owner_ != nullptr`. Normal acquisition/resizing waits for `current + requested_delta <= limit`. When `allow_one_overage` is true, exactly one caller may exceed the limit; it sets `overage_active_`, and its permit clears that flag on shrink or destruction. This guarantees that one worker can finish and unblock the writer instead of all workers deadlocking while growing results. `BoundedQueue<T>` stores `std::deque<T>`, has a fixed item capacity, and uses separate not-empty/not-full condition variables under one mutex. `cancel()` marks cancellation and clears queued items so their permits release immediately.

- [ ] **Step 5: Run concurrency tests repeatedly**

```bash
cmake --build build
ctest --test-dir build --output-on-failure -R "unit-(memory|queue)" --repeat until-fail:50
```

Expected: all 50 repetitions pass without hangs.

- [ ] **Step 6: Commit bounded memory primitives**

```bash
git add CMakeLists.txt tests/CMakeLists.txt include/convert_to_biallelic/memory_budget.hpp src/memory_budget.cpp include/convert_to_biallelic/bounded_queue.hpp tests/unit/test_memory_budget.cpp tests/unit/test_bounded_queue.cpp
git commit -m "feat: add bounded pipeline memory primitives"
```

---

### Task 8: Implement the Ordered Multithreaded Pipeline

**Files:**
- Modify: `include/convert_to_biallelic/pipeline.hpp`
- Modify: `src/pipeline.cpp`
- Create: `tests/unit/test_pipeline.cpp`
- Modify: `src/main.cpp`
- Modify: `CMakeLists.txt`
- Modify: `tests/CMakeLists.txt`

**Interfaces:**
- Consumes: annotation, I/O streams, thread allocation, memory target, and optional test hook.
- Produces: `PipelineStats run_pipeline(...)` with deterministic ordered output and first-error cancellation.

- [ ] **Step 1: Add exact chunk and pipeline types**

```cpp
namespace ctb {
struct WorkChunk {
    std::uint64_t sequence;
    std::uint64_t first_line_number;
    std::vector<std::string> records;
    MemoryPermit permit;
};

struct ResultChunk {
    std::uint64_t sequence;
    std::string bytes;
    std::uint64_t input_records;
    std::uint64_t output_records;
    MemoryPermit permit;
};

struct PipelineOptions {
    std::size_t conversion_workers;
    std::uint64_t memory_limit_bytes;
    std::size_t target_records_per_chunk = 512;
    std::uint64_t target_bytes_per_chunk = 8ULL * 1024ULL * 1024ULL;
};

PipelineStats run_pipeline(InputSource& input, OutputSink& output,
                           const AnnotationIndex& annotation,
                           const PipelineOptions& options);
}
```

- [ ] **Step 2: Write a failing forced-reordering test**

Add an internal test-only worker hook that sleeps on sequence zero and immediately completes later chunks. Feed at least four chunks, assert the observed completion order differs from sequence order, and assert writer output remains byte-identical to single-thread output. Add a worker exception test that returns within five seconds, joins every thread, and reports the first input line error.

- [ ] **Step 3: Run and verify failure**

```bash
cmake --build build
ctest --test-dir build --output-on-failure -R unit-pipeline
```

Expected: failure because `run_pipeline` is not implemented.

- [ ] **Step 4: Implement reader, workers, and ordered writer**

Reader behavior:

1. Convert and write leading headers before submitting data chunks.
2. Accumulate until 512 records or 8 MiB, whichever comes first.
3. Acquire a permit for stored string capacities.
4. Push to a work queue capped at `2 * conversion_workers`.
5. Close the work queue at EOF.

Worker behavior:

1. Pop a work chunk.
2. Convert its records sequentially into one result string, calling `permit.resize(input_capacity + next_output_capacity, true)` before each output-buffer reserve.
3. Release the input record strings, then shrink the permit to the final result capacity before queueing it.
4. Push one result with the same sequence.
5. The last exiting worker closes the result queue.

Writer behavior:

1. Store received results in `std::map<std::uint64_t, ResultChunk>`.
2. Repeatedly write and erase `next_sequence` when present.
3. Throw if the queue closes with a missing sequence.

The first exception is stored in `std::exception_ptr` under a mutex; cancellation closes both queues and cancels the memory budget. Join all threads before rethrowing.

- [ ] **Step 5: Run deterministic and differential tests across thread counts**

Extend `differential_test.py` to run `--threads 1,2,4,8` and compare every output to the same oracle.

```bash
cmake --build build
ctest --test-dir build --output-on-failure -R "unit-pipeline|differential"
ctest --test-dir build --output-on-failure -R unit-pipeline --repeat until-fail:25
```

Expected: every output matches and no repetition hangs.

- [ ] **Step 6: Commit ordered parallel conversion**

```bash
git add CMakeLists.txt tests/CMakeLists.txt include/convert_to_biallelic/pipeline.hpp src/pipeline.cpp src/main.cpp tests/unit/test_pipeline.cpp tests/differential_test.py
git commit -m "feat: add ordered multithreaded conversion pipeline"
```

---

### Task 9: Add Stdout Progress and Final Statistics

**Files:**
- Create: `include/convert_to_biallelic/progress.hpp`
- Create: `src/progress.cpp`
- Create: `tests/unit/test_progress.cpp`
- Modify: `include/convert_to_biallelic/pipeline.hpp`
- Modify: `src/pipeline.cpp`
- Modify: `src/main.cpp`
- Modify: `CMakeLists.txt`
- Modify: `tests/CMakeLists.txt`

**Interfaces:**
- Consumes: atomic pipeline counters, configured interval, quiet flag, and `std::ostream&`.
- Produces: `ProgressCounters`, `ProgressReporter::start/finish`, periodic lines, and one final summary.

- [ ] **Step 1: Declare progress state**

```cpp
// include/convert_to_biallelic/progress.hpp
#pragma once
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <iosfwd>
#include <mutex>
#include <thread>

namespace ctb {
struct PipelineStats;

struct ProgressCounters {
    std::atomic<std::uint64_t> input_records{0};
    std::atomic<std::uint64_t> output_records{0};
    std::atomic<std::uint64_t> tracked_memory{0};
};

class ProgressReporter {
public:
    ProgressReporter(ProgressCounters&, std::chrono::milliseconds,
                     std::ostream&, bool quiet);
    ~ProgressReporter();
    void start();
    void finish(const PipelineStats& stats);
private:
    void run();
    ProgressCounters& counters_;
    std::chrono::milliseconds interval_;
    std::ostream& output_;
    bool quiet_;
    std::atomic<bool> stopping_{false};
    std::mutex mutex_;
    std::condition_variable changed_;
    std::thread thread_;
    std::chrono::steady_clock::time_point started_;
};
}
```

- [ ] **Step 2: Write failing formatting and quiet-mode tests**

Use `std::ostringstream` and a 10 ms interval. Update counters, wait up to 100 ms, finish, and assert output contains `input=`, `output=`, `rate=`, `memory=`, and `Finished:`. Assert quiet mode produces an empty string. Assert the reporter joins on destruction.

- [ ] **Step 3: Run and verify failure**

```bash
cmake --build build
ctest --test-dir build --output-on-failure -R unit-progress
```

Expected: missing progress symbols.

- [ ] **Step 4: Implement the single reporter thread**

Use `steady_clock`, atomic loads with relaxed ordering, `std::condition_variable` for interruptible interval waits, integer MiB formatting, and one `output_ << line << '\n' << std::flush` operation per report. Never write from conversion workers. `main` passes `std::cout`; exceptions and warnings continue to use `std::cerr`.

- [ ] **Step 5: Add progress integration assertions**

Run without `--quiet` and assert stdout matches periodic/final field names while the output VCF still matches the oracle. Run with `--quiet` and assert stdout is empty.

```bash
cmake --build build
ctest --test-dir build --output-on-failure -R "unit-progress|integration-progress|differential"
```

Expected: all tests pass.

- [ ] **Step 6: Commit progress reporting**

```bash
git add CMakeLists.txt tests/CMakeLists.txt include/convert_to_biallelic/progress.hpp src/progress.cpp include/convert_to_biallelic/pipeline.hpp src/pipeline.cpp src/main.cpp tests/unit/test_progress.cpp tests/integration_test.py
git commit -m "feat: report conversion progress on stdout"
```

---

### Task 10: Add Transactional Output and Failure Cleanup

**Files:**
- Create: `include/convert_to_biallelic/output_transaction.hpp`
- Create: `src/output_transaction.cpp`
- Create: `tests/unit/test_output_transaction.cpp`
- Modify: `src/main.cpp`
- Modify: `CMakeLists.txt`
- Modify: `tests/CMakeLists.txt`

**Interfaces:**
- Consumes: final output path and `--force`.
- Produces: unique same-directory temporary path, `commit()`, and destructor cleanup.

- [ ] **Step 1: Declare transactional output**

```cpp
// include/convert_to_biallelic/output_transaction.hpp
#pragma once
#include <filesystem>

namespace ctb {
class OutputTransaction {
public:
    OutputTransaction(std::filesystem::path destination, bool force);
    ~OutputTransaction();
    const std::filesystem::path& temporary_path() const noexcept;
    void commit();
private:
    std::filesystem::path destination_;
    std::filesystem::path temporary_;
    bool force_;
    bool committed_ = false;
};
}
```

- [ ] **Step 2: Write failing safety tests**

Test that construction rejects an existing destination without force, a destroyed uncommitted transaction removes its temporary file, commit renames complete content, force replaces an existing destination on both platforms, and two transactions use different names.

- [ ] **Step 3: Run and verify failure**

```bash
cmake --build build
ctest --test-dir build --output-on-failure -R unit-output-transaction
```

Expected: missing transaction symbols.

- [ ] **Step 4: Implement same-directory finalize behavior**

Create names of the form `.<filename>.ctb.<process-id>.<counter>.tmp` with exclusive creation. On POSIX, finalize with `std::filesystem::rename`; when force replacement requires it, remove only the already-validated exact destination immediately before rename. On Windows, use `MoveFileExW(temporary, destination, MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)` when forced. Destructor removes only `temporary_` and ignores cleanup errors.

`main` must construct the transaction after annotation succeeds, point `OutputSink` at its temporary path while retaining the requested final format, close the sink, then call `commit()`. Any exception leaves the previous destination untouched unless `--force` replacement reached the final commit operation.

- [ ] **Step 5: Run injected-failure integration tests**

```bash
cmake --build build
ctest --test-dir build --output-on-failure -R "output-transaction|integration-failure"
```

Expected: nonzero exit, one stderr diagnostic, no published partial destination, and no `.ctb.*.tmp` file.

- [ ] **Step 6: Commit output safety**

```bash
git add CMakeLists.txt tests/CMakeLists.txt include/convert_to_biallelic/output_transaction.hpp src/output_transaction.cpp src/main.cpp tests/unit/test_output_transaction.cpp tests/integration_test.py
git commit -m "feat: publish converted VCF transactionally"
```

---

### Task 11: Complete Format, Memory, and Error Integration Coverage

**Files:**
- Modify: `tests/differential_test.py`
- Modify: `tests/integration_test.py`
- Modify: `tests/CMakeLists.txt`
- Modify: `src/main.cpp`
- Modify: `src/pipeline.cpp`

**Interfaces:**
- Consumes: the complete CLI and executable.
- Produces: verified four-way format support, memory backpressure, deterministic threading, and stable exit behavior.

- [ ] **Step 1: Add failing four-format tests**

For `.vcf` and `.vcf.gz` input crossed with `.vcf` and `.vcf.gz` output, run threads 1 and 4, decompress outputs when required, and compare with the Python oracle. Verify `.vcf.gz` begins with gzip magic bytes and can be read through HTSlib.

- [ ] **Step 2: Add failing memory-pressure and malformed-input tests**

Generate enough 1 MiB records to force `--memory-limit 64M` backpressure without exceeding it in one record. Assert completion, equality, and `peak_tracked_bytes <= 60397978` (90% of 64 MiB). Add malformed INFO, missing GT, invalid allele index, unreadable input, unwritable output directory, and annotation-too-large cases; each must exit 1 or 2 as specified and name the path/line in stderr.

- [ ] **Step 3: Run and observe specific failures**

```bash
cmake --build build
ctest --test-dir build --output-on-failure -R "differential-formats|integration-memory|integration-errors"
```

Expected: failures identify any incomplete format detection, accounting, or diagnostic paths.

- [ ] **Step 4: Make the minimum integration corrections**

Limit corrections to behaviors exposed by these tests: preserve final LF, pass the requested final format to a temporary filename without relying on the temporary extension, update tracked-memory counters on every permit change, include physical line numbers in conversion errors, and check explicit HTSlib flush/close return values before transaction commit.

- [ ] **Step 5: Run the entire suite repeatedly**

```bash
cmake --build build
ctest --test-dir build --output-on-failure
ctest --test-dir build --output-on-failure -R "pipeline|differential|integration" --repeat until-fail:20
```

Expected: full pass and 20 deterministic repetitions.

- [ ] **Step 6: Commit integration hardening**

```bash
git add tests/differential_test.py tests/integration_test.py tests/CMakeLists.txt src/main.cpp src/pipeline.cpp
git commit -m "test: verify formats memory and failure behavior"
```

---

### Task 12: Add Benchmarks, Build Documentation, and CI

**Files:**
- Create: `benchmarks/generate_fixture.py`
- Create: `benchmarks/run_benchmark.py`
- Create: `docs/build-linux.md`
- Create: `docs/build-windows.md`
- Create: `.github/workflows/ci.yml`
- Create: `README.md`
- Modify: `CMakeLists.txt`

**Interfaces:**
- Consumes: the finished executable and supported dependency environments.
- Produces: reproducible benchmark CSV, documented Linux/Windows builds, and automated cross-platform verification.

- [ ] **Step 1: Write the benchmark generator and runner**

`generate_fixture.py` accepts `--records`, `--samples`, `--seed`, `--annotation`, and `--input`; it uses only Python's standard library and writes deterministic valid fixtures. `run_benchmark.py` accepts the executable and fixtures, runs thread counts `1,2,4,8,16`, verifies every decompressed output hash is identical, and prints CSV columns:

```text
threads,seconds,input_records_per_second,output_bytes_per_second,peak_tracked_mib,sha256
```

- [ ] **Step 2: Document exact Linux commands**

```bash
sudo apt-get update
sudo apt-get install -y build-essential cmake ninja-build pkg-config libhts-dev python3 git
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build
ctest --test-dir build --output-on-failure
```

Document the `pkg-config --modversion htslib` check and require 1.17 or newer.

- [ ] **Step 3: Document exact Windows MSYS2 UCRT64 commands**

```bash
pacman -Syu
pacman -S --needed mingw-w64-ucrt-x86_64-gcc mingw-w64-ucrt-x86_64-cmake \
  mingw-w64-ucrt-x86_64-ninja mingw-w64-ucrt-x86_64-htslib \
  mingw-w64-ucrt-x86_64-python git
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build
ctest --test-dir build --output-on-failure
```

State that commands must run from the MSYS2 UCRT64 shell, not ordinary PowerShell.

- [ ] **Step 4: Add the CI matrix**

Use `ubuntu-latest` to install `libhts-dev`, configure, build, and test. Use `windows-latest` with `msys2/setup-msys2`, `msystem: UCRT64`, `update: true`, and install the UCRT64 GCC/CMake/Ninja/HTSlib/Python packages. Run the same CMake and CTest commands in both jobs. Upload `Testing/Temporary/LastTest.log` only on failure.

- [ ] **Step 5: Run release verification and a local benchmark**

```bash
cmake -S . -B build-release -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build-release
ctest --test-dir build-release --output-on-failure
python benchmarks/generate_fixture.py --records 100000 --samples 100 --seed 7 --annotation benchmark-annotation.vcf.gz --input benchmark-input.vcf.gz
python benchmarks/run_benchmark.py build-release/convert-to-biallelic benchmark-annotation.vcf.gz benchmark-input.vcf.gz
```

Expected: all tests pass, all SHA-256 values match, and the CSV records measured scaling without asserting an artificial speedup threshold.

- [ ] **Step 6: Commit documentation and automation**

```bash
git add README.md CMakeLists.txt .github/workflows/ci.yml docs/build-linux.md docs/build-windows.md benchmarks/generate_fixture.py benchmarks/run_benchmark.py
git commit -m "docs: add cross-platform build and benchmark workflow"
```

---

## Final Verification Gate

- [ ] Configure and build a clean Debug tree.
- [ ] Run the complete CTest suite with `--output-on-failure`.
- [ ] Run pipeline/differential/integration tests 20 times.
- [ ] Configure and build a clean Release tree.
- [ ] Run the 100,000-record benchmark at 1, 2, 4, 8, and 16 threads.
- [ ] Confirm all decompressed hashes match the Python oracle.
- [ ] Confirm progress is stdout-only and diagnostics are stderr-only.
- [ ] Confirm the output directory contains no abandoned temporary files.
- [ ] Confirm `git status --short` contains only intentional files.
- [ ] Record HTSlib, compiler, CMake, OS, elapsed-time, and peak-memory versions/results in the final handoff.
````````

## `include/convert_to_biallelic/annotation_index.hpp`

````````text
#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <string>
#include <string_view>
#include <unordered_map>

#include "convert_to_biallelic/vcf_io.hpp"

namespace ctb {

struct VariantDefinition {
    std::int64_t position = 0;
    std::string ref;
    std::string alt;
};

class AnnotationIndex {
public:
    AnnotationIndex() = default;
    AnnotationIndex(const AnnotationIndex&) = delete;
    AnnotationIndex& operator=(const AnnotationIndex&) = delete;
    AnnotationIndex(AnnotationIndex&&) = default;
    AnnotationIndex& operator=(AnnotationIndex&&) = delete;

    const VariantDefinition* find(std::string_view chromosome,
                                  std::string_view id) const noexcept;
    const VariantDefinition* find_checked(std::string_view chromosome,
                                          std::string_view id) const;
    std::uint64_t estimated_bytes() const noexcept;
    std::uint64_t variant_count() const noexcept;

private:
    struct StringViewHash {
        std::size_t operator()(std::string_view value) const noexcept {
            return std::hash<std::string_view>{}(value);
        }
    };

    struct StringViewEqual {
        bool operator()(std::string_view left,
                        std::string_view right) const noexcept {
            return left == right;
        }
    };

    using ById = std::unordered_map<std::string, VariantDefinition>;
    using LookupById = std::unordered_map<
        std::string_view, const VariantDefinition*, StringViewHash,
        StringViewEqual>;
    using LookupByChromosome = std::unordered_map<
        std::string_view, LookupById, StringViewHash, StringViewEqual>;

    void build_lookup(std::uint64_t allowance);

    // Owned node containers are populated completely before lookup_ is built.
    // The view keys and definition pointers remain valid because there is no
    // later mutation, copy is disabled, and default-allocator unordered_map
    // move construction preserves references to transferred nodes. Member
    // order also destroys lookup_ before the owned nodes it references.
    std::unordered_map<std::string, ById> variants_;
    LookupByChromosome lookup_;
    std::uint64_t estimated_bytes_ = 0;
    std::uint64_t variant_count_ = 0;

    friend AnnotationIndex load_annotation(InputSource&, std::uint64_t);
};

AnnotationIndex load_annotation(InputSource&, std::uint64_t memory_limit);

}  // namespace ctb
````````

## `include/convert_to_biallelic/bounded_queue.hpp`

````````text
#pragma once

#include <condition_variable>
#include <cstddef>
#include <deque>
#include <mutex>
#include <optional>
#include <stdexcept>
#include <type_traits>
#include <utility>

namespace ctb {

// Lock ordering: callers must acquire or resize memory permits outside queue
// operations. The queue never calls a memory budget while holding its mutex.
// Cancellation moves queued values out before destroying them, and pop assigns
// to the caller's output only after unlocking, so permit releases do not nest a
// memory-budget lock inside the queue lock. Element move construction and the
// destruction of a moved-from element must likewise not acquire/resize memory.
// As with other synchronization containers, all users must stop before the
// queue itself is destroyed. The nothrow element constraints make queue-lock
// transitions indivisible with respect to element moves and destruction; they
// do not replace the rule that those operations must avoid memory-budget locks.
template <class T>
class BoundedQueue {
    static_assert(std::is_nothrow_move_constructible<T>::value,
                  "BoundedQueue<T> requires nothrow move construction");
    static_assert(std::is_nothrow_move_assignable<T>::value,
                  "BoundedQueue<T> requires nothrow move assignment");
    static_assert(std::is_nothrow_destructible<T>::value,
                  "BoundedQueue<T> requires nothrow destruction");

public:
    explicit BoundedQueue(std::size_t capacity) : capacity_(capacity) {
        if (capacity == 0) {
            throw std::invalid_argument(
                "Bounded queue capacity must be positive");
        }
    }

    ~BoundedQueue() noexcept {
        cancel();
    }

    BoundedQueue(const BoundedQueue&) = delete;
    BoundedQueue& operator=(const BoundedQueue&) = delete;
    BoundedQueue(BoundedQueue&&) = delete;
    BoundedQueue& operator=(BoundedQueue&&) = delete;

    void push(T item) {
        std::unique_lock<std::mutex> lock(mutex_);
        not_full_.wait(lock, [this] {
            return cancelled_ || closed_ || queue_.size() < capacity_;
        });

        if (cancelled_) {
            throw std::runtime_error("Bounded queue is cancelled");
        }
        if (closed_) {
            throw std::runtime_error("Bounded queue is closed");
        }

        try {
            queue_.push_back(std::move(item));
        } catch (...) {
            // This producer consumed a capacity wake but did not consume the
            // slot. Restore one wake after dropping the queue mutex.
            lock.unlock();
            not_full_.notify_one();
            throw;
        }

        lock.unlock();
        not_empty_.notify_one();
    }

    bool pop(T& out) {
        std::optional<T> item;
        {
            std::unique_lock<std::mutex> lock(mutex_);
            not_empty_.wait(lock, [this] {
                return cancelled_ || closed_ || !queue_.empty();
            });

            if (cancelled_) {
                throw std::runtime_error("Bounded queue is cancelled");
            }
            if (queue_.empty()) {
                return false;
            }

            item.emplace(std::move(queue_.front()));
            queue_.pop_front();
        }

        // Nothrow assignment stays outside the queue lock so replacing an
        // existing permit in out cannot nest a memory-budget release.
        not_full_.notify_one();
        out = std::move(*item);
        return true;
    }

    void close() {
        {
            std::lock_guard<std::mutex> lock(mutex_);
            if (closed_) {
                return;
            }
            closed_ = true;
        }
        not_empty_.notify_all();
        not_full_.notify_all();
    }

    void cancel() noexcept {
        {
            std::lock_guard<std::mutex> lock(mutex_);
            if (cancelled_) {
                return;
            }
            cancelled_ = true;
            queue_.swap(discarded_);
        }

        // Publish cancellation promptly. Permit destruction may contend on the
        // memory-budget mutex, so it happens only after queue waiters are awake.
        not_empty_.notify_all();
        not_full_.notify_all();
        discarded_.clear();
    }

private:
    const std::size_t capacity_;
    std::mutex mutex_;
    std::condition_variable not_empty_;
    std::condition_variable not_full_;
    std::deque<T> queue_;
    // Construct this spare buffer with the queue so cancellation can empty the
    // live FIFO by noexcept swap without allocating while it is unwinding.
    std::deque<T> discarded_;
    bool closed_ = false;
    bool cancelled_ = false;
};

}  // namespace ctb
````````

## `include/convert_to_biallelic/cli.hpp`

````````text
#pragma once

#include "types.hpp"

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <filesystem>

namespace ctb {

struct Config {
    std::filesystem::path variants;
    std::filesystem::path input;
    std::filesystem::path output;
    std::size_t threads = 1;
    std::uint64_t memory_limit_bytes = 2ULL * 1024ULL * 1024ULL * 1024ULL;
    std::chrono::milliseconds progress_interval{5000};
    bool quiet = false;
    bool force = false;
    OutputFormat output_format = OutputFormat::vcf;
};

class UsageRequested final : public std::exception {
public:
    explicit UsageRequested(bool version) noexcept : version_(version) {}

    bool version() const noexcept { return version_; }
    const char* what() const noexcept override { return "usage requested"; }

private:
    bool version_;
};

Config parse_cli(int argc, char** argv);
ThreadAllocation allocate_threads(const Config& config, bool compressed_input,
                                  bool compressed_output);
const char* usage_text();

}  // namespace ctb
````````

## `include/convert_to_biallelic/converter.hpp`

````````text
#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <string_view>

#include "convert_to_biallelic/annotation_index.hpp"

namespace ctb {

struct ConversionResult {
    std::string bytes;
    std::uint64_t output_records = 0;
};

std::optional<std::string> convert_header(std::string_view line);

void append_converted_record(
    std::string_view line,
    const AnnotationIndex& annotation,
    std::uint64_t line_number,
    std::string& destination,
    std::uint64_t& output_records,
    const std::function<void(std::size_t planned_output_bytes,
                             std::size_t planned_scratch_bytes)>&
        before_reserve);

ConversionResult convert_record(std::string_view line,
                                const AnnotationIndex& annotation,
                                std::uint64_t line_number);

}  // namespace ctb
````````

## `include/convert_to_biallelic/memory_budget.hpp`

````````text
#pragma once

#include <condition_variable>
#include <cstdint>
#include <mutex>

namespace ctb {

class MemoryBudget;

// A nonempty permit is bound to its originating budget. That budget must
// outlive the permit, and one permit object must not be accessed concurrently.
class MemoryPermit {
public:
    MemoryPermit() = default;
    ~MemoryPermit() noexcept;

    MemoryPermit(MemoryPermit&& other) noexcept;
    MemoryPermit& operator=(MemoryPermit&& other) noexcept;

    MemoryPermit(const MemoryPermit&) = delete;
    MemoryPermit& operator=(const MemoryPermit&) = delete;

    std::uint64_t bytes() const noexcept;
    void resize(std::uint64_t bytes, bool allow_one_overage);

private:
    MemoryPermit(MemoryBudget* owner,
                 std::uint64_t bytes,
                 bool overage) noexcept;

    void release() noexcept;

    MemoryBudget* owner_ = nullptr;
    std::uint64_t bytes_ = 0;
    bool overage_ = false;

    friend class MemoryBudget;
};

class MemoryBudget {
public:
    explicit MemoryBudget(std::uint64_t tracked_limit);

    MemoryPermit acquire(std::uint64_t bytes, bool allow_one_overage);
    void cancel();

    std::uint64_t current_bytes() const;
    std::uint64_t peak_bytes() const;

private:
    bool fits_normal_locked(std::uint64_t additional) const noexcept;
    void add_locked(std::uint64_t bytes) noexcept;
    void subtract_locked(std::uint64_t bytes) noexcept;
    void resize(MemoryPermit& permit,
                std::uint64_t bytes,
                bool allow_one_overage);
    void release(std::uint64_t bytes, bool overage) noexcept;

    mutable std::mutex mutex_;
    std::condition_variable changed_;
    std::uint64_t limit_;

    // During the one permitted overage, the exact tracked sum can require 65
    // bits even though every reservation is uint64_t. current_high_ represents
    // the 2^64 bit and current_ stores the low 64 bits. Public reports saturate.
    std::uint64_t current_ = 0;
    bool current_high_ = false;
    std::uint64_t peak_ = 0;

    bool cancelled_ = false;
    bool overage_active_ = false;

    friend class MemoryPermit;
};

}  // namespace ctb
````````

## `include/convert_to_biallelic/output_transaction.hpp`

````````text
#pragma once

#include <filesystem>
#include <memory>

namespace ctb {

class OutputTransaction {
public:
    OutputTransaction(std::filesystem::path destination, bool force);
    ~OutputTransaction() noexcept;

    OutputTransaction(const OutputTransaction&) = delete;
    OutputTransaction& operator=(const OutputTransaction&) = delete;
    OutputTransaction(OutputTransaction&&) = delete;
    OutputTransaction& operator=(OutputTransaction&&) = delete;

    const std::filesystem::path& temporary_path() const noexcept;
    int take_sink_fd();
    void commit();

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

}  // namespace ctb
````````

## `include/convert_to_biallelic/pipeline.hpp`

````````text
#pragma once

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <iosfwd>
#include <type_traits>

#include "convert_to_biallelic/annotation_index.hpp"
#include "convert_to_biallelic/memory_budget.hpp"
#include "convert_to_biallelic/types.hpp"
#include "convert_to_biallelic/vcf_io.hpp"

namespace ctb {

struct WorkChunk {
    RawWorkChunk data;
    MemoryPermit permit;
};

struct ResultChunk {
    RawResultChunk data;
    MemoryPermit permit;
};

static_assert(std::is_nothrow_move_constructible<WorkChunk>::value,
              "WorkChunk must be nothrow move constructible");
static_assert(std::is_nothrow_move_assignable<WorkChunk>::value,
              "WorkChunk must be nothrow move assignable");
static_assert(std::is_nothrow_destructible<WorkChunk>::value,
              "WorkChunk must be nothrow destructible");
static_assert(std::is_nothrow_move_constructible<ResultChunk>::value,
              "ResultChunk must be nothrow move constructible");
static_assert(std::is_nothrow_move_assignable<ResultChunk>::value,
              "ResultChunk must be nothrow move assignable");
static_assert(std::is_nothrow_destructible<ResultChunk>::value,
              "ResultChunk must be nothrow destructible");

struct PipelineOptions {
    ThreadAllocation threads;
    std::uint64_t memory_limit_bytes =
        2ULL * 1024ULL * 1024ULL * 1024ULL;
    std::size_t target_records_per_chunk = 512;
    std::uint64_t target_bytes_per_chunk =
        8ULL * 1024ULL * 1024ULL;
    std::chrono::milliseconds progress_interval{5000};
    bool quiet = false;
};

PipelineStats run_pipeline(InputSource& input,
                           OutputSink& output,
                           const AnnotationIndex& annotation,
                           const PipelineOptions& options,
                           std::ostream& progress,
                           std::ostream& diagnostics);

}  // namespace ctb
````````

## `include/convert_to_biallelic/progress.hpp`

````````text
#pragma once

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <exception>
#include <functional>
#include <iosfwd>
#include <mutex>
#include <string>
#include <thread>

namespace ctb {

struct PipelineStats;

struct ProgressCounters {
    std::atomic<std::uint64_t> input_records{0};
    std::atomic<std::uint64_t> output_records{0};
    std::atomic<std::uint64_t> tracked_memory{0};
};

class ProgressReporter {
public:
    ProgressReporter(ProgressCounters& counters,
                     std::chrono::milliseconds interval,
                     std::ostream& output,
                     bool quiet,
                     std::function<void(std::exception_ptr)> error_callback =
                         {});
    ~ProgressReporter();

    ProgressReporter(const ProgressReporter&) = delete;
    ProgressReporter& operator=(const ProgressReporter&) = delete;
    ProgressReporter(ProgressReporter&&) = delete;
    ProgressReporter& operator=(ProgressReporter&&) = delete;

    void start();
    std::chrono::steady_clock::duration finish();
    void stop_without_summary() noexcept;

private:
    enum class State { idle, running, finishing, stopped, finished };

    void record_error(std::exception_ptr error) noexcept;
    void join_thread_safely() noexcept;
    void run() noexcept;

    ProgressCounters& counters_;
    std::chrono::milliseconds interval_;
    std::ostream& output_;
    bool quiet_;
    std::function<void(std::exception_ptr)> error_callback_;

    std::mutex mutex_;
    std::condition_variable changed_;
    std::thread thread_;
    std::chrono::steady_clock::time_point started_{};
    std::exception_ptr thread_error_;
    State state_ = State::idle;
    bool stopping_ = false;
    bool thread_exited_ = true;
};

std::string format_final_summary(const PipelineStats& stats);

}  // namespace ctb
````````

## `include/convert_to_biallelic/types.hpp`

````````text
#pragma once

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace ctb {

enum class OutputFormat { vcf, vcf_gz };

struct ThreadAllocation {
    std::size_t conversion_workers = 1;
    std::size_t input_io_workers = 0;
    std::size_t output_io_workers = 0;
};

struct PipelineStats {
    std::uint64_t input_records = 0;
    std::uint64_t output_records = 0;
    std::uint64_t output_bytes = 0;
    std::uint64_t peak_tracked_bytes = 0;
    std::chrono::steady_clock::duration elapsed{};
};

struct RawWorkChunk {
    std::uint64_t sequence = 0;
    std::uint64_t first_line_number = 0;
    std::vector<std::string> records;
};

struct RawResultChunk {
    std::uint64_t sequence = 0;
    std::string bytes;
    std::uint64_t input_records = 0;
    std::uint64_t output_records = 0;
};

}  // namespace ctb
````````

## `include/convert_to_biallelic/vcf_io.hpp`

````````text
#pragma once

#include <cstddef>
#include <filesystem>
#include <memory>
#include <string>
#include <string_view>

#include "convert_to_biallelic/types.hpp"

namespace ctb {

class InputSource {
public:
    InputSource(const std::filesystem::path& path, std::size_t io_workers);
    ~InputSource();

    InputSource(InputSource&&) noexcept;
    InputSource& operator=(InputSource&&) noexcept;

    bool getline(std::string& line);
    bool compressed() const noexcept;
    void close();

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

class OutputSink {
public:
    OutputSink(const std::filesystem::path& path,
               OutputFormat format,
               std::size_t io_workers);
    OutputSink(int owned_fd,
               const std::filesystem::path& display_path,
               OutputFormat format,
               std::size_t io_workers);
    ~OutputSink();

    OutputSink(OutputSink&&) noexcept;
    OutputSink& operator=(OutputSink&&) noexcept;

    void write(std::string_view bytes);
    void flush();
    void close();

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

}  // namespace ctb
````````

## `src/annotation_index.cpp`

````````text
#include "convert_to_biallelic/annotation_index.hpp"

#include <charconv>
#include <cstddef>
#include <limits>
#include <stdexcept>
#include <string>
#include <string_view>
#include <system_error>
#include <unordered_map>
#include <utility>
#include <vector>

namespace ctb {
namespace {

constexpr std::uint64_t kMinimumMemoryLimit =
    64ULL * 1024ULL * 1024ULL;
constexpr std::uint64_t kHashNodeOverhead =
    3ULL * sizeof(void*) + sizeof(std::size_t);
constexpr std::uint64_t kLookupBucketRoundingSafetyFactor = 2;

std::uint64_t saturating_add(std::uint64_t total,
                             std::uint64_t additional) noexcept {
    const auto maximum = std::numeric_limits<std::uint64_t>::max();
    return additional > maximum - total ? maximum : total + additional;
}

std::uint64_t to_uint64(std::size_t value) noexcept {
    const auto maximum = std::numeric_limits<std::uint64_t>::max();
    return value > maximum ? maximum : static_cast<std::uint64_t>(value);
}

std::uint64_t saturating_multiply(std::uint64_t left,
                                  std::uint64_t right) noexcept {
    const auto maximum = std::numeric_limits<std::uint64_t>::max();
    if (left == 0 || right == 0) {
        return 0;
    }
    return left > maximum / right ? maximum : left * right;
}

std::uint64_t conservative_lookup_bucket_count(
    std::size_t entries) noexcept {
    if (entries == 0) {
        return 0;
    }
    // reserve(n) with the default load factor needs at least n buckets, but
    // implementations round to their bucket policy. Two times n plus one is a
    // deliberate preflight safety factor; the actual post-build count is still
    // measured and checked below.
    return saturating_add(
        saturating_multiply(to_uint64(entries),
                            kLookupBucketRoundingSafetyFactor),
        1);
}

void add_string_storage(std::uint64_t& total, const std::string& value) noexcept {
    // Capacity includes the characters but not the terminating NUL.
    total = saturating_add(total, to_uint64(value.capacity()));
    total = saturating_add(total, 1);
}

using OwnedById =
    std::unordered_map<std::string, VariantDefinition>;
using OwnedByChromosome =
    std::unordered_map<std::string, OwnedById>;

std::uint64_t bucket_storage_bytes(std::size_t count) noexcept {
    return saturating_multiply(to_uint64(count), sizeof(void*));
}

void replace_estimate_component(std::uint64_t& total,
                                std::uint64_t previous,
                                std::uint64_t replacement) noexcept {
    total = previous > total ? 0 : total - previous;
    total = saturating_add(total, replacement);
}

std::uint64_t owned_chromosome_node_bytes(
    const OwnedByChromosome::value_type& entry) noexcept {
    std::uint64_t bytes = sizeof(OwnedByChromosome::value_type);
    bytes = saturating_add(bytes, kHashNodeOverhead);
    add_string_storage(bytes, entry.first);
    bytes = saturating_add(
        bytes, bucket_storage_bytes(entry.second.bucket_count()));
    return bytes;
}

std::uint64_t owned_variant_node_bytes(
    const OwnedById::value_type& entry) noexcept {
    std::uint64_t bytes = sizeof(OwnedById::value_type);
    bytes = saturating_add(bytes, kHashNodeOverhead);
    add_string_storage(bytes, entry.first);
    add_string_storage(bytes, entry.second.ref);
    add_string_storage(bytes, entry.second.alt);
    return bytes;
}

std::uint64_t owned_variant_value_string_bytes(
    const VariantDefinition& definition) noexcept {
    std::uint64_t bytes = 0;
    add_string_storage(bytes, definition.ref);
    add_string_storage(bytes, definition.alt);
    return bytes;
}

template <typename OuterMap, typename LookupMap>
std::uint64_t estimate_index_bytes(const OuterMap& variants,
                                   const LookupMap& lookup) noexcept {
    // This is deliberately an allocation estimate, not an exact RSS measurement.
    std::uint64_t total = sizeof(AnnotationIndex);
    total = saturating_add(
        total,
        saturating_multiply(to_uint64(variants.bucket_count()), sizeof(void*)));

    for (const auto& chromosome_entry : variants) {
        total = saturating_add(
            total, to_uint64(sizeof(decltype(chromosome_entry))));
        total = saturating_add(total, kHashNodeOverhead);
        add_string_storage(total, chromosome_entry.first);

        const auto& by_id = chromosome_entry.second;
        total = saturating_add(
            total,
            saturating_multiply(to_uint64(by_id.bucket_count()), sizeof(void*)));
        for (const auto& variant_entry : by_id) {
            total = saturating_add(
                total, to_uint64(sizeof(decltype(variant_entry))));
            total = saturating_add(total, kHashNodeOverhead);
            add_string_storage(total, variant_entry.first);
            add_string_storage(total, variant_entry.second.ref);
            add_string_storage(total, variant_entry.second.alt);
        }
    }

    total = saturating_add(
        total,
        saturating_multiply(to_uint64(lookup.bucket_count()),
                            sizeof(void*)));
    for (const auto& chromosome_entry : lookup) {
        total = saturating_add(
            total, to_uint64(sizeof(decltype(chromosome_entry))));
        total = saturating_add(total, kHashNodeOverhead);

        const auto& by_id = chromosome_entry.second;
        total = saturating_add(
            total,
            saturating_multiply(to_uint64(by_id.bucket_count()),
                                sizeof(void*)));
        for (const auto& variant_entry : by_id) {
            total = saturating_add(
                total, to_uint64(sizeof(decltype(variant_entry))));
            total = saturating_add(total, kHashNodeOverhead);
        }
    }
    return total;
}

std::vector<std::string_view> split_tab_fields(std::string_view line) {
    std::vector<std::string_view> fields;
    std::size_t begin = 0;
    while (begin <= line.size()) {
        const std::size_t end = line.find('\t', begin);
        if (end == std::string_view::npos) {
            fields.push_back(line.substr(begin));
            break;
        }
        fields.push_back(line.substr(begin, end - begin));
        begin = end + 1;
    }
    return fields;
}

std::int64_t parse_positive_position(std::string_view text) {
    if (text.empty()) {
        throw std::invalid_argument(
            "POS must be a fully consumed positive int64");
    }
    std::int64_t position = 0;
    const char* const begin = text.data();
    const char* const end = begin + text.size();
    const auto result = std::from_chars(begin, end, position);
    if (result.ec != std::errc{} || result.ptr != end || position <= 0) {
        throw std::invalid_argument(
            "POS must be a fully consumed positive int64");
    }
    return position;
}

std::string_view extract_identifier(std::string_view info) {
    std::string_view identifier;
    std::size_t identifier_entries = 0;
    std::size_t begin = 0;
    while (begin <= info.size()) {
        const std::size_t end = info.find(';', begin);
        const std::string_view entry =
            end == std::string_view::npos ? info.substr(begin)
                                          : info.substr(begin, end - begin);
        if (entry.size() >= 3 && entry.substr(0, 3) == "ID=") {
            if (identifier_entries != 0) {
                throw std::invalid_argument(
                    "INFO must contain exactly one ID= entry");
            }
            identifier_entries = 1;
            identifier = entry.substr(3);
        }
        if (end == std::string_view::npos) {
            break;
        }
        begin = end + 1;
    }

    if (identifier_entries != 1) {
        throw std::invalid_argument("INFO must contain exactly one ID= entry");
    }
    if (identifier.empty()) {
        throw std::invalid_argument("INFO ID= value must be nonempty");
    }
    if (identifier.find(',') != std::string_view::npos) {
        throw std::invalid_argument("INFO ID= value must not contain a comma");
    }
    return identifier;
}

std::runtime_error annotation_line_error(std::uint64_t line_number,
                                         std::string_view reason) {
    return std::runtime_error("Annotation line " +
                              std::to_string(line_number) + ": " +
                              std::string(reason));
}

}  // namespace

void AnnotationIndex::build_lookup(std::uint64_t allowance) {
    if (!lookup_.empty()) {
        throw std::logic_error(
            "Annotation view lookup was already constructed");
    }

    std::uint64_t preflight = estimate_index_bytes(variants_, lookup_);
    const std::uint64_t outer_entries = to_uint64(variants_.size());
    const std::uint64_t outer_node_bytes = saturating_add(
        to_uint64(sizeof(LookupByChromosome::value_type)),
        kHashNodeOverhead);
    preflight = saturating_add(
        preflight,
        saturating_multiply(
            conservative_lookup_bucket_count(variants_.size()),
            sizeof(void*)));
    preflight = saturating_add(
        preflight,
        saturating_multiply(outer_entries, outer_node_bytes));

    const std::uint64_t inner_node_bytes = saturating_add(
        to_uint64(sizeof(LookupById::value_type)), kHashNodeOverhead);
    for (const auto& chromosome_entry : variants_) {
        preflight = saturating_add(
            preflight,
            saturating_multiply(
                conservative_lookup_bucket_count(
                    chromosome_entry.second.size()),
                sizeof(void*)));
        preflight = saturating_add(
            preflight,
            saturating_multiply(
                to_uint64(chromosome_entry.second.size()),
                inner_node_bytes));
    }
    if (preflight > allowance) {
        throw std::invalid_argument(
            "estimated annotation index memory preflight exceeds the 90% allowance before view lookup construction");
    }

    LookupByChromosome lookup;
    lookup.reserve(variants_.size());

    for (const auto& chromosome_entry : variants_) {
        LookupById by_id_lookup;
        by_id_lookup.reserve(chromosome_entry.second.size());
        for (const auto& variant_entry : chromosome_entry.second) {
            const auto insertion = by_id_lookup.emplace(
                std::string_view(variant_entry.first),
                &variant_entry.second);
            if (!insertion.second) {
                throw std::logic_error(
                    "Duplicate ID while building annotation view lookup");
            }
        }

        const auto insertion = lookup.emplace(
            std::string_view(chromosome_entry.first),
            std::move(by_id_lookup));
        if (!insertion.second) {
            throw std::logic_error(
                "Duplicate chromosome while building annotation view lookup");
        }
    }

    lookup_ = std::move(lookup);
    const std::uint64_t completed_estimate =
        estimate_index_bytes(variants_, lookup_);
    if (completed_estimate > allowance) {
        throw std::invalid_argument(
            "estimated annotation index memory exceeds the 90% allowance after view lookup construction");
    }
    estimated_bytes_ = completed_estimate;
}

const VariantDefinition* AnnotationIndex::find_checked(
    std::string_view chromosome, std::string_view id) const {
    const auto chromosome_it = lookup_.find(chromosome);
    if (chromosome_it == lookup_.end()) {
        return nullptr;
    }

    const auto id_it = chromosome_it->second.find(id);
    return id_it == chromosome_it->second.end() ? nullptr : id_it->second;
}

const VariantDefinition* AnnotationIndex::find(
    std::string_view chromosome, std::string_view id) const noexcept {
    try {
        return find_checked(chromosome, id);
    } catch (...) {
        return nullptr;
    }
}

std::uint64_t AnnotationIndex::estimated_bytes() const noexcept {
    return estimated_bytes_;
}

std::uint64_t AnnotationIndex::variant_count() const noexcept {
    return variant_count_;
}

AnnotationIndex load_annotation(InputSource& input, std::uint64_t memory_limit) {
    if (memory_limit < kMinimumMemoryLimit) {
        throw std::runtime_error(
            "Annotation memory limit must be at least 64 MiB");
    }

    const std::uint64_t allowance = memory_limit - memory_limit / 10;
    AnnotationIndex index;
    index.estimated_bytes_ = estimate_index_bytes(
        index.variants_, index.lookup_);
    std::string line;
    std::uint64_t line_number = 0;

    for (;;) {
        const std::uint64_t next_line_number =
            line_number == std::numeric_limits<std::uint64_t>::max()
                ? line_number
                : line_number + 1;
        try {
            if (!input.getline(line)) {
                break;
            }
            if (line_number ==
                std::numeric_limits<std::uint64_t>::max()) {
                throw std::overflow_error("physical line number overflow");
            }
            line_number = next_line_number;
            if (!line.empty() && line.front() == '#') {
                continue;
            }

            const auto fields = split_tab_fields(line);
            if (fields.size() < 8) {
                throw std::invalid_argument("expected at least 8 tab-separated fields");
            }

            const std::int64_t position = parse_positive_position(fields[1]);
            const std::string_view identifier = extract_identifier(fields[7]);
            const std::size_t old_outer_buckets =
                index.variants_.bucket_count();
            const auto chromosome_insertion = index.variants_.try_emplace(
                std::string(fields[0]));
            auto& chromosome_entry = *chromosome_insertion.first;
            if (chromosome_insertion.second) {
                replace_estimate_component(
                    index.estimated_bytes_,
                    bucket_storage_bytes(old_outer_buckets),
                    bucket_storage_bytes(index.variants_.bucket_count()));
                index.estimated_bytes_ = saturating_add(
                    index.estimated_bytes_,
                    owned_chromosome_node_bytes(chromosome_entry));
            }

            auto& by_id = chromosome_entry.second;
            std::string identifier_key(identifier);
            const auto existing = by_id.find(identifier_key);
            const bool replacing = existing != by_id.end();
            const std::uint64_t previous_value_strings =
                replacing
                    ? owned_variant_value_string_bytes(existing->second)
                    : 0;
            const std::size_t old_inner_buckets = by_id.bucket_count();
            const auto variant_insertion = by_id.insert_or_assign(
                std::move(identifier_key),
                VariantDefinition{position, std::string(fields[3]),
                                  std::string(fields[4])});

            replace_estimate_component(
                index.estimated_bytes_,
                bucket_storage_bytes(old_inner_buckets),
                bucket_storage_bytes(by_id.bucket_count()));
            if (variant_insertion.second) {
                if (index.variant_count_ ==
                    std::numeric_limits<std::uint64_t>::max()) {
                    throw std::overflow_error(
                        "annotation variant count overflow");
                }
                ++index.variant_count_;
                index.estimated_bytes_ = saturating_add(
                    index.estimated_bytes_,
                    owned_variant_node_bytes(*variant_insertion.first));
            } else {
                replace_estimate_component(
                    index.estimated_bytes_, previous_value_strings,
                    owned_variant_value_string_bytes(
                        variant_insertion.first->second));
            }
            if (index.estimated_bytes_ > allowance) {
                throw std::invalid_argument(
                    "estimated annotation index memory exceeds the 90% allowance");
            }
        } catch (const std::exception& error) {
            throw annotation_line_error(next_line_number, error.what());
        }
    }

    try {
        index.build_lookup(allowance);
    } catch (const std::exception& error) {
        throw annotation_line_error(line_number, error.what());
    }
    return index;
}

}  // namespace ctb
````````

## `src/cli.cpp`

````````text
#include "convert_to_biallelic/cli.hpp"

#include <cctype>
#include <limits>
#include <stdexcept>
#include <string>
#include <thread>
#include <unordered_set>

namespace ctb {
namespace {

constexpr std::uint64_t kMiB = 1024ULL * 1024ULL;
constexpr std::uint64_t kMinimumMemoryBytes = 64ULL * kMiB;

std::string lower_ascii(std::string value) {
    for (char& character : value) {
        character = static_cast<char>(
            std::tolower(static_cast<unsigned char>(character)));
    }
    return value;
}

bool ends_with(const std::string& value, const std::string& suffix) {
    return value.size() >= suffix.size() &&
           value.compare(value.size() - suffix.size(), suffix.size(), suffix) == 0;
}

std::uint64_t parse_decimal(const std::string& value, const char* option) {
    if (value.empty()) {
        throw std::invalid_argument(std::string("missing value for ") + option);
    }

    std::uint64_t parsed = 0;
    for (const char character : value) {
        if (character < '0' || character > '9') {
            throw std::invalid_argument(std::string("invalid numeric value for ") + option);
        }
        const std::uint64_t digit = static_cast<std::uint64_t>(character - '0');
        if (parsed > (std::numeric_limits<std::uint64_t>::max() - digit) / 10ULL) {
            throw std::invalid_argument(std::string("numeric value overflows for ") + option);
        }
        parsed = parsed * 10ULL + digit;
    }
    return parsed;
}

std::size_t parse_threads(const std::string& value) {
    const std::uint64_t parsed = parse_decimal(value, "--threads");
    if (parsed == 0) {
        throw std::invalid_argument("--threads must be greater than zero");
    }
    if (parsed > static_cast<std::uint64_t>(std::numeric_limits<std::size_t>::max())) {
        throw std::invalid_argument("--threads exceeds the supported range");
    }
    return static_cast<std::size_t>(parsed);
}

std::uint64_t parse_memory_limit(const std::string& value) {
    if (value.empty()) {
        throw std::invalid_argument("missing value for --memory-limit");
    }

    std::string number = value;
    std::uint64_t multiplier = 1;
    const char final_character = value.back();
    switch (static_cast<char>(std::tolower(static_cast<unsigned char>(final_character)))) {
        case 'k':
            multiplier = 1024ULL;
            number.pop_back();
            break;
        case 'm':
            multiplier = kMiB;
            number.pop_back();
            break;
        case 'g':
            multiplier = kMiB * 1024ULL;
            number.pop_back();
            break;
        default:
            break;
    }

    const std::uint64_t parsed = parse_decimal(number, "--memory-limit");
    if (parsed > std::numeric_limits<std::uint64_t>::max() / multiplier) {
        throw std::invalid_argument("--memory-limit overflows bytes");
    }
    const std::uint64_t bytes = parsed * multiplier;
    if (bytes < kMinimumMemoryBytes) {
        throw std::invalid_argument("--memory-limit must be at least 64 MiB");
    }
    return bytes;
}

std::chrono::milliseconds parse_progress_interval(const std::string& value) {
    if (!value.empty() && value.front() == '-') {
        throw std::invalid_argument("--progress-interval must not be negative");
    }
    const std::uint64_t seconds =
        parse_decimal(value, "--progress-interval");
    const auto maximum = std::chrono::milliseconds::max().count();
    constexpr std::uint64_t kMillisecondsPerSecond = 1000;
    if (seconds > static_cast<std::uint64_t>(maximum) /
                      kMillisecondsPerSecond) {
        throw std::invalid_argument("--progress-interval exceeds the supported range");
    }
    return std::chrono::milliseconds{
        static_cast<std::chrono::milliseconds::rep>(
            seconds * kMillisecondsPerSecond)};
}

OutputFormat parse_output_format(const std::string& value) {
    if (value == "vcf") {
        return OutputFormat::vcf;
    }
    if (value == "vcf.gz") {
        return OutputFormat::vcf_gz;
    }
    throw std::invalid_argument("--output-format must be vcf or vcf.gz");
}

OutputFormat infer_output_format(const std::filesystem::path& output) {
    const std::string filename = lower_ascii(output.string());
    if (ends_with(filename, ".vcf.gz")) {
        return OutputFormat::vcf_gz;
    }
    if (ends_with(filename, ".vcf")) {
        return OutputFormat::vcf;
    }
    throw std::invalid_argument(
        "output path must end in .vcf or .vcf.gz, or use --output-format");
}

const char* require_value(int argc, char** argv, int& index, const char* option) {
    if (index + 1 >= argc || argv[index + 1] == nullptr ||
        std::string(argv[index + 1]).rfind("--", 0) == 0) {
        throw std::invalid_argument(std::string("missing value for ") + option);
    }
    return argv[++index];
}

void mark_seen(std::unordered_set<std::string>& seen, const std::string& option) {
    if (!seen.insert(option).second) {
        throw std::invalid_argument("duplicate option: " + option);
    }
}

}  // namespace

Config parse_cli(int argc, char** argv) {
    if (argc > 0 && argv == nullptr) {
        throw std::invalid_argument("null command-line argument vector");
    }

    Config config;
    bool threads_specified = false;
    bool output_format_specified = false;
    bool help_requested = false;
    bool version_requested = false;
    std::unordered_set<std::string> seen;

    for (int index = 1; index < argc; ++index) {
        if (argv[index] == nullptr) {
            throw std::invalid_argument("null command-line argument");
        }
        const std::string option(argv[index]);
        if (option == "--help") {
            mark_seen(seen, option);
            help_requested = true;
            continue;
        }
        if (option == "--version") {
            mark_seen(seen, option);
            version_requested = true;
            continue;
        }
        if (option == "--quiet") {
            mark_seen(seen, option);
            config.quiet = true;
            continue;
        }
        if (option == "--force") {
            mark_seen(seen, option);
            config.force = true;
            continue;
        }
        if (option == "--variants") {
            mark_seen(seen, option);
            config.variants = require_value(argc, argv, index, "--variants");
            continue;
        }
        if (option == "--input") {
            mark_seen(seen, option);
            config.input = require_value(argc, argv, index, "--input");
            continue;
        }
        if (option == "--output") {
            mark_seen(seen, option);
            config.output = require_value(argc, argv, index, "--output");
            continue;
        }
        if (option == "--threads") {
            mark_seen(seen, option);
            config.threads = parse_threads(require_value(argc, argv, index, "--threads"));
            threads_specified = true;
            continue;
        }
        if (option == "--memory-limit") {
            mark_seen(seen, option);
            config.memory_limit_bytes =
                parse_memory_limit(require_value(argc, argv, index, "--memory-limit"));
            continue;
        }
        if (option == "--progress-interval") {
            mark_seen(seen, option);
            config.progress_interval = parse_progress_interval(
                require_value(argc, argv, index, "--progress-interval"));
            continue;
        }
        if (option == "--output-format") {
            mark_seen(seen, option);
            config.output_format = parse_output_format(
                require_value(argc, argv, index, "--output-format"));
            output_format_specified = true;
            continue;
        }
        throw std::invalid_argument("unknown option: " + option);
    }

    if (help_requested && version_requested) {
        throw std::invalid_argument("--help and --version are mutually exclusive");
    }
    if (help_requested) {
        throw UsageRequested(false);
    }
    if (version_requested) {
        throw UsageRequested(true);
    }

    if (config.variants.empty()) {
        throw std::invalid_argument("missing required option: --variants");
    }
    if (config.input.empty()) {
        throw std::invalid_argument("missing required option: --input");
    }
    if (config.output.empty()) {
        throw std::invalid_argument("missing required option: --output");
    }

    if (!threads_specified) {
        const unsigned int available = std::thread::hardware_concurrency();
        config.threads = available == 0 ? 1 : static_cast<std::size_t>(available);
    }
    if (!output_format_specified) {
        config.output_format = infer_output_format(config.output);
    }
    return config;
}

ThreadAllocation allocate_threads(const Config& config, bool compressed_input,
                                  bool compressed_output) {
    std::size_t remaining = config.threads == 0 ? 1 : config.threads;
    ThreadAllocation allocation;

    if (compressed_output && remaining > 1) {
        allocation.output_io_workers = 1;
        --remaining;
    }
    if (compressed_input && config.threads >= 4 && remaining > 1) {
        allocation.input_io_workers = 1;
        --remaining;
    }
    allocation.conversion_workers = remaining;
    return allocation;
}

const char* usage_text() {
    return R"(Usage: convert-to-biallelic --variants FILE --input FILE --output FILE [options]

Required options:
  --variants FILE              Variant annotation input file.
  --input FILE                 Input VCF or VCF.GZ file.
  --output FILE                Explicit output VCF file path.

Optional options:
  --threads N                  Conversion thread budget; default is logical CPU count.
  --memory-limit N[K|M|G]      Memory limit in bytes or binary K/M/G units; minimum 64 MiB.
  --progress-interval SECONDS  Progress-report interval in seconds; default 5.
  --quiet                      Suppress progress reporting.
  --force                      Permit replacing an existing output file.
  --output-format vcf|vcf.gz   Override output format inferred from --output.
  --help                       Print this help text.
  --version                    Print version information.

Progress is written to stdout. Diagnostics are written to stderr. VCF output is
written to the explicit file supplied by --output.
)";
}

}  // namespace ctb
````````

## `src/converter.cpp`

````````text
#include "convert_to_biallelic/converter.hpp"

#include <algorithm>
#include <array>
#include <charconv>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <limits>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <system_error>
#include <vector>

namespace ctb {
namespace {

struct InfoEntry {
    std::string_view key;
    std::string_view value;
};

struct ResolvedVariant {
    std::string_view id;
    const VariantDefinition* definition;
};

struct ParsedSample {
    std::size_t allele_offset = 0;
    std::size_t allele_count = 0;
    std::string_view gq;
};

std::size_t checked_size_add(std::size_t left,
                             std::size_t right,
                             const char* description) {
    if (right > std::numeric_limits<std::size_t>::max() - left) {
        throw std::length_error(std::string(description) + " overflow");
    }
    return left + right;
}

std::size_t checked_size_multiply(std::size_t left,
                                  std::size_t right,
                                  const char* description) {
    if (left != 0 &&
        right > std::numeric_limits<std::size_t>::max() / left) {
        throw std::length_error(std::string(description) + " overflow");
    }
    return left * right;
}

std::size_t checked_required_size(std::size_t current,
                                  std::size_t additional,
                                  std::size_t maximum,
                                  const char* description) {
    if (current > maximum || additional > maximum - current) {
        throw std::length_error(std::string(description) + " overflow");
    }
    return current + additional;
}

std::size_t output_storage_bytes(std::size_t capacity) {
    return checked_size_add(capacity, 1, "converted output storage");
}

std::size_t planned_capacity(std::size_t current,
                             std::size_t required,
                             std::size_t maximum,
                             const char* description) {
    if (required <= current) {
        return current;
    }
    if (required > maximum) {
        throw std::length_error(std::string(description) + " is too large");
    }

    std::size_t target = required;
    if (current != 0 && current <= maximum - current) {
        target = std::max(required, current * 2);
    }
    return target;
}

template <typename Function>
void for_each_token(std::string_view text,
                    char delimiter,
                    Function&& function) {
    std::size_t begin = 0;
    for (;;) {
        const std::size_t end = text.find(delimiter, begin);
        if (end == std::string_view::npos) {
            function(text.substr(begin));
            return;
        }
        function(text.substr(begin, end - begin));
        begin = end + 1;
    }
}

class ConverterScratch {
public:
    ConverterScratch(
        std::string& destination,
        const std::function<void(std::size_t, std::size_t)>& admission)
        : destination_(destination), admission_(admission) {}

    void ensure_fields(std::size_t additional = 1) {
        ensure_capacity(fields, additional, "field metadata");
    }

    void ensure_info(std::size_t additional = 1) {
        ensure_capacity(info, additional, "INFO metadata");
    }

    void ensure_allele_mappings(std::size_t additional = 1) {
        ensure_capacity(allele_mappings, additional,
                        "allele mapping metadata");
    }

    void ensure_resolved(std::size_t additional = 1) {
        ensure_capacity(resolved, additional, "resolved variant metadata");
    }

    void ensure_format(std::size_t additional = 1) {
        ensure_capacity(format, additional, "FORMAT metadata");
    }

    void ensure_samples(std::size_t additional = 1) {
        ensure_capacity(samples, additional, "sample metadata");
    }

    void ensure_alleles(std::size_t additional = 1) {
        ensure_capacity(alleles, additional, "sample allele metadata");
    }

    void ensure_output_capacity(std::size_t additional) {
        if (released_) {
            throw std::logic_error(
                "Converter output cannot grow after scratch release");
        }

        const std::size_t required = checked_required_size(
            destination_.size(), additional, destination_.max_size(),
            "converted output string size");
        if (required <= destination_.capacity()) {
            return;
        }

        const std::size_t target = planned_capacity(
            destination_.capacity(), required, destination_.max_size(),
            "converted output string");
        const std::size_t transient_output_bytes = checked_size_add(
            output_storage_bytes(destination_.capacity()),
            output_storage_bytes(target),
            "reserve-time converted output storage");
        admission_(transient_output_bytes, scratch_bytes());
        destination_.reserve(target);
        admission_(output_storage_bytes(destination_.capacity()),
                   scratch_bytes());
    }

    void release_storage_and_admission() {
        if (released_) {
            throw std::logic_error(
                "Converter scratch admission was already released");
        }

        release_vector(fields);
        release_vector(info);
        release_vector(allele_mappings);
        release_vector(resolved);
        release_vector(format);
        release_vector(samples);
        release_vector(alleles);
        admission_(output_storage_bytes(destination_.capacity()), 0);
        released_ = true;
    }

    std::vector<std::string_view> fields;
    std::vector<InfoEntry> info;
    std::vector<std::string_view> allele_mappings;
    std::vector<ResolvedVariant> resolved;
    std::vector<std::string_view> format;
    std::vector<ParsedSample> samples;
    std::vector<std::int64_t> alleles;

private:
    template <typename T>
    static void release_vector(std::vector<T>& values) {
        std::vector<T>{}.swap(values);
        // The temporary and its former allocation are destroyed at the full-
        // expression boundary, before this helper returns to zero admission.
    }

    template <typename T>
    void ensure_capacity(std::vector<T>& values,
                         std::size_t additional,
                         const char* description) {
        if (released_) {
            throw std::logic_error(
                "Converter scratch cannot grow after admission release");
        }

        const std::size_t required = checked_required_size(
            values.size(), additional, values.max_size(), description);
        if (required <= values.capacity()) {
            return;
        }

        const std::size_t target = planned_capacity(
            values.capacity(), required, values.max_size(), description);
        const std::size_t requested_allocation = checked_size_multiply(
            target, sizeof(T), description);
        const std::size_t transient_scratch_bytes = checked_size_add(
            scratch_bytes(), requested_allocation,
            "reserve-time converter scratch byte count");
        admission_(output_storage_bytes(destination_.capacity()),
                   transient_scratch_bytes);
        values.reserve(target);
        admission_(output_storage_bytes(destination_.capacity()),
                   scratch_bytes());
    }

    std::size_t calculate_scratch_bytes() const {
        std::size_t total = 0;
        const auto add_capacity =
            [&](std::size_t capacity,
                std::size_t element_size,
                const char* description) {
                total = checked_size_add(
                    total,
                    checked_size_multiply(capacity, element_size,
                                          description),
                    "converter scratch byte count");
            };

        add_capacity(fields.capacity(), sizeof(std::string_view),
                     "field scratch bytes");
        add_capacity(info.capacity(), sizeof(InfoEntry),
                     "INFO scratch bytes");
        add_capacity(allele_mappings.capacity(), sizeof(std::string_view),
                     "allele mapping scratch bytes");
        add_capacity(resolved.capacity(),
                     sizeof(ResolvedVariant),
                     "resolved variant scratch bytes");
        add_capacity(format.capacity(), sizeof(std::string_view),
                     "FORMAT scratch bytes");
        add_capacity(samples.capacity(),
                     sizeof(ParsedSample), "sample scratch bytes");
        add_capacity(alleles.capacity(),
                     sizeof(std::int64_t), "allele scratch bytes");
        return total;
    }

    std::size_t scratch_bytes() const {
        return calculate_scratch_bytes();
    }

    std::string& destination_;
    const std::function<void(std::size_t, std::size_t)>& admission_;
    bool released_ = false;
};

void append_text(ConverterScratch& scratch,
                 std::string& destination,
                 std::string_view text) {
    if (text.empty()) {
        return;
    }
    scratch.ensure_output_capacity(text.size());
    destination.append(text.data(), text.size());
}

void append_character(ConverterScratch& scratch,
                      std::string& destination,
                      char character) {
    scratch.ensure_output_capacity(1);
    destination.push_back(character);
}

void parse_fields(std::string_view line, ConverterScratch& scratch) {
    for_each_token(line, '\t', [&](std::string_view field) {
        scratch.ensure_fields();
        scratch.fields.push_back(field);
    });
}

void parse_info(std::string_view text, ConverterScratch& scratch) {
    for_each_token(text, ';', [&](std::string_view raw_entry) {
        const std::size_t equals = raw_entry.find('=');
        if (equals == std::string_view::npos) {
            return;
        }

        const std::string_view key = raw_entry.substr(0, equals);
        const std::string_view value = raw_entry.substr(equals + 1);
        const auto existing = std::find_if(
            scratch.info.begin(), scratch.info.end(),
            [key](const InfoEntry& entry) { return entry.key == key; });
        if (existing == scratch.info.end()) {
            scratch.ensure_info();
            scratch.info.push_back(InfoEntry{key, value});
        } else {
            existing->value = value;
        }
    });
}

std::string_view require_identifier(const ConverterScratch& scratch) {
    const auto identifier = std::find_if(
        scratch.info.begin(), scratch.info.end(),
        [](const InfoEntry& entry) { return entry.key == "ID"; });
    if (identifier == scratch.info.end() || identifier->value.empty()) {
        throw std::invalid_argument(
            "INFO must contain a nonempty ID value");
    }
    return identifier->value;
}

void parse_allele_mappings(std::string_view identifier,
                           ConverterScratch& scratch) {
    scratch.ensure_allele_mappings();
    scratch.allele_mappings.push_back(
        std::string_view{});  // REF has no variant ID.

    for_each_token(identifier, ',', [&](std::string_view mapping) {
        for_each_token(mapping, ':', [](std::string_view component) {
            if (component.empty()) {
                throw std::invalid_argument(
                    "INFO ID allele mappings must not contain empty component IDs");
            }
        });
        scratch.ensure_allele_mappings();
        scratch.allele_mappings.push_back(mapping);
    });
}

bool resolve_variants(std::string_view chromosome,
                      const AnnotationIndex& annotation,
                      ConverterScratch& scratch) {
    bool unknown_single_mapping = false;
    const bool permit_unknown_passthrough =
        scratch.allele_mappings.size() == 2;

    for (std::size_t allele = 1;
         allele < scratch.allele_mappings.size(); ++allele) {
        for_each_token(
            scratch.allele_mappings[allele], ':',
            [&](std::string_view component) {
                if (unknown_single_mapping) {
                    return;
                }

                const VariantDefinition* const definition =
                    annotation.find_checked(chromosome, component);
                if (definition == nullptr) {
                    if (permit_unknown_passthrough) {
                        unknown_single_mapping = true;
                        return;
                    }
                    throw std::invalid_argument(
                        "missing annotation for chromosome '" +
                        std::string(chromosome) + "' and ID '" +
                        std::string(component) + "'");
                }

                const auto duplicate = std::find_if(
                    scratch.resolved.begin(), scratch.resolved.end(),
                    [component](const ResolvedVariant& variant) {
                        return variant.id == component;
                    });
                if (duplicate == scratch.resolved.end()) {
                    scratch.ensure_resolved();
                    scratch.resolved.push_back(
                        ResolvedVariant{component, definition});
                }
            });
        if (unknown_single_mapping) {
            return true;
        }
    }

    std::sort(scratch.resolved.begin(), scratch.resolved.end(),
              [](const ResolvedVariant& left,
                 const ResolvedVariant& right) {
                  if (left.definition->position !=
                      right.definition->position) {
                      return left.definition->position <
                             right.definition->position;
                  }
                  return left.id < right.id;
              });
    return false;
}

void parse_format(std::string_view text, ConverterScratch& scratch) {
    for_each_token(text, ':', [&](std::string_view token) {
        scratch.ensure_format();
        scratch.format.push_back(token);
    });
}

std::size_t find_required_gt(const ConverterScratch& scratch) {
    const auto gt =
        std::find(scratch.format.begin(), scratch.format.end(), "GT");
    if (gt == scratch.format.end()) {
        throw std::invalid_argument(
            "FORMAT must contain an exact GT token");
    }
    return static_cast<std::size_t>(gt - scratch.format.begin());
}

std::optional<std::size_t> find_gq(const ConverterScratch& scratch) {
    const auto gq =
        std::find(scratch.format.begin(), scratch.format.end(), "GQ");
    if (gq == scratch.format.end()) {
        return std::nullopt;
    }
    return static_cast<std::size_t>(gq - scratch.format.begin());
}

std::size_t parse_allele_index(std::string_view text,
                               std::size_t sample_number) {
    if (text.empty() ||
        !std::all_of(text.begin(), text.end(), [](char character) {
            return character >= '0' && character <= '9';
        })) {
        throw std::invalid_argument(
            "sample " + std::to_string(sample_number) + " GT allele '" +
            std::string(text) +
            "' is not a fully consumed nonnegative decimal index");
    }

    std::size_t index = 0;
    const char* const begin = text.data();
    const char* const end = begin + text.size();
    const auto result = std::from_chars(begin, end, index);
    if (result.ec != std::errc{} || result.ptr != end) {
        throw std::invalid_argument(
            "sample " + std::to_string(sample_number) + " GT allele '" +
            std::string(text) +
            "' is not a fully consumed nonnegative decimal index");
    }
    return index;
}

struct SampleValues {
    std::string_view gt;
    std::string_view gq;
    bool has_gt = false;
    bool has_gq = false;
};

SampleValues locate_sample_values(
    std::string_view sample,
    std::size_t gt_index,
    std::optional<std::size_t> gq_index,
    std::size_t sample_number) {
    SampleValues located;
    std::size_t value_index = 0;
    for_each_token(sample, ':', [&](std::string_view value) {
        if (value_index == gt_index) {
            located.gt = value;
            located.has_gt = true;
        }
        if (gq_index.has_value() && value_index == *gq_index) {
            located.gq = value;
            located.has_gq = true;
        }
        value_index = checked_size_add(
            value_index, 1, "sample FORMAT value count");
    });

    if (!located.has_gt) {
        throw std::invalid_argument(
            "sample " + std::to_string(sample_number) +
            " does not contain the GT field required by FORMAT");
    }
    if (gq_index.has_value() && !located.has_gq) {
        throw std::invalid_argument(
            "sample " + std::to_string(sample_number) +
            " does not contain the GQ field required by FORMAT");
    }
    return located;
}

void append_parsed_allele(std::string_view allele,
                          std::size_t sample_number,
                          ConverterScratch& scratch) {
    scratch.ensure_alleles();
    if (allele == ".") {
        scratch.alleles.push_back(-1);
        return;
    }

    const std::size_t allele_index =
        parse_allele_index(allele, sample_number);
    if (allele_index >= scratch.allele_mappings.size()) {
        throw std::invalid_argument(
            "sample " + std::to_string(sample_number) +
            " GT allele index " + std::string(allele) +
            " is outside the INFO ID allele mapping range");
    }
    if (static_cast<std::uintmax_t>(allele_index) >
        static_cast<std::uintmax_t>(
            std::numeric_limits<std::int64_t>::max())) {
        throw std::overflow_error(
            "sample allele index cannot be represented internally");
    }
    scratch.alleles.push_back(
        static_cast<std::int64_t>(allele_index));
}

void parse_genotype(std::string_view genotype,
                    std::size_t sample_number,
                    ConverterScratch& scratch) {
    std::size_t begin = 0;
    for (std::size_t index = 0; index < genotype.size(); ++index) {
        if (genotype[index] == '/' || genotype[index] == '|') {
            append_parsed_allele(
                genotype.substr(begin, index - begin), sample_number,
                scratch);
            begin = index + 1;
        }
    }
    append_parsed_allele(genotype.substr(begin), sample_number, scratch);
}

void parse_samples(std::size_t gt_index,
                   std::optional<std::size_t> gq_index,
                   ConverterScratch& scratch) {
    for (std::size_t field_index = 9;
         field_index < scratch.fields.size(); ++field_index) {
        const std::size_t sample_number = field_index - 8;
        const SampleValues values = locate_sample_values(
            scratch.fields[field_index], gt_index, gq_index,
            sample_number);

        ParsedSample sample;
        sample.allele_offset = scratch.alleles.size();
        sample.gq = values.gq;
        parse_genotype(values.gt, sample_number, scratch);
        sample.allele_count =
            scratch.alleles.size() - sample.allele_offset;
        scratch.ensure_samples();
        scratch.samples.push_back(sample);
    }
}

bool mapping_contains_variant(std::string_view mapping,
                              std::string_view variant_id) {
    bool contains = false;
    for_each_token(mapping, ':', [&](std::string_view component) {
        if (component == variant_id) {
            contains = true;
        }
    });
    return contains;
}

void append_info(ConverterScratch& scratch,
                 std::string& destination,
                 std::string_view variant_id) {
    append_text(scratch, destination, "ID=");
    append_text(scratch, destination, variant_id);
    for (const InfoEntry& entry : scratch.info) {
        if (entry.key != "MA" && entry.key != "UK") {
            continue;
        }
        append_character(scratch, destination, ';');
        append_text(scratch, destination, entry.key);
        append_character(scratch, destination, '=');
        append_text(scratch, destination, entry.value);
    }
}

void append_sample(ConverterScratch& scratch,
                   std::string& destination,
                   const ParsedSample& sample,
                   std::string_view variant_id,
                   bool include_gq) {
    if (sample.allele_offset > scratch.alleles.size() ||
        sample.allele_count >
            scratch.alleles.size() - sample.allele_offset) {
        throw std::logic_error(
            "Parsed sample allele range is inconsistent");
    }

    for (std::size_t index = 0; index < sample.allele_count; ++index) {
        if (index != 0) {
            append_character(scratch, destination, '/');
        }

        const std::int64_t allele =
            scratch.alleles[sample.allele_offset + index];
        if (allele < 0) {
            append_character(scratch, destination, '.');
            continue;
        }

        const std::size_t mapping_index =
            static_cast<std::size_t>(allele);
        if (mapping_index >= scratch.allele_mappings.size()) {
            throw std::logic_error(
                "Parsed sample allele index is inconsistent");
        }
        const bool contains_variant = mapping_contains_variant(
            scratch.allele_mappings[mapping_index], variant_id);
        append_character(scratch, destination,
                         contains_variant ? '1' : '0');
    }

    if (include_gq) {
        append_character(scratch, destination, ':');
        append_text(scratch, destination, sample.gq);
    }
}

void append_passthrough(std::string_view line,
                        ConverterScratch& scratch,
                        std::string& destination,
                        std::uint64_t& output_records) {
    if (output_records == std::numeric_limits<std::uint64_t>::max()) {
        throw std::overflow_error(
            "Converted output record count overflow");
    }
    append_text(scratch, destination, line);
    append_character(scratch, destination, '\n');
    ++output_records;
}

std::string_view format_position(
    std::int64_t position,
    std::array<char, 32>& storage) {
    const auto converted = std::to_chars(
        storage.data(), storage.data() + storage.size(), position);
    if (converted.ec != std::errc{}) {
        throw std::overflow_error(
            "Annotation position could not be formatted");
    }
    return std::string_view(
        storage.data(),
        static_cast<std::size_t>(converted.ptr - storage.data()));
}

void append_converted_record_impl(
    std::string_view line,
    const AnnotationIndex& annotation,
    std::string& destination,
    std::uint64_t& output_records,
    const std::function<void(std::size_t, std::size_t)>& admission) {
    ConverterScratch scratch(destination, admission);
    parse_fields(line, scratch);
    if (scratch.fields.size() < 8) {
        throw std::invalid_argument(
            "expected at least 8 tab-separated fields");
    }

    parse_info(scratch.fields[7], scratch);
    const std::string_view identifier = require_identifier(scratch);
    parse_allele_mappings(identifier, scratch);

    const bool unknown_single_mapping =
        resolve_variants(scratch.fields[0], annotation, scratch);
    if (unknown_single_mapping) {
        append_passthrough(line, scratch, destination, output_records);
        scratch.release_storage_and_admission();
        return;
    }

    if (scratch.fields.size() < 9) {
        throw std::invalid_argument(
            "known records require a FORMAT field");
    }

    parse_format(scratch.fields[8], scratch);
    const std::size_t gt_index = find_required_gt(scratch);
    const std::optional<std::size_t> gq_index = find_gq(scratch);
    parse_samples(gt_index, gq_index, scratch);
    const std::string_view output_format =
        gq_index.has_value() ? std::string_view("GT:GQ")
                             : std::string_view("GT");

    std::array<char, 32> position_storage{};
    for (const ResolvedVariant& variant : scratch.resolved) {
        if (output_records == std::numeric_limits<std::uint64_t>::max()) {
            throw std::overflow_error(
                "Converted output record count overflow");
        }

        const std::string_view position = format_position(
            variant.definition->position, position_storage);
        append_text(scratch, destination, scratch.fields[0]);
        append_character(scratch, destination, '\t');
        append_text(scratch, destination, position);
        append_character(scratch, destination, '\t');
        append_text(scratch, destination, variant.id);
        append_character(scratch, destination, '\t');
        append_text(scratch, destination, variant.definition->ref);
        append_character(scratch, destination, '\t');
        append_text(scratch, destination, variant.definition->alt);
        append_character(scratch, destination, '\t');
        append_text(scratch, destination, scratch.fields[5]);
        append_character(scratch, destination, '\t');
        append_text(scratch, destination, scratch.fields[6]);
        append_character(scratch, destination, '\t');
        append_info(scratch, destination, variant.id);
        append_character(scratch, destination, '\t');
        append_text(scratch, destination, output_format);

        for (const ParsedSample& sample : scratch.samples) {
            append_character(scratch, destination, '\t');
            append_sample(scratch, destination, sample, variant.id,
                          gq_index.has_value());
        }

        append_character(scratch, destination, '\n');
        ++output_records;
    }

    scratch.release_storage_and_admission();
}

}  // namespace

std::optional<std::string> convert_header(std::string_view line) {
    constexpr std::array<std::string_view, 4> removed_fields{
        "INFO=<ID=AF", "INFO=<ID=AK", "FORMAT=<ID=GL", "FORMAT=<ID=KC"};
    for (const std::string_view removed_field : removed_fields) {
        if (line.find(removed_field) != std::string_view::npos) {
            return std::nullopt;
        }
    }

    std::string output(line);
    output.push_back('\n');
    return output;
}

ConversionResult convert_record(std::string_view line,
                                const AnnotationIndex& annotation,
                                std::uint64_t line_number) {
    ConversionResult result;
    const std::function<void(std::size_t, std::size_t)> no_admission =
        [](std::size_t, std::size_t) {};
    append_converted_record(line, annotation, line_number, result.bytes,
                            result.output_records, no_admission);
    return result;
}

void append_converted_record(
    std::string_view line,
    const AnnotationIndex& annotation,
    std::uint64_t line_number,
    std::string& destination,
    std::uint64_t& output_records,
    const std::function<void(std::size_t planned_output_bytes,
                             std::size_t planned_scratch_bytes)>&
        before_reserve) {
    try {
        append_converted_record_impl(line, annotation, destination,
                                     output_records, before_reserve);
    } catch (const std::exception& error) {
        throw std::runtime_error("Input line " + std::to_string(line_number) +
                                 ": " + error.what());
    }
}

}  // namespace ctb
````````

## `src/main.cpp`

````````text
#include "convert_to_biallelic/annotation_index.hpp"
#include "convert_to_biallelic/cli.hpp"
#include "convert_to_biallelic/output_transaction.hpp"
#include "convert_to_biallelic/pipeline.hpp"
#include "convert_to_biallelic/progress.hpp"
#include "convert_to_biallelic/vcf_io.hpp"

#include <exception>
#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <string>
#include <string_view>

namespace {

constexpr std::string_view kVersion = "convert-to-biallelic 0.1.0\n";

void write_stdout(std::string_view text) {
    std::cout << text << std::flush;
    if (!std::cout) {
        throw std::runtime_error("Failed to write standard output");
    }
}

void report_error(std::string_view message) noexcept {
    try {
        std::cerr << "Error: " << message << '\n' << std::flush;
    } catch (...) {
        // There is no secondary reporting channel. Preserve the primary
        // failure and its exit code if stderr itself is unavailable.
    }
}

ctb::AnnotationIndex load_annotation_from_config(const ctb::Config& config) {
    // Annotation loading is intentionally single-streamed and uses no HTSlib
    // I/O workers so the full configured thread budget remains available to
    // the subsequently opened conversion input and output.
    ctb::InputSource annotation_input(config.variants, 0);
    ctb::AnnotationIndex annotation =
        ctb::load_annotation(annotation_input, config.memory_limit_bytes);
    annotation_input.close();
    return annotation;
}

int run_conversion(const ctb::Config& config) {
    ctb::AnnotationIndex annotation = load_annotation_from_config(config);

    const bool compressed_output =
        config.output_format == ctb::OutputFormat::vcf_gz;
    // InputSource discovers compression only after hts_open. Pass the worker
    // count that would apply to compressed input; the constructor ignores it
    // for plain input. This keeps the conversion input single-open while the
    // final allocation still returns that unit to conversion for plain VCF.
    const ctb::ThreadAllocation compressed_candidate =
        ctb::allocate_threads(config, true, compressed_output);
    ctb::InputSource input(config.input,
                           compressed_candidate.input_io_workers);
    const ctb::ThreadAllocation threads =
        ctb::allocate_threads(config, input.compressed(), compressed_output);

    ctb::PipelineOptions options;
    options.threads = threads;
    options.memory_limit_bytes = config.memory_limit_bytes;
    options.progress_interval = config.progress_interval;
    options.quiet = config.quiet;

    ctb::OutputTransaction transaction(config.output, config.force);
    const int sink_fd = transaction.take_sink_fd();
    ctb::OutputSink output(sink_fd, transaction.temporary_path(),
                           config.output_format,
                           threads.output_io_workers);

    ctb::PipelineStats stats =
        ctb::run_pipeline(input, output, annotation, options,
                          std::cout, std::cerr);

    input.close();
    output.flush();
    output.close();
    transaction.commit();

    if (!config.quiet) {
        const std::string summary = ctb::format_final_summary(stats);
        write_stdout(summary + "\n");
    }
    return 0;
}

}  // namespace

int main(int argc, char** argv) {
    ctb::Config config;
    try {
        try {
            config = ctb::parse_cli(argc, argv);
        } catch (const ctb::UsageRequested& request) {
            write_stdout(request.version() ? kVersion
                                           : std::string_view(ctb::usage_text()));
            return 0;
        } catch (const std::invalid_argument& error) {
            report_error(error.what());
            return 2;
        }

        return run_conversion(config);
    } catch (const std::exception& error) {
        report_error(error.what());
        return 1;
    } catch (...) {
        report_error("Unknown non-standard exception");
        return 1;
    }
}
````````

## `src/memory_budget.cpp`

````````text
#include "convert_to_biallelic/memory_budget.hpp"

#include <algorithm>
#include <limits>
#include <stdexcept>

namespace ctb {
namespace {

constexpr const char* kCancelledMessage = "Memory budget is cancelled";

}  // namespace

MemoryPermit::MemoryPermit(MemoryBudget* owner,
                           std::uint64_t bytes,
                           bool overage) noexcept
    : owner_(owner), bytes_(bytes), overage_(overage) {}

MemoryPermit::~MemoryPermit() noexcept {
    release();
}

MemoryPermit::MemoryPermit(MemoryPermit&& other) noexcept
    : owner_(other.owner_),
      bytes_(other.bytes_),
      overage_(other.overage_) {
    other.owner_ = nullptr;
    other.bytes_ = 0;
    other.overage_ = false;
}

MemoryPermit& MemoryPermit::operator=(MemoryPermit&& other) noexcept {
    if (this == &other) {
        return *this;
    }

    release();
    owner_ = other.owner_;
    bytes_ = other.bytes_;
    overage_ = other.overage_;
    other.owner_ = nullptr;
    other.bytes_ = 0;
    other.overage_ = false;
    return *this;
}

std::uint64_t MemoryPermit::bytes() const noexcept {
    return bytes_;
}

void MemoryPermit::resize(std::uint64_t bytes,
                          bool allow_one_overage) {
    if (owner_ == nullptr) {
        throw std::logic_error(
            "Cannot resize an empty or moved-from memory permit");
    }
    owner_->resize(*this, bytes, allow_one_overage);
}

void MemoryPermit::release() noexcept {
    if (owner_ == nullptr) {
        return;
    }

    owner_->release(bytes_, overage_);
    owner_ = nullptr;
    bytes_ = 0;
    overage_ = false;
}

MemoryBudget::MemoryBudget(std::uint64_t tracked_limit)
    : limit_(tracked_limit) {
    if (tracked_limit == 0) {
        throw std::invalid_argument(
            "Memory budget tracked limit must be positive");
    }
}

MemoryPermit MemoryBudget::acquire(std::uint64_t bytes,
                                   bool allow_one_overage) {
    std::unique_lock<std::mutex> lock(mutex_);
    if (cancelled_) {
        throw std::runtime_error(kCancelledMessage);
    }
    if (bytes == 0) {
        return MemoryPermit(this, 0, false);
    }

    changed_.wait(lock, [this, bytes, allow_one_overage] {
        return cancelled_ || fits_normal_locked(bytes) ||
               (allow_one_overage && !overage_active_);
    });

    if (cancelled_) {
        throw std::runtime_error(kCancelledMessage);
    }

    const bool overage = !fits_normal_locked(bytes);
    add_locked(bytes);
    if (overage) {
        overage_active_ = true;
    }
    return MemoryPermit(this, bytes, overage);
}

void MemoryBudget::cancel() {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (cancelled_) {
            return;
        }
        cancelled_ = true;
    }
    changed_.notify_all();
}

std::uint64_t MemoryBudget::current_bytes() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return current_high_ ? std::numeric_limits<std::uint64_t>::max()
                         : current_;
}

std::uint64_t MemoryBudget::peak_bytes() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return peak_;
}

bool MemoryBudget::fits_normal_locked(
    std::uint64_t additional) const noexcept {
    return !current_high_ && current_ <= limit_ &&
           additional <= limit_ - current_;
}

void MemoryBudget::add_locked(std::uint64_t bytes) noexcept {
    const std::uint64_t maximum =
        std::numeric_limits<std::uint64_t>::max();

    if (current_high_) {
        // A valid budget state cannot exceed twice UINT64_MAX: before an
        // overage the aggregate is at most the limit, and the sole overage
        // permit itself is at most UINT64_MAX. Clamp only as a defensive guard
        // against a corrupted state.
        current_ = bytes > maximum - current_ ? maximum : current_ + bytes;
        peak_ = maximum;
        return;
    }

    if (bytes > maximum - current_) {
        current_ = bytes - (maximum - current_) - 1;
        current_high_ = true;
        peak_ = maximum;
        return;
    }

    current_ += bytes;
    peak_ = std::max(peak_, current_);
}

void MemoryBudget::subtract_locked(std::uint64_t bytes) noexcept {
    const std::uint64_t maximum =
        std::numeric_limits<std::uint64_t>::max();

    if (current_high_) {
        if (bytes <= current_) {
            current_ -= bytes;
            return;
        }

        current_ = maximum - (bytes - current_ - 1);
        current_high_ = false;
        return;
    }

    // Correctly owned permits always satisfy bytes <= current_. Avoid unsigned
    // wrap if a future caller violates that accounting invariant.
    current_ = bytes > current_ ? 0 : current_ - bytes;
}

void MemoryBudget::resize(MemoryPermit& permit,
                          std::uint64_t bytes,
                          bool allow_one_overage) {
    std::unique_lock<std::mutex> lock(mutex_);
    if (cancelled_) {
        throw std::runtime_error(kCancelledMessage);
    }

    if (bytes <= permit.bytes_) {
        const std::uint64_t released = permit.bytes_ - bytes;
        subtract_locked(released);
        permit.bytes_ = bytes;

        bool cleared_overage = false;
        if (permit.overage_ && fits_normal_locked(0)) {
            permit.overage_ = false;
            overage_active_ = false;
            cleared_overage = true;
        }

        lock.unlock();
        if (released != 0 || cleared_overage) {
            changed_.notify_all();
        }
        return;
    }

    const std::uint64_t additional = bytes - permit.bytes_;
    changed_.wait(lock, [this, &permit, additional, allow_one_overage] {
        return cancelled_ || fits_normal_locked(additional) ||
               (allow_one_overage &&
                (permit.overage_ || !overage_active_));
    });

    if (cancelled_) {
        throw std::runtime_error(kCancelledMessage);
    }

    const bool fits_normally = fits_normal_locked(additional);
    add_locked(additional);
    permit.bytes_ = bytes;

    bool cleared_overage = false;
    if (fits_normally) {
        if (permit.overage_) {
            permit.overage_ = false;
            overage_active_ = false;
            cleared_overage = true;
        }
    } else if (!permit.overage_) {
        permit.overage_ = true;
        overage_active_ = true;
    }

    lock.unlock();
    if (cleared_overage) {
        changed_.notify_all();
    }
}

void MemoryBudget::release(std::uint64_t bytes, bool overage) noexcept {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        subtract_locked(bytes);
        if (overage) {
            overage_active_ = false;
        }
    }
    changed_.notify_all();
}

}  // namespace ctb
````````

## `src/output_transaction.cpp`

````````text
#ifndef _WIN32
#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#endif

#include "convert_to_biallelic/output_transaction.hpp"

#include <algorithm>
#include <atomic>
#include <cerrno>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>
#include <new>
#include <stdexcept>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

#ifdef _WIN32
#define NOMINMAX
#include <fcntl.h>
#include <io.h>
#include <windows.h>
#elif defined(__linux__)
#include <fcntl.h>
#include <stdio.h>
#include <sys/stat.h>
#include <unistd.h>
#else
#error "OutputTransaction supports only Windows and Linux"
#endif

namespace ctb {
namespace {

std::atomic<std::uint64_t> temporary_counter{0};
constexpr std::uint64_t kMaximumCreationAttempts = 1024ULL * 1024ULL;

std::string display_path(const std::filesystem::path& path) {
    return path.u8string();
}

std::string system_message(unsigned long error) {
#ifdef _WIN32
    return std::system_category().message(static_cast<int>(error));
#elif defined(__linux__)
    return std::generic_category().message(static_cast<int>(error));
#endif
}

std::string errno_message(int error) {
    return std::generic_category().message(error);
}

[[noreturn]] void throw_path_error(
    std::string_view operation,
    const std::filesystem::path& source,
    const std::filesystem::path& destination,
    unsigned long error) {
    throw std::runtime_error(
        std::string(operation) + " from '" + display_path(source) +
        "' to '" + display_path(destination) + "': " +
        system_message(error));
}

[[noreturn]] void throw_filesystem_error(
    std::string_view operation,
    const std::filesystem::path& path,
    const std::filesystem::path& destination,
    const std::error_code& error) {
    throw std::runtime_error(std::string(operation) + " '" +
                             display_path(path) + "' for destination '" +
                             display_path(destination) + "': " +
                             error.message());
}

void append_ascii(std::filesystem::path::string_type& destination,
                  std::string_view ascii) {
    for (const char character : ascii) {
        destination.push_back(
            static_cast<std::filesystem::path::value_type>(character));
    }
}

std::uint64_t process_id() noexcept {
#ifdef _WIN32
    return static_cast<std::uint64_t>(::GetCurrentProcessId());
#elif defined(__linux__)
    return static_cast<std::uint64_t>(::getpid());
#endif
}

std::filesystem::path make_candidate_name(
    const std::filesystem::path& filename,
    std::uint64_t counter,
    std::string_view role = {}) {
    std::filesystem::path::string_type name;
    name.push_back(static_cast<std::filesystem::path::value_type>('.'));
    name += filename.native();
    append_ascii(name, ".ctb.");
    if (!role.empty()) {
        append_ascii(name, role);
        name.push_back(static_cast<std::filesystem::path::value_type>('.'));
    }
    append_ascii(name, std::to_string(process_id()));
    name.push_back(static_cast<std::filesystem::path::value_type>('.'));
    append_ascii(name, std::to_string(counter));
    append_ascii(name, ".tmp");
    return std::filesystem::path(std::move(name));
}

std::filesystem::path absolute_destination(
    const std::filesystem::path& destination) {
    std::error_code error;
    std::filesystem::path absolute =
        std::filesystem::absolute(destination, error);
    if (error) {
        throw_filesystem_error("Failed to resolve output destination",
                               destination, destination, error);
    }
    return absolute.lexically_normal();
}

void validate_parent(const std::filesystem::path& parent,
                     const std::filesystem::path& destination) {
    std::error_code error;
    const std::filesystem::file_status status =
        std::filesystem::status(parent, error);
    if (error) {
        throw_filesystem_error("Failed to inspect output parent directory",
                               parent, destination, error);
    }
    if (!std::filesystem::is_directory(status)) {
        throw std::invalid_argument(
            "Output parent '" + display_path(parent) +
            "' for destination '" + display_path(destination) +
            "' is not an existing directory");
    }
}

#ifdef _WIN32
bool windows_path_entry_exists(const std::filesystem::path& path) {
    std::error_code error;
    const std::filesystem::file_status status =
        std::filesystem::symlink_status(path, error);
    if (error) {
        throw_filesystem_error("Failed to inspect output destination", path,
                               path, error);
    }
    if (status.type() == std::filesystem::file_type::not_found) {
        return false;
    }
    if (!std::filesystem::status_known(status)) {
        throw std::runtime_error(
            "Output destination has unknown filesystem status: '" +
            display_path(path) + "'");
    }
    return true;
}

bool same_windows_identity(const BY_HANDLE_FILE_INFORMATION& left,
                           const BY_HANDLE_FILE_INFORMATION& right) noexcept {
    return left.dwVolumeSerialNumber == right.dwVolumeSerialNumber &&
           left.nFileIndexHigh == right.nFileIndexHigh &&
           left.nFileIndexLow == right.nFileIndexLow;
}

void verify_windows_path_identity(
    HANDLE retained,
    const std::filesystem::path& temporary,
    const std::filesystem::path& destination) {
    BY_HANDLE_FILE_INFORMATION retained_info{};
    if (::GetFileInformationByHandle(retained, &retained_info) == 0) {
        const DWORD error = ::GetLastError();
        throw_path_error("Failed to inspect retained temporary output identity",
                         temporary, destination, error);
    }

    HANDLE path_handle = ::CreateFileW(
        temporary.c_str(), FILE_READ_ATTRIBUTES,
        FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
        nullptr, OPEN_EXISTING,
        FILE_ATTRIBUTE_NORMAL | FILE_FLAG_OPEN_REPARSE_POINT, nullptr);
    if (path_handle == INVALID_HANDLE_VALUE) {
        const DWORD error = ::GetLastError();
        throw_path_error("Failed to open temporary output path for identity verification",
                         temporary, destination, error);
    }

    BY_HANDLE_FILE_INFORMATION path_info{};
    if (::GetFileInformationByHandle(path_handle, &path_info) == 0) {
        const DWORD inspect_error = ::GetLastError();
        if (::CloseHandle(path_handle) == 0) {
            const DWORD close_error = ::GetLastError();
            throw std::runtime_error(
                "Failed to inspect temporary output path identity '" +
                display_path(temporary) + "' for destination '" +
                display_path(destination) + "': " +
                system_message(inspect_error) +
                "; verification-handle cleanup also failed: " +
                system_message(close_error));
        }
        throw_path_error("Failed to inspect temporary output path identity",
                         temporary, destination, inspect_error);
    }
    if (::CloseHandle(path_handle) == 0) {
        const DWORD error = ::GetLastError();
        throw_path_error("Failed to close temporary identity verification handle",
                         temporary, destination, error);
    }
    if (!same_windows_identity(retained_info, path_info)) {
        throw std::runtime_error(
            "Temporary output path no longer names the retained file identity: '" +
            display_path(temporary) + "' for destination '" +
            display_path(destination) + "'");
    }
}
#endif

#ifdef __linux__
bool tmpfile_is_unsupported(int error) noexcept {
    return error == EOPNOTSUPP || error == EISDIR || error == ENOENT ||
           error == EINVAL;
}

bool same_linux_identity(const struct stat& left,
                         const struct stat& right) noexcept {
    return left.st_dev == right.st_dev && left.st_ino == right.st_ino;
}

bool linux_named_identity_matches(
    int retained_fd,
    int directory_fd,
    const std::filesystem::path& temporary_name,
    const std::filesystem::path& temporary,
    const std::filesystem::path& destination) {
    struct stat retained_status {};
    if (::fstat(retained_fd, &retained_status) != 0) {
        const int error = errno;
        throw_path_error("Failed to inspect retained Linux output identity",
                         temporary, destination,
                         static_cast<unsigned long>(error));
    }

    struct stat path_status {};
    if (::fstatat(directory_fd, temporary_name.c_str(), &path_status,
                  AT_SYMLINK_NOFOLLOW) != 0) {
        const int error = errno;
        throw_path_error("Failed to inspect linked temporary output identity",
                         temporary, destination,
                         static_cast<unsigned long>(error));
    }
    return same_linux_identity(retained_status, path_status);
}

struct LinkIdentityResult {
    bool linked = false;
    int error = 0;
    int empty_path_error = 0;
    bool procfs_attempted = false;
};

bool should_try_procfs_link(int error) noexcept {
    return error == ENOENT || error == EPERM || error == EACCES ||
           error == EINVAL;
}

LinkIdentityResult link_anonymous_identity(
    int retained_fd,
    int directory_fd,
    const std::filesystem::path& destination_name) {
    if (::linkat(retained_fd, "", directory_fd,
                 destination_name.c_str(), AT_EMPTY_PATH) == 0) {
        return LinkIdentityResult{true, 0, 0, false};
    }

    const int empty_path_error = errno;
    if (!should_try_procfs_link(empty_path_error)) {
        return LinkIdentityResult{false, empty_path_error,
                                  empty_path_error, false};
    }

    const std::string proc_path =
        "/proc/self/fd/" + std::to_string(retained_fd);
    if (::linkat(AT_FDCWD, proc_path.c_str(), directory_fd,
                 destination_name.c_str(), AT_SYMLINK_FOLLOW) == 0) {
        return LinkIdentityResult{true, 0, empty_path_error, true};
    }
    return LinkIdentityResult{false, errno, empty_path_error, true};
}

[[noreturn]] void throw_link_identity_error(
    std::string_view operation,
    const std::filesystem::path& source,
    const std::filesystem::path& destination,
    const LinkIdentityResult& result) {
    if (result.procfs_attempted) {
        throw std::runtime_error(
            std::string(operation) + " from retained identity '" +
            display_path(source) + "' to '" + display_path(destination) +
            "': AT_EMPTY_PATH failed: " +
            system_message(static_cast<unsigned long>(result.empty_path_error)) +
            "; /proc/self/fd fallback failed: " +
            system_message(static_cast<unsigned long>(result.error)));
    }
    throw_path_error(operation, source, destination,
                     static_cast<unsigned long>(result.error));
}
#endif

}  // namespace

struct OutputTransaction::Impl {
    std::filesystem::path destination;
    std::filesystem::path parent;
    std::filesystem::path destination_name;
    std::filesystem::path temporary;
    bool force = false;
    bool committed = false;
    bool sink_fd_taken = false;

#ifdef _WIN32
    HANDLE file_handle = INVALID_HANDLE_VALUE;
#elif defined(__linux__)
    int file_fd = -1;
    int directory_fd = -1;
    bool anonymous_tmpfile = false;
    std::filesystem::path temporary_name;
    bool temporary_linked = false;
    std::filesystem::path staging_name;
    bool staging_linked = false;
#endif

    ~Impl() noexcept {
#ifdef _WIN32
        if (file_handle == INVALID_HANDLE_VALUE) {
            return;
        }
        if (!committed) {
            FILE_DISPOSITION_INFO disposition{};
            disposition.DeleteFile = TRUE;
            (void)::SetFileInformationByHandle(
                file_handle, FileDispositionInfo, &disposition,
                static_cast<DWORD>(sizeof(disposition)));
        }
        (void)::CloseHandle(file_handle);
#elif defined(__linux__)
        if (directory_fd >= 0) {
            if (temporary_linked && !temporary_name.empty()) {
                (void)::unlinkat(directory_fd,
                                 temporary_name.c_str(), 0);
            }
            if (!committed && staging_linked && !staging_name.empty()) {
                (void)::unlinkat(directory_fd, staging_name.c_str(), 0);
            }
        }
        if (file_fd >= 0) {
            (void)::close(file_fd);
        }
        if (directory_fd >= 0) {
            (void)::close(directory_fd);
        }
#endif
    }
};

OutputTransaction::OutputTransaction(std::filesystem::path destination,
                                     bool force)
    : impl_(std::make_unique<Impl>()) {
    if (destination.filename().empty()) {
        throw std::invalid_argument(
            "Output destination must have a nonempty filename: '" +
            display_path(destination) + "'");
    }

    impl_->destination = absolute_destination(destination);
    impl_->destination_name = impl_->destination.filename();
    impl_->parent = impl_->destination.parent_path();
    impl_->force = force;
    validate_parent(impl_->parent, impl_->destination);

#ifdef _WIN32
    if (!force && windows_path_entry_exists(impl_->destination)) {
        throw std::runtime_error("Output destination already exists: '" +
                                 display_path(impl_->destination) + "'");
    }

    for (std::uint64_t attempt = 0; attempt < kMaximumCreationAttempts;
         ++attempt) {
        const std::uint64_t counter =
            temporary_counter.fetch_add(1, std::memory_order_relaxed);
        const std::filesystem::path candidate =
            impl_->parent /
            make_candidate_name(impl_->destination_name, counter);

        HANDLE handle = ::CreateFileW(
            candidate.c_str(), GENERIC_READ | GENERIC_WRITE | DELETE,
            FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
            nullptr, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (handle == INVALID_HANDLE_VALUE) {
            const DWORD error = ::GetLastError();
            if (error == ERROR_FILE_EXISTS) {
                continue;
            }
            throw_path_error("Failed to exclusively create temporary output",
                             candidate, impl_->destination, error);
        }

        impl_->file_handle = handle;
        impl_->temporary = candidate;
        return;
    }
#elif defined(__linux__)
    impl_->directory_fd =
        ::open(impl_->parent.c_str(), O_RDONLY | O_DIRECTORY | O_CLOEXEC);
    if (impl_->directory_fd == -1) {
        const int error = errno;
        throw_path_error("Failed to open output parent directory",
                         impl_->parent, impl_->destination,
                         static_cast<unsigned long>(error));
    }

    if (!force) {
        struct stat status {};
        if (::fstatat(impl_->directory_fd,
                      impl_->destination_name.c_str(), &status,
                      AT_SYMLINK_NOFOLLOW) == 0) {
            throw std::runtime_error("Output destination already exists: '" +
                                     display_path(impl_->destination) + "'");
        }
        const int error = errno;
        if (error != ENOENT) {
            throw_path_error("Failed to inspect output destination",
                             impl_->parent / impl_->destination_name,
                             impl_->destination,
                             static_cast<unsigned long>(error));
        }
    }

    const std::uint64_t anonymous_counter =
        temporary_counter.fetch_add(1, std::memory_order_relaxed);
    impl_->temporary_name =
        make_candidate_name(impl_->destination_name, anonymous_counter,
                            "anonymous");
    impl_->temporary = impl_->parent / impl_->temporary_name;
    impl_->file_fd =
        ::openat(impl_->directory_fd, ".",
                 O_RDWR | O_TMPFILE | O_CLOEXEC, 0666);
    if (impl_->file_fd >= 0) {
        impl_->anonymous_tmpfile = true;
        return;
    }
    const int anonymous_error = errno;
    if (!tmpfile_is_unsupported(anonymous_error)) {
        throw_path_error("Failed to create anonymous Linux temporary output",
                         impl_->parent, impl_->destination,
                         static_cast<unsigned long>(anonymous_error));
    }

    for (std::uint64_t attempt = 0; attempt < kMaximumCreationAttempts;
         ++attempt) {
        const std::uint64_t counter =
            temporary_counter.fetch_add(1, std::memory_order_relaxed);
        impl_->temporary_name =
            make_candidate_name(impl_->destination_name, counter);
        impl_->temporary = impl_->parent / impl_->temporary_name;

        const int descriptor =
            ::openat(impl_->directory_fd, impl_->temporary_name.c_str(),
                     O_RDWR | O_CREAT | O_EXCL | O_CLOEXEC | O_NOFOLLOW,
                     0666);
        if (descriptor == -1) {
            const int error = errno;
            if (error == EEXIST) {
                continue;
            }
            throw_path_error("Failed to exclusively create temporary output",
                             impl_->temporary, impl_->destination,
                             static_cast<unsigned long>(error));
        }

        impl_->file_fd = descriptor;
        impl_->temporary_linked = true;
        return;
    }
#endif

    throw std::runtime_error(
        "Failed to find a unique temporary output name beside destination '" +
        display_path(impl_->destination) + "'");
}

OutputTransaction::~OutputTransaction() noexcept = default;

const std::filesystem::path& OutputTransaction::temporary_path() const noexcept {
    return impl_->temporary;
}

int OutputTransaction::take_sink_fd() {
    if (impl_->sink_fd_taken) {
        throw std::logic_error(
            "The transactional output sink descriptor was already taken");
    }

#ifdef _WIN32
    HANDLE duplicate = INVALID_HANDLE_VALUE;
    HANDLE process = ::GetCurrentProcess();
    if (::DuplicateHandle(process, impl_->file_handle, process, &duplicate,
                          0, FALSE, DUPLICATE_SAME_ACCESS) == 0) {
        const DWORD error = ::GetLastError();
        throw_path_error("Failed to duplicate temporary output identity",
                         impl_->temporary, impl_->destination, error);
    }

    const int descriptor = ::_open_osfhandle(
        reinterpret_cast<intptr_t>(duplicate), _O_BINARY | _O_RDWR);
    if (descriptor == -1) {
        const int conversion_error = errno;
        if (::CloseHandle(duplicate) == 0) {
            const DWORD close_error = ::GetLastError();
            throw std::runtime_error(
                "Failed to convert duplicated temporary output handle '" +
                display_path(impl_->temporary) + "' for destination '" +
                display_path(impl_->destination) + "': " +
                errno_message(conversion_error) +
                "; duplicate-handle cleanup also failed: " +
                system_message(close_error));
        }
        throw std::runtime_error(
            "Failed to convert duplicated temporary output handle '" +
            display_path(impl_->temporary) + "' for destination '" +
            display_path(impl_->destination) + "': " +
            errno_message(conversion_error));
    }
#elif defined(__linux__)
    const int descriptor =
        ::fcntl(impl_->file_fd, F_DUPFD_CLOEXEC, 0);
    if (descriptor == -1) {
        const int error = errno;
        throw_path_error("Failed to duplicate temporary output identity",
                         impl_->temporary, impl_->destination,
                         static_cast<unsigned long>(error));
    }
#endif

    impl_->sink_fd_taken = true;
    return descriptor;
}

void OutputTransaction::commit() {
    if (impl_->committed) {
        return;
    }

#ifdef _WIN32
    if (::FlushFileBuffers(impl_->file_handle) == 0) {
        const DWORD error = ::GetLastError();
        throw_path_error("Failed to flush retained temporary output identity",
                         impl_->temporary, impl_->destination, error);
    }

    if (impl_->force) {
        verify_windows_path_identity(impl_->file_handle, impl_->temporary,
                                     impl_->destination);
        if (::MoveFileExW(impl_->temporary.c_str(),
                          impl_->destination.c_str(),
                          MOVEFILE_REPLACE_EXISTING |
                              MOVEFILE_WRITE_THROUGH) == 0) {
            const DWORD error = ::GetLastError();
            throw_path_error(
                "Failed to durably replace output from verified temporary identity",
                impl_->temporary, impl_->destination, error);
        }
        impl_->committed = true;
        return;
    }

    const std::filesystem::path::string_type& target =
        impl_->destination.native();
    if (target.size() >
        static_cast<std::size_t>(
            std::numeric_limits<DWORD>::max() / sizeof(wchar_t))) {
        throw std::runtime_error("Output destination path is too long for handle-based publication: '" +
                                 display_path(impl_->destination) + "'");
    }

    const std::size_t name_bytes = target.size() * sizeof(wchar_t);
    const std::size_t header_bytes = offsetof(FILE_RENAME_INFO, FileName);
    if (name_bytes > std::numeric_limits<std::size_t>::max() - header_bytes ||
        header_bytes + name_bytes >
            static_cast<std::size_t>(std::numeric_limits<DWORD>::max())) {
        throw std::runtime_error("Output destination rename information is too large: '" +
                                 display_path(impl_->destination) + "'");
    }

    const std::size_t information_bytes = header_bytes + name_bytes;
    const std::size_t aligned_bytes =
        std::max(information_bytes, sizeof(FILE_RENAME_INFO));
    const std::size_t alignment_units =
        aligned_bytes / sizeof(std::max_align_t) +
        (aligned_bytes % sizeof(std::max_align_t) == 0 ? 0 : 1);
    std::vector<std::max_align_t> storage(alignment_units);
    auto* rename_info = ::new (static_cast<void*>(storage.data()))
        FILE_RENAME_INFO{};
    rename_info->ReplaceIfExists = FALSE;
    rename_info->RootDirectory = nullptr;
    rename_info->FileNameLength = static_cast<DWORD>(name_bytes);
    if (name_bytes != 0) {
        auto* const filename_storage =
            reinterpret_cast<unsigned char*>(storage.data()) + header_bytes;
        std::memcpy(filename_storage, target.data(), name_bytes);
    }

    if (::SetFileInformationByHandle(
            impl_->file_handle, FileRenameInfo, rename_info,
            static_cast<DWORD>(information_bytes)) == 0) {
        const DWORD error = ::GetLastError();
        throw_path_error("Failed to publish retained temporary output identity",
                         impl_->temporary, impl_->destination, error);
    }
#elif defined(__linux__)
    if (!impl_->anonymous_tmpfile) {
        bool identity_matches = false;
        try {
            identity_matches = linux_named_identity_matches(
                impl_->file_fd, impl_->directory_fd,
                impl_->temporary_name, impl_->temporary,
                impl_->destination);
        } catch (...) {
            // If verification itself cannot establish ownership, pathname
            // cleanup could target a substituted entry. Retain only the fd.
            impl_->temporary_linked = false;
            throw;
        }
        if (!identity_matches) {
            impl_->temporary_linked = false;
            throw std::runtime_error(
                "Linked temporary output no longer names the retained file identity: '" +
                display_path(impl_->temporary) + "' for destination '" +
                display_path(impl_->destination) + "'");
        }
        if (!impl_->force) {
            if (::linkat(impl_->directory_fd,
                         impl_->temporary_name.c_str(),
                         impl_->directory_fd,
                         impl_->destination_name.c_str(), 0) != 0) {
                const int error = errno;
                throw_path_error(
                    "Failed to publish verified linked Linux temporary output",
                    impl_->temporary, impl_->destination,
                    static_cast<unsigned long>(error));
            }
            if (::unlinkat(impl_->directory_fd,
                           impl_->temporary_name.c_str(), 0) != 0) {
                const int error = errno;
                throw_path_error(
                    "Published output but failed to remove verified linked temporary output",
                    impl_->temporary, impl_->destination,
                    static_cast<unsigned long>(error));
            }
            impl_->temporary_linked = false;
        } else {
            if (::renameat(impl_->directory_fd,
                           impl_->temporary_name.c_str(),
                           impl_->directory_fd,
                           impl_->destination_name.c_str()) != 0) {
                const int error = errno;
                throw_path_error(
                    "Failed to atomically replace output from verified linked temporary output",
                    impl_->temporary, impl_->destination,
                    static_cast<unsigned long>(error));
            }
            impl_->temporary_linked = false;
        }
    } else if (!impl_->force) {
        const LinkIdentityResult result = link_anonymous_identity(
            impl_->file_fd, impl_->directory_fd,
            impl_->destination_name);
        if (!result.linked) {
            throw_link_identity_error(
                "Failed to publish anonymous Linux output identity",
                impl_->temporary, impl_->destination, result);
        }
    } else {
        bool staged = false;
        for (std::uint64_t attempt = 0;
             attempt < kMaximumCreationAttempts; ++attempt) {
            const std::uint64_t counter =
                temporary_counter.fetch_add(1, std::memory_order_relaxed);
            impl_->staging_name = make_candidate_name(
                impl_->destination_name, counter, "publish");
            const LinkIdentityResult result = link_anonymous_identity(
                impl_->file_fd, impl_->directory_fd,
                impl_->staging_name);
            if (result.linked) {
                staged = true;
                impl_->staging_linked = true;
                break;
            }
            if (result.error == EEXIST) {
                impl_->staging_name.clear();
                continue;
            }
            throw_link_identity_error(
                "Failed to stage anonymous Linux output identity",
                impl_->temporary,
                impl_->parent / impl_->staging_name,
                result);
        }
        if (!staged) {
            throw std::runtime_error(
                "Failed to find a unique Linux publication staging name beside destination '" +
                display_path(impl_->destination) + "'");
        }

        const std::filesystem::path staging_path =
            impl_->parent / impl_->staging_name;
        if (::renameat(impl_->directory_fd, impl_->staging_name.c_str(),
                       impl_->directory_fd,
                       impl_->destination_name.c_str()) != 0) {
            const int rename_error = errno;
            if (::unlinkat(impl_->directory_fd,
                           impl_->staging_name.c_str(), 0) != 0) {
                const int cleanup_error = errno;
                throw std::runtime_error(
                    "Failed to atomically replace destination '" +
                    display_path(impl_->destination) + "' from staging path '" +
                    display_path(impl_->parent / impl_->staging_name) +
                    "': " +
                    system_message(static_cast<unsigned long>(rename_error)) +
                    "; staging cleanup also failed: " +
                    system_message(static_cast<unsigned long>(cleanup_error)));
            }
            impl_->staging_name.clear();
            impl_->staging_linked = false;
            throw_path_error("Failed to atomically replace output from staging path",
                             staging_path, impl_->destination,
                             static_cast<unsigned long>(rename_error));
        }
        impl_->staging_name.clear();
        impl_->staging_linked = false;
    }
#endif

    impl_->committed = true;
}

}  // namespace ctb
````````

## `src/pipeline.cpp`

````````text
#include "convert_to_biallelic/pipeline.hpp"

#include <algorithm>
#include <atomic>
#include <condition_variable>
#include <exception>
#include <functional>
#include <limits>
#include <map>
#include <memory>
#include <mutex>
#include <ostream>
#include <stdexcept>
#include <string>
#include <system_error>
#include <thread>
#include <utility>
#include <vector>

#include "convert_to_biallelic/bounded_queue.hpp"
#include "convert_to_biallelic/converter.hpp"
#include "convert_to_biallelic/progress.hpp"

namespace ctb {
namespace {

constexpr std::uint64_t kMinimumMemoryLimit =
    64ULL * 1024ULL * 1024ULL;

class OverageReporter {
public:
    explicit OverageReporter(std::ostream& diagnostics)
        : diagnostics_(diagnostics) {}

    void warn_for_input(std::uint64_t line_number) {
        warn("input line " + std::to_string(line_number));
    }

    void warn_for_sequence(std::uint64_t sequence) {
        warn("pipeline sequence " + std::to_string(sequence));
    }

private:
    void warn(const std::string& subject) {
        bool expected = false;
        if (!emitted_.compare_exchange_strong(
                expected, true, std::memory_order_acq_rel,
                std::memory_order_acquire)) {
            return;
        }

        std::lock_guard<std::mutex> lock(mutex_);
        diagnostics_ << "Warning: " << subject
                     << " requires the one permitted exceptional "
                        "tracked-memory overage"
                     << '\n'
                     << std::flush;
        if (!diagnostics_) {
            throw std::runtime_error(
                "Failed to write pipeline diagnostics");
        }
    }

    std::ostream& diagnostics_;
    std::mutex mutex_;
    std::atomic<bool> emitted_{false};
};

std::uint64_t checked_size(std::size_t value, const char* description) {
    if (value > static_cast<std::size_t>(
                    std::numeric_limits<std::uint64_t>::max())) {
        throw std::overflow_error(std::string(description) + " is too large");
    }
    return static_cast<std::uint64_t>(value);
}

std::uint64_t checked_add(std::uint64_t left,
                          std::uint64_t right,
                          const char* description) {
    if (right > std::numeric_limits<std::uint64_t>::max() - left) {
        throw std::overflow_error(std::string(description) + " overflow");
    }
    return left + right;
}

std::uint64_t checked_multiply(std::uint64_t left,
                               std::uint64_t right,
                               const char* description) {
    if (left != 0 &&
        right > std::numeric_limits<std::uint64_t>::max() / left) {
        throw std::overflow_error(std::string(description) + " overflow");
    }
    return left * right;
}

std::uint64_t string_storage_bytes(const std::string& value) {
    return checked_add(checked_size(value.capacity(), "string capacity"), 1,
                       "string storage");
}

std::uint64_t vector_storage_bytes(
    const std::vector<std::string>& records) {
    return checked_multiply(checked_size(records.capacity(),
                                         "record vector capacity"),
                            sizeof(std::string),
                            "record vector storage");
}

std::uint64_t work_storage_bytes(const RawWorkChunk& chunk) {
    std::uint64_t bytes = vector_storage_bytes(chunk.records);
    for (const std::string& record : chunk.records) {
        bytes = checked_add(bytes, string_storage_bytes(record),
                            "work chunk storage");
    }
    return bytes;
}

std::uint64_t result_storage_bytes(const RawResultChunk& chunk) {
    return string_storage_bytes(chunk.bytes);
}

std::size_t planned_vector_capacity(std::size_t current,
                                    std::size_t required,
                                    std::size_t target) {
    if (required <= current) {
        return current;
    }
    if (required > target) {
        throw std::length_error("Work chunk record target was exceeded");
    }

    std::size_t capacity = current == 0 ? 1 : current;
    while (capacity < required) {
        if (capacity >= target || capacity > target - capacity) {
            capacity = target;
        } else {
            capacity *= 2;
        }
    }
    return capacity;
}

std::uint64_t projected_work_storage(const WorkChunk& chunk,
                                     const std::string& next_record,
                                     std::size_t target_records) {
    if (chunk.data.records.size() ==
        std::numeric_limits<std::size_t>::max()) {
        throw std::overflow_error("Work chunk record count overflow");
    }

    const std::size_t required = chunk.data.records.size() + 1;
    const std::size_t planned = planned_vector_capacity(
        chunk.data.records.capacity(), required, target_records);
    const std::uint64_t added_slots = checked_size(
        planned - chunk.data.records.capacity(),
        "additional record vector capacity");
    const std::uint64_t added_vector_bytes = checked_multiply(
        added_slots, sizeof(std::string), "record vector growth");

    return checked_add(
        checked_add(chunk.permit.bytes(), added_vector_bytes,
                    "projected work chunk storage"),
        string_storage_bytes(next_record), "projected work chunk storage");
}

void publish_memory(ProgressCounters& counters,
                    MemoryBudget& budget,
                    std::mutex& publication_mutex) {
    // Serialize the read-and-publish pair so a delayed publisher cannot store
    // a snapshot older than one already published by another pipeline thread.
    std::lock_guard<std::mutex> lock(publication_mutex);
    counters.tracked_memory.store(budget.current_bytes(),
                                  std::memory_order_relaxed);
}

void resize_result_permit(
    MemoryPermit& permit,
    std::uint64_t bytes,
    std::uint64_t sequence,
    std::uint64_t tracked_allowance,
    const std::atomic<std::uint64_t>& next_written_sequence,
    const std::atomic<bool>& pipeline_cancelled,
    ProgressCounters& counters,
    MemoryBudget& budget,
    std::mutex& coordination_mutex,
    std::condition_variable& memory_changed,
    OverageReporter& overage_reporter);

void append_work_record(WorkChunk& chunk,
                        std::string& record,
                        std::uint64_t line_number,
                        std::size_t target_records,
                        std::uint64_t tracked_allowance,
                        const std::atomic<std::uint64_t>&
                            next_written_sequence,
                        const std::atomic<bool>& pipeline_cancelled,
                        ProgressCounters& counters,
                        MemoryBudget& budget,
                        std::mutex& publication_mutex,
                        std::condition_variable& memory_changed,
                        OverageReporter& overage_reporter) {
    const std::size_t required = chunk.data.records.size() + 1;
    const std::size_t planned = planned_vector_capacity(
        chunk.data.records.capacity(), required, target_records);
    const std::uint64_t planned_bytes = projected_work_storage(
        chunk, record, target_records);

    // vector::reserve keeps the old allocation alive while obtaining the new
    // one. Admit both allocations, plus the incoming record, until reserve
    // returns and the implementation-selected capacity can be reconciled.
    std::uint64_t reserve_time_bytes = planned_bytes;
    if (planned > chunk.data.records.capacity()) {
        reserve_time_bytes = checked_add(
            reserve_time_bytes, vector_storage_bytes(chunk.data.records),
            "reserve-time work chunk storage");
    }
    resize_result_permit(
        chunk.permit, reserve_time_bytes, chunk.data.sequence,
        tracked_allowance, next_written_sequence, pipeline_cancelled,
        counters, budget, publication_mutex, memory_changed,
        overage_reporter);
    if (planned > chunk.data.records.capacity()) {
        chunk.data.records.reserve(planned);
    }

    // The standard permits reserve() to choose a larger capacity than the
    // request. Reconcile that implementation-selected capacity before the
    // record string itself is moved into the vector.
    const std::uint64_t before_move = checked_add(
        work_storage_bytes(chunk.data), string_storage_bytes(record),
        "work chunk storage before record move");
    resize_result_permit(
        chunk.permit, before_move, chunk.data.sequence,
        tracked_allowance, next_written_sequence, pipeline_cancelled,
        counters, budget, publication_mutex, memory_changed,
        overage_reporter);

    if (chunk.data.records.empty()) {
        chunk.data.first_line_number = line_number;
    }
    chunk.data.records.push_back(std::move(record));

    // A nothrow string move normally transfers its capacity. Reconcile the
    // actual capacity so the permit remains exact even for an SSO move.
    resize_result_permit(
        chunk.permit, work_storage_bytes(chunk.data), chunk.data.sequence,
        tracked_allowance, next_written_sequence, pipeline_cancelled,
        counters, budget, publication_mutex, memory_changed,
        overage_reporter);
}

void checked_atomic_add(std::atomic<std::uint64_t>& counter,
                        std::uint64_t additional,
                        const char* description) {
    std::uint64_t current = counter.load(std::memory_order_relaxed);
    for (;;) {
        if (additional >
            std::numeric_limits<std::uint64_t>::max() - current) {
            throw std::overflow_error(std::string(description) + " overflow");
        }
        if (counter.compare_exchange_weak(
                current, current + additional, std::memory_order_relaxed,
                std::memory_order_relaxed)) {
            return;
        }
    }
}

std::uint64_t physical_line_number(std::uint64_t first,
                                   std::size_t offset) {
    const std::uint64_t converted_offset =
        checked_size(offset, "record line offset");
    return checked_add(first, converted_offset, "physical line number");
}

void resize_result_permit(
    MemoryPermit& permit,
    std::uint64_t bytes,
    std::uint64_t sequence,
    std::uint64_t tracked_allowance,
    const std::atomic<std::uint64_t>& next_written_sequence,
    const std::atomic<bool>& pipeline_cancelled,
    ProgressCounters& counters,
    MemoryBudget& budget,
    std::mutex& coordination_mutex,
    std::condition_variable& memory_changed,
    OverageReporter& overage_reporter) {
    if (permit.bytes() == bytes) {
        return;
    }

    std::unique_lock<std::mutex> lock(coordination_mutex);
    for (;;) {
        if (pipeline_cancelled.load(std::memory_order_acquire)) {
            throw std::runtime_error("Pipeline was cancelled");
        }

        const bool shrinking = bytes <= permit.bytes();
        const std::uint64_t current = budget.current_bytes();
        const std::uint64_t additional =
            shrinking ? 0 : bytes - permit.bytes();
        const bool fits_normally =
            shrinking ||
            (current <= tracked_allowance &&
             additional <= tracked_allowance - current);
        const std::uint64_t next =
            next_written_sequence.load(std::memory_order_acquire);
        if (next > sequence) {
            throw std::logic_error(
                "Result sequence advanced before conversion completed");
        }
        const bool is_next_sequence = next == sequence;

        if (fits_normally || is_next_sequence) {
            // A later sequence may use only normal capacity. The next result
            // required by the writer may claim or continue the sole overage,
            // so an over-budget result can never wait in the reorder map while
            // blocking an earlier sequence from finishing.
            permit.resize(bytes, is_next_sequence);
            const std::uint64_t current_after = budget.current_bytes();
            counters.tracked_memory.store(current_after,
                                          std::memory_order_relaxed);
            if (!shrinking && current_after > tracked_allowance) {
                overage_reporter.warn_for_sequence(sequence);
            }
            lock.unlock();
            memory_changed.notify_all();
            return;
        }

        memory_changed.wait(lock);
    }
}

ResultChunk convert_chunk(WorkChunk& work,
                          const AnnotationIndex& annotation,
                          std::uint64_t tracked_allowance,
                          const std::atomic<std::uint64_t>&
                              next_written_sequence,
                          const std::atomic<bool>& pipeline_cancelled,
                          ProgressCounters& counters,
                          MemoryBudget& budget,
                          std::mutex& memory_publication_mutex,
                          std::condition_variable& memory_changed,
                          OverageReporter& overage_reporter) {
    const std::uint64_t input_records =
        checked_size(work.data.records.size(), "input record count");
    const std::uint64_t input_storage = work_storage_bytes(work.data);
    const std::uint64_t sequence = work.data.sequence;
    resize_result_permit(
        work.permit, input_storage, sequence, tracked_allowance,
        next_written_sequence, pipeline_cancelled, counters, budget,
        memory_publication_mutex, memory_changed, overage_reporter);

    RawResultChunk converted_chunk;
    converted_chunk.sequence = sequence;
    converted_chunk.input_records = input_records;

    resize_result_permit(
        work.permit,
        checked_add(input_storage, result_storage_bytes(converted_chunk),
                    "combined input and result storage"),
        sequence, tracked_allowance, next_written_sequence,
        pipeline_cancelled, counters, budget, memory_publication_mutex,
        memory_changed, overage_reporter);

    const std::function<void(std::size_t, std::size_t)> before_reserve =
        [&](std::size_t planned_output_bytes,
            std::size_t planned_scratch_bytes) {
            const std::uint64_t planned_result = checked_size(
                planned_output_bytes, "planned result storage");
            const std::uint64_t planned_scratch = checked_size(
                planned_scratch_bytes, "planned converter scratch size");
            resize_result_permit(
                work.permit,
                checked_add(
                    checked_add(input_storage, planned_result,
                                "combined input and result storage"),
                    planned_scratch,
                    "combined input, result, and converter scratch storage"),
                sequence, tracked_allowance, next_written_sequence,
                pipeline_cancelled, counters, budget,
                memory_publication_mutex, memory_changed,
                overage_reporter);
        };

    for (std::size_t index = 0; index < work.data.records.size(); ++index) {
        append_converted_record(
            work.data.records[index], annotation,
            physical_line_number(work.data.first_line_number, index),
            converted_chunk.bytes, converted_chunk.output_records,
            before_reserve);
    }

    // Release source storage before shrinking its reservation away. The
    // remaining permit then describes only the result string capacity.
    work.data = RawWorkChunk{};
    ResultChunk result;
    result.data = std::move(converted_chunk);
    result.permit = std::move(work.permit);
    resize_result_permit(
        result.permit, result_storage_bytes(result.data), sequence,
        tracked_allowance, next_written_sequence, pipeline_cancelled, counters,
        budget, memory_publication_mutex, memory_changed,
        overage_reporter);
    checked_atomic_add(counters.input_records, input_records,
                       "pipeline input record counter");
    return result;
}

class PipelineState {
public:
    bool capture_failure(std::exception_ptr error) noexcept {
        bool first = false;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            if (!first_error_) {
                first_error_ = error;
                cancelled_ = true;
                first = true;
            }
        }
        changed_.notify_all();
        return first;
    }

    std::exception_ptr first_error() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return first_error_;
    }

    bool cancelled() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return cancelled_;
    }

    bool wait_until_written(std::uint64_t count) {
        std::unique_lock<std::mutex> lock(mutex_);
        changed_.wait(lock, [this, count] {
            return cancelled_ || written_chunks_ >= count;
        });
        return !cancelled_;
    }

    void mark_written(std::uint64_t count) {
        {
            std::lock_guard<std::mutex> lock(mutex_);
            if (count < written_chunks_) {
                throw std::logic_error(
                    "Written chunk sequence moved backwards");
            }
            written_chunks_ = count;
        }
        changed_.notify_all();
    }

private:
    mutable std::mutex mutex_;
    std::condition_variable changed_;
    std::exception_ptr first_error_;
    std::uint64_t written_chunks_ = 0;
    bool cancelled_ = false;
};

class ThreadCompletion {
public:
    void mark_exited() noexcept {
        try {
            std::unique_lock<std::mutex> lock(mutex_);
            exited_ = true;
            // Keep the completion mutex locked until the native thread has
            // actually exited. A fallback waiter therefore cannot detach on
            // an about-to-exit signal while wrapper epilogue code is active.
            std::notify_all_at_thread_exit(changed_, std::move(lock));
        } catch (...) {
            // Without publishing completion, a failed join cannot safely
            // recover while pipeline threads still reference stack state.
            std::terminate();
        }
    }

    void wait_until_exited() {
        std::unique_lock<std::mutex> lock(mutex_);
        changed_.wait(lock, [this] { return exited_; });
    }

private:
    std::mutex mutex_;
    std::condition_variable changed_;
    bool exited_ = false;
};

class WorkerLifecycle {
public:
    void add_starting_worker() {
        std::lock_guard<std::mutex> lock(mutex_);
        if (spawning_finished_) {
            throw std::logic_error(
                "Cannot add a worker after spawning has finished");
        }
        if (active_workers_ ==
            std::numeric_limits<std::size_t>::max()) {
            throw std::overflow_error("Active worker count overflow");
        }
        ++active_workers_;
    }

    void rollback_unstarted_worker() {
        std::lock_guard<std::mutex> lock(mutex_);
        if (active_workers_ == 0) {
            throw std::logic_error(
                "Cannot roll back an absent starting worker");
        }
        --active_workers_;
    }

    bool worker_exited_and_should_close() {
        std::lock_guard<std::mutex> lock(mutex_);
        if (active_workers_ == 0) {
            throw std::logic_error(
                "Active worker count underflow");
        }
        --active_workers_;
        return claim_result_close_locked();
    }

    bool finish_spawning_and_should_close() {
        std::lock_guard<std::mutex> lock(mutex_);
        spawning_finished_ = true;
        return claim_result_close_locked();
    }

private:
    bool claim_result_close_locked() noexcept {
        if (!spawning_finished_ || active_workers_ != 0 ||
            result_close_claimed_) {
            return false;
        }
        result_close_claimed_ = true;
        return true;
    }

    std::mutex mutex_;
    std::size_t active_workers_ = 0;
    bool spawning_finished_ = false;
    bool result_close_claimed_ = false;
};

std::size_t queue_capacity(std::size_t conversion_workers) noexcept {
    const std::size_t maximum = std::numeric_limits<std::size_t>::max();
    if (conversion_workers > maximum / 2) {
        return maximum;
    }
    return std::max<std::size_t>(1, conversion_workers * 2);
}

std::uint64_t validate_and_calculate_allowance(
    const AnnotationIndex& annotation,
    const PipelineOptions& options) {
    if (options.threads.conversion_workers == 0) {
        throw std::invalid_argument(
            "Pipeline requires at least one conversion worker");
    }
    if (options.target_records_per_chunk == 0) {
        throw std::invalid_argument(
            "Pipeline record target must be positive");
    }
    if (options.target_bytes_per_chunk == 0) {
        throw std::invalid_argument(
            "Pipeline byte target must be positive");
    }
    if (options.memory_limit_bytes < kMinimumMemoryLimit) {
        throw std::invalid_argument(
            "Pipeline memory limit must be at least 64 MiB");
    }

    const std::uint64_t reserve = options.memory_limit_bytes / 10;
    const std::uint64_t after_reserve =
        options.memory_limit_bytes - reserve;
    if (annotation.estimated_bytes() >= after_reserve) {
        throw std::invalid_argument(
            "Annotation estimate plus the 10% reserve must be below the memory limit");
    }
    return after_reserve - annotation.estimated_bytes();
}

bool read_physical_line(InputSource& input,
                        std::string& line,
                        std::uint64_t& line_number) {
    if (!input.getline(line)) {
        return false;
    }
    if (line_number == std::numeric_limits<std::uint64_t>::max()) {
        throw std::overflow_error("Physical input line number overflow");
    }
    ++line_number;
    return true;
}

}  // namespace

PipelineStats run_pipeline(InputSource& input,
                           OutputSink& output,
                           const AnnotationIndex& annotation,
                           const PipelineOptions& options,
                           std::ostream& progress,
                           std::ostream& diagnostics) {
    if (std::addressof(progress) == std::addressof(diagnostics) ||
        progress.rdbuf() == diagnostics.rdbuf()) {
        throw std::invalid_argument(
            "Progress and diagnostics streams must not share an object or "
            "stream buffer");
    }

    const std::uint64_t tracked_allowance =
        validate_and_calculate_allowance(annotation, options);

    // Header I/O is deliberately complete before any conversion, writer, or
    // reporter thread is started. The first non-header line is retained for
    // the streaming reader below.
    std::uint64_t line_number = 0;
    std::uint64_t header_output_bytes = 0;
    std::string line;
    std::string first_data_line;
    std::uint64_t first_data_line_number = 0;
    bool has_first_data_line = false;

    while (read_physical_line(input, line, line_number)) {
        if (!line.empty() && line.front() == '#') {
            const auto converted = convert_header(line);
            if (converted) {
                const std::uint64_t updated_header_bytes = checked_add(
                    header_output_bytes,
                    checked_size(converted->size(), "header output size"),
                    "header output byte counter");
                output.write(*converted);
                header_output_bytes = updated_header_bytes;
            }
            continue;
        }

        first_data_line = std::move(line);
        first_data_line_number = line_number;
        has_first_data_line = true;
        break;
    }

    ProgressCounters counters;
    MemoryBudget memory_budget(tracked_allowance);
    const std::size_t capacity =
        queue_capacity(options.threads.conversion_workers);
    BoundedQueue<WorkChunk> work_queue(capacity);
    BoundedQueue<ResultChunk> result_queue(capacity);
    PipelineState state;
    std::mutex memory_publication_mutex;
    std::condition_variable memory_changed;
    std::atomic<std::uint64_t> submitted_chunks{0};
    std::atomic<std::uint64_t> next_written_sequence{0};
    std::atomic<std::uint64_t> output_bytes{header_output_bytes};
    std::atomic<bool> pipeline_cancelled{false};
    WorkerLifecycle worker_lifecycle;
    OverageReporter overage_reporter(diagnostics);

    auto record_failure = [&](std::exception_ptr error) noexcept {
        if (error && state.capture_failure(error)) {
            // Do not hold the failure-state mutex while entering any memory or
            // queue operation. Each cancellation path wakes its waiters.
            pipeline_cancelled.store(true, std::memory_order_release);
            memory_budget.cancel();
            memory_changed.notify_all();
            work_queue.cancel();
            result_queue.cancel();
        }
    };

    ProgressReporter reporter(
        counters, options.progress_interval, progress, options.quiet,
        [&](std::exception_ptr error) noexcept { record_failure(error); });

    auto writer_body = [&] {
        try {
            std::map<std::uint64_t, ResultChunk> pending;
            std::uint64_t next_sequence = 0;
            ResultChunk incoming;

            while (result_queue.pop(incoming)) {
                const std::uint64_t sequence = incoming.data.sequence;
                if (sequence < next_sequence) {
                    throw std::runtime_error(
                        "Duplicate result sequence " +
                        std::to_string(sequence));
                }

                const auto insertion =
                    pending.emplace(sequence, std::move(incoming));
                if (!insertion.second) {
                    throw std::runtime_error(
                        "Duplicate result sequence " +
                        std::to_string(sequence));
                }

                for (;;) {
                    auto ready = pending.find(next_sequence);
                    if (ready == pending.end()) {
                        break;
                    }

                    const std::uint64_t output_record_total = checked_add(
                        counters.output_records.load(
                            std::memory_order_relaxed),
                        ready->second.data.output_records,
                        "pipeline output record counter");
                    const std::uint64_t output_byte_total = checked_add(
                        output_bytes.load(std::memory_order_relaxed),
                        checked_size(ready->second.data.bytes.size(),
                                     "result output size"),
                        "pipeline output byte counter");

                    output.write(ready->second.data.bytes);
                    counters.output_records.store(output_record_total,
                                                  std::memory_order_relaxed);
                    output_bytes.store(output_byte_total,
                                       std::memory_order_relaxed);

                    if (next_sequence ==
                        std::numeric_limits<std::uint64_t>::max()) {
                        throw std::overflow_error(
                            "Writer result sequence overflow");
                    }
                    ++next_sequence;
                    pending.erase(ready);
                    publish_memory(counters, memory_budget,
                                   memory_publication_mutex);
                    next_written_sequence.store(next_sequence,
                                                std::memory_order_release);
                    memory_changed.notify_all();
                    state.mark_written(next_sequence);
                }
            }

            const std::uint64_t expected =
                submitted_chunks.load(std::memory_order_acquire);
            if (!pending.empty() || next_sequence != expected) {
                throw std::runtime_error(
                    "Result queue closed with a missing sequence: expected " +
                    std::to_string(next_sequence) + " of " +
                    std::to_string(expected));
            }
        } catch (...) {
            record_failure(std::current_exception());
        }
    };

    auto worker_body = [&] {
        try {
            WorkChunk work;
            while (work_queue.pop(work)) {
                ResultChunk result = convert_chunk(
                    work, annotation, tracked_allowance,
                    next_written_sequence, pipeline_cancelled, counters,
                    memory_budget, memory_publication_mutex, memory_changed,
                    overage_reporter);
                result_queue.push(std::move(result));
            }
        } catch (...) {
            record_failure(std::current_exception());
        }

        bool should_close_results = false;
        try {
            should_close_results =
                worker_lifecycle.worker_exited_and_should_close();
        } catch (...) {
            record_failure(std::current_exception());
        }

        if (should_close_results) {
            try {
                result_queue.close();
            } catch (...) {
                record_failure(std::current_exception());
            }
        }
    };

    std::thread writer_thread;
    std::vector<std::thread> worker_threads;
    std::shared_ptr<ThreadCompletion> writer_completion;
    std::vector<std::shared_ptr<ThreadCompletion>> worker_completions;

    auto join_safely = [&](
                           std::thread& thread,
                           const std::shared_ptr<ThreadCompletion>& completion)
        noexcept {
        if (!thread.joinable()) {
            return;
        }
        if (thread.get_id() == std::this_thread::get_id() || !completion) {
            // run_pipeline owns and joins these threads only from its reader
            // context. Detaching an active self-thread would invalidate every
            // reference capture, so fail closed if that invariant is broken.
            std::terminate();
        }

        try {
            thread.join();
            return;
        } catch (const std::system_error&) {
            record_failure(std::current_exception());
        } catch (...) {
            record_failure(std::current_exception());
        }

        try {
            completion->wait_until_exited();
        } catch (...) {
            record_failure(std::current_exception());
            // Stack-captured pipeline state must outlive every active thread.
            // A wait failure therefore cannot fall through to active detach.
            std::terminate();
        }

        // Once the wrapper has published completion, none of its stack
        // references can be used again. Retry join, then detach only the
        // already-exited native thread as a last-resort handle cleanup.
        try {
            thread.join();
            return;
        } catch (const std::system_error&) {
            record_failure(std::current_exception());
        } catch (...) {
            record_failure(std::current_exception());
        }

        if (thread.joinable()) {
            try {
                thread.detach();
            } catch (...) {
                record_failure(std::current_exception());
                std::terminate();
            }
        }
    };

    auto finish_worker_spawning = [&]() noexcept {
        try {
            if (worker_lifecycle.finish_spawning_and_should_close()) {
                result_queue.close();
            }
        } catch (...) {
            record_failure(std::current_exception());
        }
    };

    try {
        reporter.start();
        writer_completion = std::make_shared<ThreadCompletion>();
        writer_thread = std::thread(
            [&, completion = writer_completion]() noexcept {
                try {
                    writer_body();
                } catch (...) {
                    record_failure(std::current_exception());
                }
                completion->mark_exited();
            });
        worker_threads.reserve(options.threads.conversion_workers);
        worker_completions.reserve(options.threads.conversion_workers);
        for (std::size_t remaining =
                 options.threads.conversion_workers;
             remaining != 0; --remaining) {
            if (state.cancelled()) {
                break;
            }

            auto completion = std::make_shared<ThreadCompletion>();
            worker_completions.push_back(completion);
            try {
                worker_lifecycle.add_starting_worker();
            } catch (...) {
                worker_completions.pop_back();
                throw;
            }
            try {
                worker_threads.emplace_back(
                    [&, completion = std::move(completion)]() noexcept {
                        try {
                            worker_body();
                        } catch (...) {
                            record_failure(std::current_exception());
                        }
                        completion->mark_exited();
                    });
            } catch (...) {
                const std::exception_ptr creation_error =
                    std::current_exception();
                worker_completions.pop_back();
                record_failure(creation_error);
                try {
                    worker_lifecycle.rollback_unstarted_worker();
                } catch (...) {
                    record_failure(std::current_exception());
                }
                throw;
            }
        }
        finish_worker_spawning();
        if (state.cancelled()) {
            throw std::runtime_error(
                "Pipeline was cancelled during worker creation");
        }

        WorkChunk chunk;
        std::uint64_t chunk_input_bytes = 0;
        std::uint64_t next_sequence = 0;
        const std::uint64_t in_flight_limit =
            checked_size(capacity, "in-flight chunk limit");

        auto begin_chunk = [&] {
            if (next_sequence ==
                std::numeric_limits<std::uint64_t>::max()) {
                throw std::overflow_error("Reader chunk sequence overflow");
            }
            if (next_sequence >= in_flight_limit) {
                const std::uint64_t minimum_written =
                    next_sequence - in_flight_limit + 1;
                if (!state.wait_until_written(minimum_written)) {
                    throw std::runtime_error("Pipeline was cancelled");
                }
            }
            chunk = WorkChunk{};
            chunk.data.sequence = next_sequence;
            chunk.permit = memory_budget.acquire(0, false);
            publish_memory(counters, memory_budget,
                           memory_publication_mutex);
        };

        auto submit_chunk = [&] {
            if (chunk.data.records.empty()) {
                return;
            }
            work_queue.push(std::move(chunk));
            ++next_sequence;
            submitted_chunks.store(next_sequence, std::memory_order_release);
            chunk = WorkChunk{};
            chunk_input_bytes = 0;
        };

        auto process_data_line = [&](std::string& record,
                                     std::uint64_t record_line_number) {
            if (!record.empty() && record.front() == '#') {
                throw std::runtime_error(
                    "Input line " + std::to_string(record_line_number) +
                    ": header line encountered after data began");
            }

            const std::uint64_t record_bytes =
                checked_size(record.size(), "input record size");
            if (chunk.permit.bytes() == 0 && chunk.data.records.empty()) {
                begin_chunk();
            }

            std::uint64_t projected = projected_work_storage(
                chunk, record, options.target_records_per_chunk);
            const bool exceeds_byte_target =
                record_bytes > options.target_bytes_per_chunk ||
                chunk_input_bytes >
                    options.target_bytes_per_chunk -
                        std::min(record_bytes,
                                 options.target_bytes_per_chunk);
            const bool exceeds_memory_target =
                projected > tracked_allowance;

            if (!chunk.data.records.empty() &&
                (exceeds_byte_target || exceeds_memory_target)) {
                submit_chunk();
                begin_chunk();
                projected = projected_work_storage(
                    chunk, record, options.target_records_per_chunk);
            }

            const bool single_record_overage =
                chunk.data.records.empty() && projected > tracked_allowance;
            if (single_record_overage) {
                // An input overage must never sit behind older work while it
                // owns the sole overage slot. Drain all prior sequences first,
                // then the worker that pops this chunk can grow the same permit
                // without deadlocking other workers.
                if (!state.wait_until_written(next_sequence)) {
                    throw std::runtime_error("Pipeline was cancelled");
                }
                overage_reporter.warn_for_input(record_line_number);
            }

            append_work_record(
                chunk, record, record_line_number,
                options.target_records_per_chunk, tracked_allowance,
                next_written_sequence, pipeline_cancelled, counters,
                memory_budget, memory_publication_mutex, memory_changed,
                overage_reporter);
            chunk_input_bytes = checked_add(
                chunk_input_bytes, record_bytes, "chunk input byte count");

            if (chunk.data.records.size() >=
                    options.target_records_per_chunk ||
                chunk_input_bytes >= options.target_bytes_per_chunk ||
                single_record_overage) {
                submit_chunk();
            }
        };

        if (has_first_data_line) {
            process_data_line(first_data_line, first_data_line_number);
        }
        while (read_physical_line(input, line, line_number)) {
            process_data_line(line, line_number);
        }
        submit_chunk();
        work_queue.close();
    } catch (...) {
        record_failure(std::current_exception());
    }
    finish_worker_spawning();

    for (std::size_t index = 0; index < worker_threads.size(); ++index) {
        join_safely(worker_threads[index], worker_completions[index]);
    }
    join_safely(writer_thread, writer_completion);

    if (const std::exception_ptr error = state.first_error()) {
        reporter.stop_without_summary();
        std::rethrow_exception(error);
    }

    try {
        publish_memory(counters, memory_budget, memory_publication_mutex);
        PipelineStats stats;
        stats.input_records =
            counters.input_records.load(std::memory_order_relaxed);
        stats.output_records =
            counters.output_records.load(std::memory_order_relaxed);
        stats.output_bytes = output_bytes.load(std::memory_order_relaxed);
        stats.peak_tracked_bytes = memory_budget.peak_bytes();
        stats.elapsed = reporter.finish();
        return stats;
    } catch (...) {
        reporter.stop_without_summary();
        throw;
    }
}

}  // namespace ctb
````````

## `src/progress.cpp`

````````text
#include "convert_to_biallelic/progress.hpp"

#include <algorithm>
#include <iomanip>
#include <limits>
#include <ostream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <system_error>
#include <utility>

#include "convert_to_biallelic/types.hpp"

namespace ctb {
namespace {

constexpr std::uint64_t kBytesPerMebibyte = 1024ULL * 1024ULL;

std::string format_elapsed(
    std::chrono::steady_clock::duration duration) {
    using Seconds = std::chrono::seconds;
    const auto elapsed = std::max(Seconds::zero(),
                                  std::chrono::duration_cast<Seconds>(duration));
    const auto total_seconds = elapsed.count();
    const auto hours = total_seconds / 3600;
    const auto minutes = (total_seconds / 60) % 60;
    const auto seconds = total_seconds % 60;

    std::ostringstream formatted;
    formatted << std::setfill('0') << std::setw(2) << hours << ':'
              << std::setw(2) << minutes << ':' << std::setw(2) << seconds;
    return formatted.str();
}

std::uint64_t records_per_second(
    std::uint64_t records,
    std::chrono::steady_clock::duration duration) noexcept {
    const long double seconds =
        std::chrono::duration<long double>(duration).count();
    if (records == 0 || seconds <= 0.0L) {
        return 0;
    }

    const long double rate = static_cast<long double>(records) / seconds;
    const long double maximum = static_cast<long double>(
        std::numeric_limits<std::uint64_t>::max());
    return rate >= maximum ? std::numeric_limits<std::uint64_t>::max()
                           : static_cast<std::uint64_t>(rate);
}

std::string periodic_line(
    std::chrono::steady_clock::duration elapsed,
    std::uint64_t input_records,
    std::uint64_t output_records,
    std::uint64_t recent_rate,
    std::uint64_t tracked_memory) {
    std::ostringstream line;
    line << '[' << format_elapsed(elapsed) << "] input=" << input_records
         << " output=" << output_records << " rate=" << recent_rate
         << " records/s memory=" << tracked_memory / kBytesPerMebibyte
         << " MiB";
    return line.str();
}

}  // namespace

std::string format_final_summary(const PipelineStats& stats) {
    std::ostringstream line;
    line << "Finished: input=" << stats.input_records
         << " output=" << stats.output_records
         << " elapsed=" << format_elapsed(stats.elapsed)
         << " average=" << records_per_second(stats.input_records,
                                                stats.elapsed)
         << " records/s peak_tracked_memory="
         << stats.peak_tracked_bytes / kBytesPerMebibyte << " MiB";
    return line.str();
}

ProgressReporter::ProgressReporter(ProgressCounters& counters,
                                   std::chrono::milliseconds interval,
                                   std::ostream& output,
                                   bool quiet,
                                   std::function<void(std::exception_ptr)>
                                       error_callback)
    : counters_(counters),
      interval_(interval),
      output_(output),
      quiet_(quiet),
      error_callback_(std::move(error_callback)) {}

ProgressReporter::~ProgressReporter() {
    stop_without_summary();
}

void ProgressReporter::start() {
    std::lock_guard<std::mutex> lock(mutex_);
    if (state_ != State::idle) {
        throw std::logic_error("Progress reporter has already been started");
    }

    started_ = std::chrono::steady_clock::now();
    stopping_ = false;
    thread_error_ = nullptr;
    thread_exited_ = true;
    state_ = State::running;

    if (quiet_ || interval_ <= std::chrono::milliseconds::zero()) {
        return;
    }

    try {
        thread_exited_ = false;
        thread_ = std::thread(&ProgressReporter::run, this);
    } catch (...) {
        state_ = State::idle;
        stopping_ = false;
        thread_exited_ = true;
        throw;
    }
}

std::chrono::steady_clock::duration ProgressReporter::finish() {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (state_ != State::running) {
            throw std::logic_error(
                "Progress reporter is not running or was already finished");
        }
        if (thread_.joinable() &&
            thread_.get_id() == std::this_thread::get_id()) {
            throw std::logic_error(
                "Progress reporter cannot be finished from its own thread");
        }
        state_ = State::finishing;
        stopping_ = true;
    }
    changed_.notify_all();
    join_thread_safely();

    const auto elapsed = std::chrono::steady_clock::now() - started_;
    std::exception_ptr thread_error;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        state_ = State::finished;
        thread_error = thread_error_;
    }

    if (thread_error) {
        std::rethrow_exception(thread_error);
    }
    return elapsed;
}

void ProgressReporter::stop_without_summary() noexcept {
    try {
        {
            std::lock_guard<std::mutex> lock(mutex_);
            if (state_ != State::running) {
                return;
            }
            stopping_ = true;
            if (thread_.joinable() &&
                thread_.get_id() == std::this_thread::get_id()) {
                // The internal thread cannot join itself. Leave the reporter
                // in `running` state so an owning/control thread can join it
                // after this callback returns and run() exits.
                changed_.notify_all();
                return;
            }
            state_ = State::stopped;
        }
        changed_.notify_all();
        join_thread_safely();
    } catch (...) {
        record_error(std::current_exception());
    }
}

void ProgressReporter::record_error(std::exception_ptr error) noexcept {
    if (!error) {
        return;
    }

    bool first = false;
    try {
        {
            std::lock_guard<std::mutex> lock(mutex_);
            if (!thread_error_) {
                thread_error_ = error;
                first = true;
            }
            stopping_ = true;
        }
        changed_.notify_all();
    } catch (...) {
        std::terminate();
    }

    if (first && error_callback_) {
        try {
            error_callback_(error);
        } catch (...) {
            // The original reporter failure remains the authoritative error.
            // Error callbacks are notifications and must not escape this
            // no-throw reporter boundary.
        }
    }
}

void ProgressReporter::join_thread_safely() noexcept {
    if (!thread_.joinable()) {
        return;
    }
    if (thread_.get_id() == std::this_thread::get_id()) {
        // The public lifecycle rejects self-finish and leaves self-stop for an
        // owning control thread. Destroying `this` from run() is unsupported
        // and cannot be made lifetime-safe by detaching an active thread.
        std::terminate();
    }

    try {
        thread_.join();
        return;
    } catch (const std::system_error&) {
        record_error(std::current_exception());
    } catch (...) {
        record_error(std::current_exception());
    }

    try {
        std::unique_lock<std::mutex> lock(mutex_);
        changed_.wait(lock, [this] { return thread_exited_; });
    } catch (...) {
        record_error(std::current_exception());
        // Detaching before run() exits would permit use-after-destruction.
        std::terminate();
    }

    // Retry a normal join after confirmed thread exit. Detach is used only if
    // the platform still rejects that join, and is then lifetime-safe because
    // run() has published its terminal state already.
    try {
        thread_.join();
        return;
    } catch (const std::system_error&) {
        record_error(std::current_exception());
    } catch (...) {
        record_error(std::current_exception());
    }

    if (thread_.joinable()) {
        try {
            thread_.detach();
        } catch (...) {
            record_error(std::current_exception());
            std::terminate();
        }
    }
}

void ProgressReporter::run() noexcept {
    try {
        auto previous_time = std::chrono::steady_clock::now();
        std::uint64_t previous_input =
            counters_.input_records.load(std::memory_order_relaxed);

        for (;;) {
            {
                std::unique_lock<std::mutex> lock(mutex_);
                if (changed_.wait_for(lock, interval_,
                                      [this] { return stopping_; })) {
                    break;
                }
            }

            const auto now = std::chrono::steady_clock::now();
            const std::uint64_t input =
                counters_.input_records.load(std::memory_order_relaxed);
            const std::uint64_t output =
                counters_.output_records.load(std::memory_order_relaxed);
            const std::uint64_t memory =
                counters_.tracked_memory.load(std::memory_order_relaxed);
            const std::uint64_t delta =
                input >= previous_input ? input - previous_input : 0;
            const std::uint64_t rate =
                records_per_second(delta, now - previous_time);

            output_ << periodic_line(now - started_, input, output, rate,
                                     memory)
                    << '\n'
                    << std::flush;
            if (!output_) {
                throw std::runtime_error(
                    "Failed to write periodic progress output");
            }
            previous_time = now;
            previous_input = input;
        }
    } catch (...) {
        record_error(std::current_exception());
    }

    try {
        std::unique_lock<std::mutex> lock(mutex_);
        thread_exited_ = true;
        // Publish completion only at native thread exit. A join fallback can
        // therefore detach only after this reporter has stopped using `this`.
        std::notify_all_at_thread_exit(changed_, std::move(lock));
    } catch (...) {
        std::terminate();
    }
}

}  // namespace ctb
````````

## `src/vcf_io.cpp`

````````text
#include "convert_to_biallelic/vcf_io.hpp"

#include <htslib/bgzf.h>
#include <htslib/hfile.h>
#include <htslib/hts.h>
#include <htslib/kstring.h>

#include <cerrno>
#include <cstdlib>
#include <limits>
#include <stdexcept>
#include <string>
#include <system_error>

#ifdef _WIN32
#include <io.h>
#else
#include <unistd.h>
#endif

namespace ctb {
namespace {

std::string path_to_utf8(const std::filesystem::path& path) {
    return path.u8string();
}

int checked_thread_count(std::size_t io_workers,
                         const std::string& path_text) {
    if (io_workers > static_cast<std::size_t>(
                         std::numeric_limits<int>::max())) {
        throw std::runtime_error("I/O worker count is too large for HTSlib for '" +
                                 path_text + "'");
    }
    return static_cast<int>(io_workers);
}

std::runtime_error state_error(const char* operation, const char* state) {
    return std::runtime_error(std::string("Cannot ") + operation + " a " + state +
                              " VCF I/O object");
}

int close_descriptor(int descriptor) noexcept {
#ifdef _WIN32
    return ::_close(descriptor);
#else
    return ::close(descriptor);
#endif
}

std::string errno_message(int error) {
    return std::generic_category().message(error);
}

class DescriptorGuard {
public:
    explicit DescriptorGuard(int descriptor) noexcept
        : descriptor_(descriptor) {}

    ~DescriptorGuard() noexcept {
        if (descriptor_ >= 0) {
            (void)close_descriptor(descriptor_);
        }
    }

    DescriptorGuard(const DescriptorGuard&) = delete;
    DescriptorGuard& operator=(const DescriptorGuard&) = delete;

    int release() noexcept {
        const int descriptor = descriptor_;
        descriptor_ = -1;
        return descriptor;
    }

    int get() const noexcept { return descriptor_; }

private:
    int descriptor_;
};

}  // namespace

struct InputSource::Impl {
    explicit Impl(const std::filesystem::path& path, std::size_t io_workers)
        : path_text(path_to_utf8(path)) {
        file = hts_open(path_text.c_str(), "r");
        if (file == nullptr) {
            throw std::runtime_error("Failed to open input VCF '" + path_text +
                                     "'");
        }

        try {
            const htsFormat* detected = hts_get_format(file);
            if (detected == nullptr || detected->format != vcf) {
                throw std::runtime_error("Input '" + path_text +
                                         "' is not a text VCF file");
            }

            switch (detected->compression) {
                case no_compression:
                    is_compressed = false;
                    break;
                case gzip:
                case bgzf:
                    is_compressed = true;
                    break;
                default:
                    throw std::runtime_error(
                        "Input VCF '" + path_text +
                        "' uses unsupported compression; expected plain, gzip, or BGZF");
            }

            if (is_compressed && io_workers > 0) {
                const int thread_count =
                    checked_thread_count(io_workers, path_text);
                if (hts_set_threads(file, thread_count) != 0) {
                    throw std::runtime_error(
                        "Failed to enable threaded input decompression for '" +
                        path_text + "'");
                }
            }
        } catch (...) {
            (void)hts_close(file);
            file = nullptr;
            throw;
        }
    }

    ~Impl() noexcept {
        if (file != nullptr) {
            (void)hts_close(file);
        }
        std::free(line.s);
    }

    std::string path_text;
    htsFile* file = nullptr;
    kstring_t line{0, 0, nullptr};
    bool is_compressed = false;
};

InputSource::InputSource(const std::filesystem::path& path,
                         std::size_t io_workers)
    : impl_(std::make_unique<Impl>(path, io_workers)) {}

InputSource::~InputSource() = default;
InputSource::InputSource(InputSource&&) noexcept = default;
InputSource& InputSource::operator=(InputSource&&) noexcept = default;

bool InputSource::getline(std::string& line) {
    if (!impl_) {
        throw state_error("read from", "moved-from");
    }
    if (impl_->file == nullptr) {
        throw state_error("read from", "closed");
    }

    const int result = hts_getline(impl_->file, KS_SEP_LINE, &impl_->line);
    if (result == -1) {
        return false;
    }
    if (result < -1) {
        throw std::runtime_error("Failed while reading input VCF '" +
                                 impl_->path_text + "'");
    }

    if (impl_->line.l == 0) {
        line.clear();
    } else {
        line.assign(impl_->line.s, impl_->line.l);
    }
    return true;
}

bool InputSource::compressed() const noexcept {
    return impl_ != nullptr && impl_->is_compressed;
}

void InputSource::close() {
    if (!impl_) {
        throw state_error("close", "moved-from");
    }
    if (impl_->file == nullptr) {
        return;
    }

    htsFile* const handle = impl_->file;
    impl_->file = nullptr;
    if (hts_close(handle) != 0) {
        throw std::runtime_error("Failed to close input VCF '" +
                                 impl_->path_text + "'");
    }
}

struct OutputSink::Impl {
    Impl(const std::filesystem::path& path,
         OutputFormat format,
         std::size_t io_workers)
        : pending_descriptor(-1), path_text(path_to_utf8(path)) {
        hFILE* stream = hopen(path_text.c_str(), "wb");
        if (stream == nullptr) {
            throw std::runtime_error("Failed to open VCF output '" +
                                     path_text + "'");
        }
        adopt_hfile(stream, format, io_workers);
    }

    Impl(int owned_fd,
         const std::filesystem::path& display_path,
         OutputFormat format,
         std::size_t io_workers)
        : pending_descriptor(owned_fd), path_text(path_to_utf8(display_path)) {
        if (owned_fd < 0) {
            throw std::invalid_argument(
                "Invalid owned output descriptor for '" + path_text + "'");
        }

        hFILE* stream = hdopen(pending_descriptor.get(), "wb");
        if (stream == nullptr) {
            const int adoption_error = errno;
            const int descriptor = pending_descriptor.release();
            if (close_descriptor(descriptor) != 0) {
                const int close_error = errno;
                throw std::runtime_error(
                    "Failed to adopt owned output descriptor for '" +
                    path_text + "': " + errno_message(adoption_error) +
                    "; descriptor cleanup also failed: " +
                    errno_message(close_error));
            }
            throw std::runtime_error(
                "Failed to adopt owned output descriptor for '" + path_text +
                "': " + errno_message(adoption_error));
        }
        (void)pending_descriptor.release();
        adopt_hfile(stream, format, io_workers);
    }

    void adopt_hfile(hFILE* stream,
                     OutputFormat format,
                     std::size_t io_workers) {
        try {
            switch (format) {
                case OutputFormat::vcf:
                    plain = stream;
                    break;

                case OutputFormat::vcf_gz:
                    compressed = bgzf_hopen(stream, "w");
                    if (compressed == nullptr) {
                        const int error = errno;
                        hclose_abruptly(stream);
                        throw std::runtime_error(
                            "Failed to wrap owned hFILE as BGZF VCF output '" +
                            path_text + "': " + errno_message(error));
                    }
                    if (io_workers > 0) {
                        const int thread_count =
                            checked_thread_count(io_workers, path_text);
                        constexpr int kThreadQueueBlocks = 256;
                        if (bgzf_mt(compressed, thread_count, kThreadQueueBlocks) !=
                            0) {
                            throw std::runtime_error(
                                "Failed to enable threaded BGZF output for '" +
                                path_text + "'");
                        }
                    }
                    break;

                default:
                    hclose_abruptly(stream);
                    throw std::runtime_error("Unsupported output format for '" +
                                             path_text + "'");
            }
        } catch (...) {
            if (plain != nullptr) {
                (void)hclose(plain);
                plain = nullptr;
            }
            if (compressed != nullptr) {
                (void)bgzf_close(compressed);
                compressed = nullptr;
            }
            throw;
        }
    }

    ~Impl() noexcept {
        if (plain != nullptr) {
            (void)hflush(plain);
            (void)hclose(plain);
        }
        if (compressed != nullptr) {
            (void)bgzf_flush(compressed);
            (void)bgzf_close(compressed);
        }
    }

    bool is_closed() const noexcept {
        return plain == nullptr && compressed == nullptr;
    }

    void write(std::string_view bytes) {
        if (is_closed()) {
            throw state_error("write to", "closed");
        }

        std::size_t offset = 0;
        while (offset < bytes.size()) {
            const std::size_t remaining = bytes.size() - offset;
            const auto written = plain != nullptr
                                     ? hwrite(plain, bytes.data() + offset, remaining)
                                     : bgzf_write(compressed,
                                                  bytes.data() + offset,
                                                  remaining);
            if (written <= 0) {
                throw std::runtime_error("Failed while writing output VCF '" +
                                         path_text + "'");
            }
            offset += static_cast<std::size_t>(written);
        }
    }

    void flush() {
        if (is_closed()) {
            throw state_error("flush", "closed");
        }

        const int status = plain != nullptr ? hflush(plain)
                                            : bgzf_flush(compressed);
        if (status != 0) {
            throw std::runtime_error("Failed to flush output VCF '" + path_text +
                                     "'");
        }
    }

    void close() {
        if (is_closed()) {
            return;
        }

        int flush_status = 0;
        int close_status = 0;
        if (plain != nullptr) {
            hFILE* handle = plain;
            plain = nullptr;
            flush_status = hflush(handle);
            close_status = hclose(handle);
        } else {
            BGZF* handle = compressed;
            compressed = nullptr;
            flush_status = bgzf_flush(handle);
            close_status = bgzf_close(handle);
        }

        if (flush_status != 0 && close_status != 0) {
            throw std::runtime_error("Failed to flush and close output VCF '" +
                                     path_text + "'");
        }
        if (flush_status != 0) {
            throw std::runtime_error("Failed to flush output VCF while closing '" +
                                     path_text + "'");
        }
        if (close_status != 0) {
            throw std::runtime_error("Failed to close output VCF '" + path_text +
                                     "'");
        }
    }

    // Declared first so descriptor ownership survives exceptions from display
    // path allocation and every later member/constructor operation.
    DescriptorGuard pending_descriptor;
    std::string path_text;
    hFILE* plain = nullptr;
    BGZF* compressed = nullptr;
};

OutputSink::OutputSink(const std::filesystem::path& path,
                       OutputFormat format,
                       std::size_t io_workers)
    : impl_(std::make_unique<Impl>(path, format, io_workers)) {}

OutputSink::OutputSink(int owned_fd,
                       const std::filesystem::path& display_path,
                       OutputFormat format,
                       std::size_t io_workers) {
    DescriptorGuard descriptor(owned_fd);
    // A new-expression allocates before evaluating its initializer. If the
    // allocation fails, the guard still owns the descriptor; once release()
    // runs, Impl is solely responsible for every constructor path.
    impl_.reset(new Impl(descriptor.release(), display_path, format,
                         io_workers));
}

OutputSink::~OutputSink() = default;
OutputSink::OutputSink(OutputSink&&) noexcept = default;
OutputSink& OutputSink::operator=(OutputSink&&) noexcept = default;

void OutputSink::write(std::string_view bytes) {
    if (!impl_) {
        throw state_error("write to", "moved-from");
    }
    impl_->write(bytes);
}

void OutputSink::flush() {
    if (!impl_) {
        throw state_error("flush", "moved-from");
    }
    impl_->flush();
}

void OutputSink::close() {
    if (!impl_) {
        throw state_error("close", "moved-from");
    }
    impl_->close();
}

}  // namespace ctb
````````
