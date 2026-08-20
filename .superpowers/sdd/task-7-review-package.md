# Task 7 Complete Eight-File Static Review Package

## No-Git-base note

This source-only review did not use a Git base, diff, status, commit, branch, or worktree. The package below contains exact fenced copies of all eight current annotation, converter, progress, and pipeline API/implementation files.

## Review boundary

The files below were compared statically with the Task 4, Task 5, and Task 7 briefs plus the required review-fix passes. Immutable view-index ownership, preflight and post-build annotation accounting, bounded converter scratch, physical cleanup ordering, result growth, reporter failure propagation, stream ownership, reader/worker/writer ownership, spawn completion, sequence ordering, memory admission, cancellation, overflow, thread lifetime, safe join fallback, lock ordering, semantic preservation, and deferred runtime boundaries are documented in `.superpowers/sdd/task-7-report.md`.

No compiler, build, executable, test, benchmark, Python process, or Git command was run.

## `include/convert_to_biallelic/annotation_index.hpp`

```cpp
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
    std::int64_t position;
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
```

## `src/annotation_index.cpp`

```cpp
#include "convert_to_biallelic/annotation_index.hpp"

#include <charconv>
#include <cstddef>
#include <limits>
#include <stdexcept>
#include <string>
#include <string_view>
#include <system_error>
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

template <typename OuterMap>
std::uint64_t count_variants(const OuterMap& variants) noexcept {
    std::uint64_t total = 0;
    for (const auto& chromosome_entry : variants) {
        total = saturating_add(total, to_uint64(chromosome_entry.second.size()));
    }
    return total;
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
            auto& by_id = index.variants_[std::string(fields[0])];
            by_id.insert_or_assign(
                std::string(identifier),
                VariantDefinition{position, std::string(fields[3]), std::string(fields[4])});

            index.variant_count_ = count_variants(index.variants_);
            index.estimated_bytes_ = estimate_index_bytes(
                index.variants_, index.lookup_);
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
```

## `include/convert_to_biallelic/converter.hpp`

```cpp
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
    const std::function<void(std::size_t target_output_capacity,
                             std::size_t target_scratch_bytes)>&
        before_reserve);

ConversionResult convert_record(std::string_view line,
                                const AnnotationIndex& annotation,
                                std::uint64_t line_number);

}  // namespace ctb
```

## `src/converter.cpp`

```cpp
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

enum class ScratchKind {
    fields,
    info,
    allele_mappings,
    resolved,
    format,
    samples,
    alleles
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
        ensure_capacity(fields, ScratchKind::fields, additional,
                        "field metadata");
    }

    void ensure_info(std::size_t additional = 1) {
        ensure_capacity(info, ScratchKind::info, additional,
                        "INFO metadata");
    }

    void ensure_allele_mappings(std::size_t additional = 1) {
        ensure_capacity(allele_mappings, ScratchKind::allele_mappings,
                        additional, "allele mapping metadata");
    }

    void ensure_resolved(std::size_t additional = 1) {
        ensure_capacity(resolved, ScratchKind::resolved, additional,
                        "resolved variant metadata");
    }

    void ensure_format(std::size_t additional = 1) {
        ensure_capacity(format, ScratchKind::format, additional,
                        "FORMAT metadata");
    }

    void ensure_samples(std::size_t additional = 1) {
        ensure_capacity(samples, ScratchKind::samples, additional,
                        "sample metadata");
    }

    void ensure_alleles(std::size_t additional = 1) {
        ensure_capacity(alleles, ScratchKind::alleles, additional,
                        "sample allele metadata");
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
        admission_(target, scratch_bytes());
        destination_.reserve(target);
        admission_(destination_.capacity(), scratch_bytes());
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
        admission_(destination_.capacity(), 0);
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
                         ScratchKind kind,
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
        admission_(destination_.capacity(),
                   scratch_bytes_with(kind, target));
        values.reserve(target);
        admission_(destination_.capacity(), scratch_bytes());
    }

    std::size_t selected_capacity(ScratchKind current,
                                  ScratchKind overridden,
                                  std::size_t override_capacity,
                                  std::size_t actual_capacity) const noexcept {
        return current == overridden ? override_capacity : actual_capacity;
    }

    std::size_t calculate_scratch_bytes(
        bool has_override,
        ScratchKind overridden,
        std::size_t override_capacity) const {
        std::size_t total = 0;
        const auto add_capacity =
            [&](ScratchKind kind,
                std::size_t actual_capacity,
                std::size_t element_size,
                const char* description) {
                const std::size_t capacity =
                    has_override
                        ? selected_capacity(kind, overridden,
                                            override_capacity,
                                            actual_capacity)
                        : actual_capacity;
                total = checked_size_add(
                    total,
                    checked_size_multiply(capacity, element_size,
                                          description),
                    "converter scratch byte count");
            };

        add_capacity(ScratchKind::fields, fields.capacity(),
                     sizeof(std::string_view), "field scratch bytes");
        add_capacity(ScratchKind::info, info.capacity(), sizeof(InfoEntry),
                     "INFO scratch bytes");
        add_capacity(ScratchKind::allele_mappings,
                     allele_mappings.capacity(), sizeof(std::string_view),
                     "allele mapping scratch bytes");
        add_capacity(ScratchKind::resolved, resolved.capacity(),
                     sizeof(ResolvedVariant),
                     "resolved variant scratch bytes");
        add_capacity(ScratchKind::format, format.capacity(),
                     sizeof(std::string_view), "FORMAT scratch bytes");
        add_capacity(ScratchKind::samples, samples.capacity(),
                     sizeof(ParsedSample), "sample scratch bytes");
        add_capacity(ScratchKind::alleles, alleles.capacity(),
                     sizeof(std::int64_t), "allele scratch bytes");
        return total;
    }

    std::size_t scratch_bytes() const {
        return calculate_scratch_bytes(false, ScratchKind::fields, 0);
    }

    std::size_t scratch_bytes_with(
        ScratchKind overridden,
        std::size_t override_capacity) const {
        return calculate_scratch_bytes(true, overridden,
                                       override_capacity);
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
    if (scratch.fields.size() < 9) {
        throw std::invalid_argument(
            "expected at least 9 tab-separated fields");
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
    const std::function<void(std::size_t target_output_capacity,
                             std::size_t target_scratch_bytes)>&
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
```

## `include/convert_to_biallelic/progress.hpp`

```cpp
#pragma once

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <exception>
#include <functional>
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
    void finish(const PipelineStats& stats);
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

}  // namespace ctb
```

## `src/progress.cpp`

```cpp
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

std::string final_line(const PipelineStats& stats,
                       std::chrono::steady_clock::duration elapsed) {
    std::ostringstream line;
    line << "Finished: input=" << stats.input_records
         << " output=" << stats.output_records
         << " elapsed=" << format_elapsed(elapsed)
         << " average=" << records_per_second(stats.input_records, elapsed)
         << " records/s";
    return line.str();
}

}  // namespace

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

void ProgressReporter::finish(const PipelineStats& stats) {
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
    if (!quiet_) {
        try {
            output_ << final_line(stats, elapsed) << '\n' << std::flush;
            if (!output_) {
                throw std::runtime_error(
                    "Failed to write final progress output");
            }
        } catch (...) {
            const std::exception_ptr error = std::current_exception();
            record_error(error);
            std::rethrow_exception(error);
        }
    }
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
```

## `include/convert_to_biallelic/pipeline.hpp`

```cpp
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
```

## `src/pipeline.cpp`

```cpp
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
    std::condition_variable& memory_changed);

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
                        std::condition_variable& memory_changed) {
    const std::size_t required = chunk.data.records.size() + 1;
    const std::size_t planned = planned_vector_capacity(
        chunk.data.records.capacity(), required, target_records);
    const std::uint64_t planned_bytes = projected_work_storage(
        chunk, record, target_records);

    // Reserve the permit before every planned vector or string-capacity
    // transfer into pipeline-owned storage.
    resize_result_permit(
        chunk.permit, planned_bytes, chunk.data.sequence,
        tracked_allowance, next_written_sequence, pipeline_cancelled,
        counters, budget, publication_mutex, memory_changed);
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
        counters, budget, publication_mutex, memory_changed);

    if (chunk.data.records.empty()) {
        chunk.data.first_line_number = line_number;
    }
    chunk.data.records.push_back(std::move(record));

    // A nothrow string move normally transfers its capacity. Reconcile the
    // actual capacity so the permit remains exact even for an SSO move.
    resize_result_permit(
        chunk.permit, work_storage_bytes(chunk.data), chunk.data.sequence,
        tracked_allowance, next_written_sequence, pipeline_cancelled,
        counters, budget, publication_mutex, memory_changed);
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
    std::condition_variable& memory_changed) {
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
            counters.tracked_memory.store(budget.current_bytes(),
                                          std::memory_order_relaxed);
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
                          std::condition_variable& memory_changed) {
    const std::uint64_t input_records =
        checked_size(work.data.records.size(), "input record count");
    const std::uint64_t input_storage = work_storage_bytes(work.data);
    const std::uint64_t sequence = work.data.sequence;
    resize_result_permit(
        work.permit, input_storage, sequence, tracked_allowance,
        next_written_sequence, pipeline_cancelled, counters, budget,
        memory_publication_mutex, memory_changed);

    RawResultChunk converted_chunk;
    converted_chunk.sequence = sequence;
    converted_chunk.input_records = input_records;

    resize_result_permit(
        work.permit,
        checked_add(input_storage, result_storage_bytes(converted_chunk),
                    "combined input and result storage"),
        sequence, tracked_allowance, next_written_sequence,
        pipeline_cancelled, counters, budget, memory_publication_mutex,
        memory_changed);

    const std::function<void(std::size_t, std::size_t)> before_reserve =
        [&](std::size_t target_output_capacity,
            std::size_t target_scratch_bytes) {
            const std::uint64_t planned_result = checked_add(
                checked_size(target_output_capacity,
                             "planned result capacity"),
                1, "planned result storage");
            const std::uint64_t planned_scratch = checked_size(
                target_scratch_bytes, "planned converter scratch size");
            resize_result_permit(
                work.permit,
                checked_add(
                    checked_add(input_storage, planned_result,
                                "combined input and result storage"),
                    planned_scratch,
                    "combined input, result, and converter scratch storage"),
                sequence, tracked_allowance, next_written_sequence,
                pipeline_cancelled, counters, budget,
                memory_publication_mutex, memory_changed);
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
        budget, memory_publication_mutex, memory_changed);
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
                    memory_budget, memory_publication_mutex, memory_changed);
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
        bool warned_about_input_overage = false;

        auto begin_chunk = [&] {
            if (next_sequence ==
                std::numeric_limits<std::uint64_t>::max()) {
                throw std::overflow_error("Reader chunk sequence overflow");
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
                if (!warned_about_input_overage) {
                    diagnostics
                        << "Warning: input line " << record_line_number
                        << " exceeds the tracked pipeline memory allowance; "
                           "permitting one overage chunk"
                        << '\n'
                        << std::flush;
                    if (!diagnostics) {
                        throw std::runtime_error(
                            "Failed to write pipeline diagnostics");
                    }
                    warned_about_input_overage = true;
                }
            }

            append_work_record(
                chunk, record, record_line_number,
                options.target_records_per_chunk, tracked_allowance,
                next_written_sequence, pipeline_cancelled, counters,
                memory_budget, memory_publication_mutex, memory_changed);
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
        reporter.finish(stats);
        return stats;
    } catch (...) {
        reporter.stop_without_summary();
        throw;
    }
}

}  // namespace ctb
```

