#pragma once

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace ctb {

enum class OutputFormat { vcf, vcf_gz };
enum class CompatibilityMode { strict, python };

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
