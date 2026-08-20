### Task 4: Author the Immutable Annotation Index

Create `include/convert_to_biallelic/annotation_index.hpp` and `src/annotation_index.cpp`. Source-only: no install/build/run/test/Git; Python unchanged.

Required public API in namespace `ctb`:

```cpp
struct VariantDefinition { std::int64_t position; std::string ref; std::string alt; };
class AnnotationIndex {
public:
  const VariantDefinition* find(std::string_view chromosome, std::string_view id) const noexcept;
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
```

Requirements:

1. Stream `InputSource` once; skip header lines beginning `#`; track physical line number.
2. Split data on tabs without regex; require at least 8 fields.
3. Parse POS as a fully consumed positive int64 using `std::from_chars`.
4. Scan semicolon INFO entries for exact key `ID`; require exactly one `ID=` entry with nonempty value and reject comma-containing values.
5. Store chromosome, ID, POS, REF, ALT. Assignment overwrites duplicate chromosome/ID so last wins; `variant_count` counts unique current keys, not lines.
6. `find` must avoid allocation where practical; because C++17 unordered_map lacks transparent lookup, a temporary string is acceptable and must be noted.
7. Estimate memory conservatively from map/bucket/node payload approximations and string capacities. Recalculate/update after insertion/overwrite without unsigned underflow.
8. Reserve 10% of `memory_limit`; reject memory limits below 64 MiB and throw before estimate exceeds 90% allowance. Error names annotation line and reason.
9. After loading, index is immutable via public API and safe for concurrent const reads.
10. Do not make unsupported exact-RSS claims; estimate remains unverified.
11. Use apply_patch. Static self-review only.

Write `.superpowers/sdd/task-4-report.md` and a complete full-file `.superpowers/sdd/task-4-review-package.md`. Explicit no tests/commits and concerns.
