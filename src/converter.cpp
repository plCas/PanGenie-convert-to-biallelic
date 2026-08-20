#include "convert_to_biallelic/converter.hpp"

#include <algorithm>
#include <array>
#include <charconv>
#include <cctype>
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

void parse_python_fields(std::string_view line, ConverterScratch& scratch) {
    std::size_t index = 0;
    while (index < line.size()) {
        while (index < line.size() &&
               std::isspace(static_cast<unsigned char>(line[index])) != 0) {
            ++index;
        }

        const std::size_t begin = index;
        while (index < line.size() &&
               std::isspace(static_cast<unsigned char>(line[index])) == 0) {
            ++index;
        }

        if (begin != index) {
            scratch.ensure_fields();
            scratch.fields.push_back(line.substr(begin, index - begin));
        }
    }
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
                      CompatibilityMode mode,
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

    if (mode == CompatibilityMode::python) {
        std::stable_sort(scratch.resolved.begin(), scratch.resolved.end(),
                         [](const ResolvedVariant& left,
                            const ResolvedVariant& right) {
                             return left.definition->position <
                                    right.definition->position;
                         });
    } else {
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
    }
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
    CompatibilityMode mode,
    std::string& destination,
    std::uint64_t& output_records,
    const std::function<void(std::size_t, std::size_t)>& admission) {
    ConverterScratch scratch(destination, admission);
    if (mode == CompatibilityMode::python) {
        parse_python_fields(line, scratch);
    } else {
        parse_fields(line, scratch);
    }
    if (scratch.fields.size() < 8) {
        throw std::invalid_argument(
            "expected at least 8 tab-separated fields");
    }

    parse_info(scratch.fields[7], scratch);
    const std::string_view identifier = require_identifier(scratch);
    parse_allele_mappings(identifier, scratch);

    const bool unknown_single_mapping =
        resolve_variants(scratch.fields[0], annotation, mode, scratch);
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
    const bool include_gq =
        mode == CompatibilityMode::python
            ? scratch.fields[8].find("GQ") != std::string_view::npos
            : gq_index.has_value();
    if (include_gq && !gq_index.has_value()) {
        throw std::invalid_argument(
            "FORMAT contains GQ but no exact GQ token");
    }
    parse_samples(gt_index, include_gq ? gq_index : std::nullopt, scratch);
    const std::string_view output_format =
        include_gq ? std::string_view("GT:GQ") : std::string_view("GT");

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
                          include_gq);
        }

        append_character(scratch, destination, '\n');
        ++output_records;
    }

    scratch.release_storage_and_admission();
}

}  // namespace

std::optional<std::string> convert_header(std::string_view line,
                                          CompatibilityMode mode) {
    (void)mode;
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
                                std::uint64_t line_number,
                                CompatibilityMode mode) {
    ConversionResult result;
    const std::function<void(std::size_t, std::size_t)> no_admission =
        [](std::size_t, std::size_t) {};
    append_converted_record(line, annotation, line_number, mode, result.bytes,
                            result.output_records, no_admission);
    return result;
}

void append_converted_record(
    std::string_view line,
    const AnnotationIndex& annotation,
    std::uint64_t line_number,
    CompatibilityMode mode,
    std::string& destination,
    std::uint64_t& output_records,
    const std::function<void(std::size_t planned_output_bytes,
                             std::size_t planned_scratch_bytes)>&
        before_reserve) {
    try {
        append_converted_record_impl(line, annotation, mode, destination,
                                     output_records, before_reserve);
    } catch (const std::exception& error) {
        throw std::runtime_error("Input line " + std::to_string(line_number) +
                                 ": " + error.what());
    }
}

}  // namespace ctb
