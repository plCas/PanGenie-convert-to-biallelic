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
