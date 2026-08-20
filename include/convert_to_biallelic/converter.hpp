#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <string_view>

#include "convert_to_biallelic/annotation_index.hpp"
#include "convert_to_biallelic/types.hpp"

namespace ctb {

struct ConversionResult {
    std::string bytes;
    std::uint64_t output_records = 0;
};

std::optional<std::string> convert_header(std::string_view line,
                                          CompatibilityMode mode);

void append_converted_record(
    std::string_view line,
    const AnnotationIndex& annotation,
    std::uint64_t line_number,
    CompatibilityMode mode,
    std::string& destination,
    std::uint64_t& output_records,
    const std::function<void(std::size_t planned_output_bytes,
                             std::size_t planned_scratch_bytes)>&
        before_reserve);

ConversionResult convert_record(std::string_view line,
                                const AnnotationIndex& annotation,
                                std::uint64_t line_number,
                                CompatibilityMode mode);

}  // namespace ctb
