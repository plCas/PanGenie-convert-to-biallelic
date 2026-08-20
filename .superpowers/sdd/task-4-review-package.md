# Task 4 Full-File Static Review Package

## No-Git-base note

This source-only review did not use a Git base, diff, status, commit, branch, or worktree. The package below contains exact fenced copies of the authored Task 4 files for direct review.

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

const VariantDefinition* AnnotationIndex::find(
    std::string_view chromosome, std::string_view id) const noexcept {
    // C++17 unordered_map has no heterogeneous lookup, so temporary strings are
    // required here. Allocation failure is represented as a failed lookup to
    // preserve this public noexcept API.
    try {
        const auto chromosome_it = variants_.find(std::string(chromosome));
        if (chromosome_it == variants_.end()) {
            return nullptr;
        }

        const auto id_it = chromosome_it->second.find(std::string(id));
        return id_it == chromosome_it->second.end() ? nullptr : &id_it->second;
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
