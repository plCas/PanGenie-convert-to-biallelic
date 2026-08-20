### Task 5: Author Python-Compatible Text Conversion

Create `include/convert_to_biallelic/converter.hpp` and `src/converter.cpp`. Source-only: no install/build/run/test/Git; Python unchanged.

Public API in namespace `ctb`:

```cpp
struct ConversionResult { std::string bytes; std::uint64_t output_records = 0; };
std::optional<std::string> convert_header(std::string_view line);
ConversionResult convert_record(std::string_view line,
                                const AnnotationIndex& annotation,
                                std::uint64_t line_number);
```

Exact requirements:

1. `convert_header` returns nullopt if line contains any of: `INFO=<ID=AF`, `INFO=<ID=AK`, `FORMAT=<ID=GL`, `FORMAT=<ID=KC`; otherwise exact line plus LF.
2. Data split on tabs; require at least 9 fields; error includes `Input line N:`.
3. Parse INFO semicolon entries containing `=` at first equals. Duplicate keys use last value while key iteration order stays first occurrence. Require nonempty `ID`.
4. `allele_to_ids` is one empty REF entry plus comma-split `INFO/ID` allele mappings. Each mapping may contain colon-separated component IDs.
5. If `INFO/ID` has exactly one comma-level mapping and any of its colon components is missing from annotation for the chromosome, pass original line through plus LF without further conversion.
6. Otherwise every component must resolve. Deduplicate by ID, retain annotation position, sort by position then ID for deterministic coordinate ties.
7. For each emitted ID: begin with first 9 fields; set POS to normalized decimal annotation POS; VCF ID column to variant ID; REF/ALT from annotation; INFO to `ID=<id>` plus only MA and UK in original key encounter order with last duplicate value; FORMAT to `GT` plus `:GQ` only if exact GQ token exists.
8. Require exact GT FORMAT token. Determine GT/GQ indices once per input record. Validate each sample has needed colon fields.
9. Split genotype alleles on either `/` or `|`, preserving ploidy. `.` stays `.`. Otherwise parse fully consumed nonnegative decimal allele index, require index in `allele_to_ids`, split that mapping on `:`, emit `1` if current variant ID is an exact component and `0` otherwise. Join with `/`.
10. Copy exact GQ text when present. Append LF per emitted record. Output record count equals emissions; unknown passthrough count is 1.
11. Reject empty component IDs, malformed indices, missing annotation for convertible records, and structural errors with line context.
12. Avoid repeated parsing inside the sample loop beyond each sample's colon split and GT allele split.
13. Static comparison against Python lines 26-101 must be documented; note deliberate stricter validation and deterministic tie ordering. Byte equivalence remains unverified.

Use apply_patch. Write complete report and full exact-file review package `.superpowers/sdd/task-5-report.md` and `task-5-review-package.md`. No tests/commits.
