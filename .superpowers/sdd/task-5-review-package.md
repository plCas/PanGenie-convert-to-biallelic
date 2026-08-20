# Task 5 Full-File Static Review Package

## No-Git-base note

This source-only review did not use a Git base, diff, status, commit, branch, or
worktree. The package below contains exact fenced copies of the current Task 5
API and implementation files plus the annotation API/source changed by the
checked-lookup review fix.

## Review boundary

The exact files below were compared statically with
`convert-to-biallelic.py` lines 26-101 and the Task 5 brief. The comparison,
intentional stricter validation, deterministic position/ID tie ordering, and
deferred byte-equivalence verification are documented in
`.superpowers/sdd/task-5-report.md`.

No compiler, build, executable, Python oracle, or test was run.

## `include/convert_to_biallelic/converter.hpp`

```cpp
#pragma once

#include <cstdint>
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

ConversionResult convert_record(std::string_view line,
                                const AnnotationIndex& annotation,
                                std::uint64_t line_number);

}  // namespace ctb
```

## `include/convert_to_biallelic/annotation_index.hpp`

```cpp
#pragma once

#include <cstdint>
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
    const VariantDefinition* find(std::string_view chromosome,
                                  std::string_view id) const noexcept;
    const VariantDefinition* find_checked(std::string_view chromosome,
                                          std::string_view id) const;
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

template <typename OuterMap>
std::uint64_t estimate_index_bytes(const OuterMap& variants) noexcept {
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

const VariantDefinition* AnnotationIndex::find_checked(
    std::string_view chromosome, std::string_view id) const {
    // C++17 unordered_map has no heterogeneous lookup, so temporary strings are
    // required here. Their allocation failures intentionally propagate.
    const auto chromosome_it = variants_.find(std::string(chromosome));
    if (chromosome_it == variants_.end()) {
        return nullptr;
    }

    const auto id_it = chromosome_it->second.find(std::string(id));
    return id_it == chromosome_it->second.end() ? nullptr : &id_it->second;
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
        std::uint64_t next_line_number = line_number;
        try {
            if (next_line_number == std::numeric_limits<std::uint64_t>::max()) {
                throw std::overflow_error("physical line number overflow");
            }
            ++next_line_number;
            if (!input.getline(line)) {
                break;
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
            index.estimated_bytes_ = estimate_index_bytes(index.variants_);
            if (index.estimated_bytes_ > allowance) {
                throw std::invalid_argument(
                    "estimated annotation index memory exceeds the 90% allowance");
            }
        } catch (const std::exception& error) {
            throw annotation_line_error(next_line_number, error.what());
        }
    }

    return index;
}

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
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

namespace ctb {
namespace {

struct InfoEntry {
    std::string_view key;
    std::string_view value;
};

struct AlleleMapping {
    std::vector<std::string_view> components;
};

struct ResolvedVariant {
    std::string_view id;
    const VariantDefinition* definition;
};

struct ParsedAllele {
    bool missing = false;
    std::size_t index = 0;
};

struct ParsedSample {
    std::vector<ParsedAllele> alleles;
    std::string_view gq;
};

std::vector<std::string_view> split_on(std::string_view text,
                                       char delimiter) {
    std::vector<std::string_view> parts;
    std::size_t begin = 0;
    while (begin <= text.size()) {
        const std::size_t end = text.find(delimiter, begin);
        if (end == std::string_view::npos) {
            parts.push_back(text.substr(begin));
            break;
        }
        parts.push_back(text.substr(begin, end - begin));
        begin = end + 1;
    }
    return parts;
}

std::vector<std::string_view> split_genotype(std::string_view genotype) {
    std::vector<std::string_view> alleles;
    std::size_t begin = 0;
    for (std::size_t index = 0; index < genotype.size(); ++index) {
        if (genotype[index] == '/' || genotype[index] == '|') {
            alleles.push_back(genotype.substr(begin, index - begin));
            begin = index + 1;
        }
    }
    alleles.push_back(genotype.substr(begin));
    return alleles;
}

std::vector<InfoEntry> parse_info(std::string_view info) {
    std::vector<InfoEntry> parsed;
    for (const std::string_view raw_entry : split_on(info, ';')) {
        const std::size_t equals = raw_entry.find('=');
        if (equals == std::string_view::npos) {
            continue;
        }

        const std::string_view key = raw_entry.substr(0, equals);
        const std::string_view value = raw_entry.substr(equals + 1);
        const auto existing = std::find_if(
            parsed.begin(), parsed.end(),
            [key](const InfoEntry& entry) { return entry.key == key; });
        if (existing == parsed.end()) {
            parsed.push_back(InfoEntry{key, value});
        } else {
            existing->value = value;
        }
    }
    return parsed;
}

std::string_view require_identifier(const std::vector<InfoEntry>& info) {
    const auto identifier = std::find_if(
        info.begin(), info.end(),
        [](const InfoEntry& entry) { return entry.key == "ID"; });
    if (identifier == info.end() || identifier->value.empty()) {
        throw std::invalid_argument(
            "INFO must contain a nonempty ID value");
    }
    return identifier->value;
}

std::vector<AlleleMapping> parse_allele_mappings(
    std::string_view identifier) {
    std::vector<AlleleMapping> allele_to_ids;
    allele_to_ids.emplace_back();  // REF has no variant ID.

    for (const std::string_view mapping : split_on(identifier, ',')) {
        AlleleMapping parsed_mapping;
        for (const std::string_view component : split_on(mapping, ':')) {
            if (component.empty()) {
                throw std::invalid_argument(
                    "INFO ID allele mappings must not contain empty component IDs");
            }
            parsed_mapping.components.push_back(component);
        }
        allele_to_ids.push_back(std::move(parsed_mapping));
    }
    return allele_to_ids;
}

std::vector<ResolvedVariant> resolve_variants(
    std::string_view chromosome,
    const std::vector<AlleleMapping>& allele_to_ids,
    const AnnotationIndex& annotation,
    bool& unknown_single_mapping) {
    std::vector<ResolvedVariant> variants;
    unknown_single_mapping = false;
    const bool permit_unknown_passthrough = allele_to_ids.size() == 2;

    for (std::size_t allele = 1; allele < allele_to_ids.size(); ++allele) {
        for (const std::string_view component :
             allele_to_ids[allele].components) {
            const VariantDefinition* const definition =
                annotation.find_checked(chromosome, component);
            if (definition == nullptr) {
                if (permit_unknown_passthrough) {
                    unknown_single_mapping = true;
                    return {};
                }
                throw std::invalid_argument(
                    "missing annotation for chromosome '" +
                    std::string(chromosome) + "' and ID '" +
                    std::string(component) + "'");
            }

            const auto duplicate = std::find_if(
                variants.begin(), variants.end(),
                [component](const ResolvedVariant& variant) {
                    return variant.id == component;
                });
            if (duplicate == variants.end()) {
                variants.push_back(ResolvedVariant{component, definition});
            }
        }
    }

    std::sort(variants.begin(), variants.end(),
              [](const ResolvedVariant& left,
                 const ResolvedVariant& right) {
                  if (left.definition->position != right.definition->position) {
                      return left.definition->position < right.definition->position;
                  }
                  return left.id < right.id;
              });
    return variants;
}

std::size_t find_required_gt(const std::vector<std::string_view>& format) {
    const auto gt = std::find(format.begin(), format.end(), "GT");
    if (gt == format.end()) {
        throw std::invalid_argument(
            "FORMAT must contain an exact GT token");
    }
    return static_cast<std::size_t>(gt - format.begin());
}

std::optional<std::size_t> find_gq(
    const std::vector<std::string_view>& format) {
    const auto gq = std::find(format.begin(), format.end(), "GQ");
    if (gq == format.end()) {
        return std::nullopt;
    }
    return static_cast<std::size_t>(gq - format.begin());
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

std::vector<ParsedSample> parse_samples(
    const std::vector<std::string_view>& fields,
    std::size_t gt_index,
    std::optional<std::size_t> gq_index,
    std::size_t allele_mapping_count) {
    std::vector<ParsedSample> samples;
    samples.reserve(fields.size() - 9);

    for (std::size_t field_index = 9; field_index < fields.size();
         ++field_index) {
        const std::size_t sample_number = field_index - 8;
        const auto values = split_on(fields[field_index], ':');
        if (gt_index >= values.size()) {
            throw std::invalid_argument(
                "sample " + std::to_string(sample_number) +
                " does not contain the GT field required by FORMAT");
        }
        if (gq_index.has_value() && *gq_index >= values.size()) {
            throw std::invalid_argument(
                "sample " + std::to_string(sample_number) +
                " does not contain the GQ field required by FORMAT");
        }

        ParsedSample sample;
        if (gq_index.has_value()) {
            sample.gq = values[*gq_index];
        }

        for (const std::string_view allele : split_genotype(values[gt_index])) {
            if (allele == ".") {
                sample.alleles.push_back(ParsedAllele{true, 0});
                continue;
            }

            const std::size_t allele_index =
                parse_allele_index(allele, sample_number);
            if (allele_index >= allele_mapping_count) {
                throw std::invalid_argument(
                    "sample " + std::to_string(sample_number) +
                    " GT allele index " + std::string(allele) +
                    " is outside the INFO ID allele mapping range");
            }
            sample.alleles.push_back(ParsedAllele{false, allele_index});
        }
        samples.push_back(std::move(sample));
    }
    return samples;
}

std::string build_info(std::string_view variant_id,
                       const std::vector<InfoEntry>& info) {
    std::string output = "ID=";
    output.append(variant_id.data(), variant_id.size());
    for (const InfoEntry& entry : info) {
        if (entry.key != "MA" && entry.key != "UK") {
            continue;
        }
        output.push_back(';');
        output.append(entry.key.data(), entry.key.size());
        output.push_back('=');
        output.append(entry.value.data(), entry.value.size());
    }
    return output;
}

std::string convert_sample(const ParsedSample& sample,
                           std::string_view variant_id,
                           const std::vector<AlleleMapping>& allele_to_ids,
                           bool include_gq) {
    std::string output;
    for (std::size_t index = 0; index < sample.alleles.size(); ++index) {
        if (index != 0) {
            output.push_back('/');
        }

        const ParsedAllele& allele = sample.alleles[index];
        if (allele.missing) {
            output.push_back('.');
            continue;
        }

        const auto& components = allele_to_ids[allele.index].components;
        const bool contains_variant =
            std::find(components.begin(), components.end(), variant_id) !=
            components.end();
        output.push_back(contains_variant ? '1' : '0');
    }

    if (include_gq) {
        output.push_back(':');
        output.append(sample.gq.data(), sample.gq.size());
    }
    return output;
}

void append_record(std::string& bytes,
                   const std::vector<std::string>& fields) {
    for (std::size_t index = 0; index < fields.size(); ++index) {
        if (index != 0) {
            bytes.push_back('\t');
        }
        bytes.append(fields[index]);
    }
    bytes.push_back('\n');
}

ConversionResult passthrough(std::string_view line) {
    ConversionResult result;
    result.bytes.assign(line.data(), line.size());
    result.bytes.push_back('\n');
    result.output_records = 1;
    return result;
}

ConversionResult convert_record_impl(std::string_view line,
                                     const AnnotationIndex& annotation) {
    const auto fields = split_on(line, '\t');
    if (fields.size() < 9) {
        throw std::invalid_argument(
            "expected at least 9 tab-separated fields");
    }

    const auto info = parse_info(fields[7]);
    const std::string_view identifier = require_identifier(info);
    const auto allele_to_ids = parse_allele_mappings(identifier);

    bool unknown_single_mapping = false;
    const auto variants =
        resolve_variants(fields[0], allele_to_ids, annotation,
                         unknown_single_mapping);
    if (unknown_single_mapping) {
        return passthrough(line);
    }

    const auto format = split_on(fields[8], ':');
    const std::size_t gt_index = find_required_gt(format);
    const std::optional<std::size_t> gq_index = find_gq(format);
    const auto samples = parse_samples(fields, gt_index, gq_index,
                                       allele_to_ids.size());
    const std::string output_format =
        gq_index.has_value() ? "GT:GQ" : "GT";

    ConversionResult result;
    for (const ResolvedVariant& variant : variants) {
        std::vector<std::string> output_fields;
        output_fields.reserve(9 + samples.size());
        for (std::size_t index = 0; index < 9; ++index) {
            output_fields.emplace_back(fields[index]);
        }

        output_fields[1] = std::to_string(variant.definition->position);
        output_fields[2] = std::string(variant.id);
        output_fields[3] = variant.definition->ref;
        output_fields[4] = variant.definition->alt;
        output_fields[7] = build_info(variant.id, info);
        output_fields[8] = output_format;

        for (const ParsedSample& sample : samples) {
            output_fields.push_back(
                convert_sample(sample, variant.id, allele_to_ids,
                               gq_index.has_value()));
        }

        append_record(result.bytes, output_fields);
        ++result.output_records;
    }
    return result;
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
    try {
        return convert_record_impl(line, annotation);
    } catch (const std::exception& error) {
        throw std::runtime_error("Input line " + std::to_string(line_number) +
                                 ": " + error.what());
    }
}

}  // namespace ctb
```
