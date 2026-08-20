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
