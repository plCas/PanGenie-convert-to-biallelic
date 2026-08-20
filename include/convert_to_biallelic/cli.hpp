#pragma once

#include "types.hpp"

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <filesystem>

namespace ctb {

struct Config {
    std::filesystem::path variants;
    std::filesystem::path input;
    std::filesystem::path output;
    std::size_t threads = 1;
    std::uint64_t memory_limit_bytes = 2ULL * 1024ULL * 1024ULL * 1024ULL;
    std::chrono::milliseconds progress_interval{5000};
    bool quiet = false;
    bool force = false;
    OutputFormat output_format = OutputFormat::vcf;
    CompatibilityMode compatibility_mode = CompatibilityMode::strict;
};

class UsageRequested final : public std::exception {
public:
    explicit UsageRequested(bool version) noexcept : version_(version) {}

    bool version() const noexcept { return version_; }
    const char* what() const noexcept override { return "usage requested"; }

private:
    bool version_;
};

Config parse_cli(int argc, char** argv);
ThreadAllocation allocate_threads(const Config& config, bool compressed_input,
                                  bool compressed_output);
const char* usage_text();

}  // namespace ctb
