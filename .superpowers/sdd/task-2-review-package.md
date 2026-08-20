# Task 2 review package

This package contains the complete current contents of the two Task 2 source files.
There is no Git base/head comparison because this is source-only work and Git use is
prohibited for this task.

## `include/convert_to_biallelic/cli.hpp`

```cpp
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
```

## `src/cli.cpp`

```cpp
#include "convert_to_biallelic/cli.hpp"

#include <cctype>
#include <limits>
#include <stdexcept>
#include <string>
#include <thread>
#include <unordered_set>

namespace ctb {
namespace {

constexpr std::uint64_t kMiB = 1024ULL * 1024ULL;
constexpr std::uint64_t kMinimumMemoryBytes = 64ULL * kMiB;

std::string lower_ascii(std::string value) {
    for (char& character : value) {
        character = static_cast<char>(
            std::tolower(static_cast<unsigned char>(character)));
    }
    return value;
}

bool ends_with(const std::string& value, const std::string& suffix) {
    return value.size() >= suffix.size() &&
           value.compare(value.size() - suffix.size(), suffix.size(), suffix) == 0;
}

std::uint64_t parse_decimal(const std::string& value, const char* option) {
    if (value.empty()) {
        throw std::invalid_argument(std::string("missing value for ") + option);
    }

    std::uint64_t parsed = 0;
    for (const char character : value) {
        if (character < '0' || character > '9') {
            throw std::invalid_argument(std::string("invalid numeric value for ") + option);
        }
        const std::uint64_t digit = static_cast<std::uint64_t>(character - '0');
        if (parsed > (std::numeric_limits<std::uint64_t>::max() - digit) / 10ULL) {
            throw std::invalid_argument(std::string("numeric value overflows for ") + option);
        }
        parsed = parsed * 10ULL + digit;
    }
    return parsed;
}

std::size_t parse_threads(const std::string& value) {
    const std::uint64_t parsed = parse_decimal(value, "--threads");
    if (parsed == 0) {
        throw std::invalid_argument("--threads must be greater than zero");
    }
    if (parsed > static_cast<std::uint64_t>(std::numeric_limits<std::size_t>::max())) {
        throw std::invalid_argument("--threads exceeds the supported range");
    }
    return static_cast<std::size_t>(parsed);
}

std::uint64_t parse_memory_limit(const std::string& value) {
    if (value.empty()) {
        throw std::invalid_argument("missing value for --memory-limit");
    }

    std::string number = value;
    std::uint64_t multiplier = 1;
    const char final_character = value.back();
    switch (static_cast<char>(std::tolower(static_cast<unsigned char>(final_character)))) {
        case 'k':
            multiplier = 1024ULL;
            number.pop_back();
            break;
        case 'm':
            multiplier = kMiB;
            number.pop_back();
            break;
        case 'g':
            multiplier = kMiB * 1024ULL;
            number.pop_back();
            break;
        default:
            break;
    }

    const std::uint64_t parsed = parse_decimal(number, "--memory-limit");
    if (parsed > std::numeric_limits<std::uint64_t>::max() / multiplier) {
        throw std::invalid_argument("--memory-limit overflows bytes");
    }
    const std::uint64_t bytes = parsed * multiplier;
    if (bytes < kMinimumMemoryBytes) {
        throw std::invalid_argument("--memory-limit must be at least 64 MiB");
    }
    return bytes;
}

std::chrono::milliseconds parse_progress_interval(const std::string& value) {
    if (!value.empty() && value.front() == '-') {
        throw std::invalid_argument("--progress-interval must not be negative");
    }
    const std::uint64_t parsed = parse_decimal(value, "--progress-interval");
    const auto maximum = std::chrono::milliseconds::max().count();
    if (parsed > static_cast<std::uint64_t>(maximum)) {
        throw std::invalid_argument("--progress-interval exceeds the supported range");
    }
    return std::chrono::milliseconds{static_cast<std::chrono::milliseconds::rep>(parsed)};
}

OutputFormat parse_output_format(const std::string& value) {
    if (value == "vcf") {
        return OutputFormat::vcf;
    }
    if (value == "vcf.gz") {
        return OutputFormat::vcf_gz;
    }
    throw std::invalid_argument("--output-format must be vcf or vcf.gz");
}

OutputFormat infer_output_format(const std::filesystem::path& output) {
    const std::string filename = lower_ascii(output.string());
    if (ends_with(filename, ".vcf.gz")) {
        return OutputFormat::vcf_gz;
    }
    if (ends_with(filename, ".vcf")) {
        return OutputFormat::vcf;
    }
    throw std::invalid_argument(
        "output path must end in .vcf or .vcf.gz, or use --output-format");
}

const char* require_value(int argc, char** argv, int& index, const char* option) {
    if (index + 1 >= argc || argv[index + 1] == nullptr ||
        std::string(argv[index + 1]).rfind("--", 0) == 0) {
        throw std::invalid_argument(std::string("missing value for ") + option);
    }
    return argv[++index];
}

void mark_seen(std::unordered_set<std::string>& seen, const std::string& option) {
    if (!seen.insert(option).second) {
        throw std::invalid_argument("duplicate option: " + option);
    }
}

}  // namespace

Config parse_cli(int argc, char** argv) {
    if (argc > 0 && argv == nullptr) {
        throw std::invalid_argument("null command-line argument vector");
    }

    Config config;
    bool threads_specified = false;
    bool output_format_specified = false;
    bool help_requested = false;
    bool version_requested = false;
    std::unordered_set<std::string> seen;

    for (int index = 1; index < argc; ++index) {
        if (argv[index] == nullptr) {
            throw std::invalid_argument("null command-line argument");
        }
        const std::string option(argv[index]);
        if (option == "--help") {
            mark_seen(seen, option);
            help_requested = true;
            continue;
        }
        if (option == "--version") {
            mark_seen(seen, option);
            version_requested = true;
            continue;
        }
        if (option == "--quiet") {
            mark_seen(seen, option);
            config.quiet = true;
            continue;
        }
        if (option == "--force") {
            mark_seen(seen, option);
            config.force = true;
            continue;
        }
        if (option == "--variants") {
            mark_seen(seen, option);
            config.variants = require_value(argc, argv, index, "--variants");
            continue;
        }
        if (option == "--input") {
            mark_seen(seen, option);
            config.input = require_value(argc, argv, index, "--input");
            continue;
        }
        if (option == "--output") {
            mark_seen(seen, option);
            config.output = require_value(argc, argv, index, "--output");
            continue;
        }
        if (option == "--threads") {
            mark_seen(seen, option);
            config.threads = parse_threads(require_value(argc, argv, index, "--threads"));
            threads_specified = true;
            continue;
        }
        if (option == "--memory-limit") {
            mark_seen(seen, option);
            config.memory_limit_bytes =
                parse_memory_limit(require_value(argc, argv, index, "--memory-limit"));
            continue;
        }
        if (option == "--progress-interval") {
            mark_seen(seen, option);
            config.progress_interval = parse_progress_interval(
                require_value(argc, argv, index, "--progress-interval"));
            continue;
        }
        if (option == "--output-format") {
            mark_seen(seen, option);
            config.output_format = parse_output_format(
                require_value(argc, argv, index, "--output-format"));
            output_format_specified = true;
            continue;
        }
        throw std::invalid_argument("unknown option: " + option);
    }

    if (help_requested && version_requested) {
        throw std::invalid_argument("--help and --version are mutually exclusive");
    }
    if (help_requested) {
        throw UsageRequested(false);
    }
    if (version_requested) {
        throw UsageRequested(true);
    }

    if (config.variants.empty()) {
        throw std::invalid_argument("missing required option: --variants");
    }
    if (config.input.empty()) {
        throw std::invalid_argument("missing required option: --input");
    }
    if (config.output.empty()) {
        throw std::invalid_argument("missing required option: --output");
    }

    if (!threads_specified) {
        const unsigned int available = std::thread::hardware_concurrency();
        config.threads = available == 0 ? 1 : static_cast<std::size_t>(available);
    }
    if (!output_format_specified) {
        config.output_format = infer_output_format(config.output);
    }
    return config;
}

ThreadAllocation allocate_threads(const Config& config, bool compressed_input,
                                  bool compressed_output) {
    std::size_t remaining = config.threads == 0 ? 1 : config.threads;
    ThreadAllocation allocation;

    if (compressed_output && remaining > 1) {
        allocation.output_io_workers = 1;
        --remaining;
    }
    if (compressed_input && config.threads >= 4 && remaining > 1) {
        allocation.input_io_workers = 1;
        --remaining;
    }
    allocation.conversion_workers = remaining;
    return allocation;
}

const char* usage_text() {
    return R"(Usage: convert-to-biallelic --variants FILE --input FILE --output FILE [options]

Required options:
  --variants FILE              Variant annotation input file.
  --input FILE                 Input VCF or VCF.GZ file.
  --output FILE                Explicit output VCF file path.

Optional options:
  --threads N                  Conversion thread budget; default is logical CPU count.
  --memory-limit N[K|M|G]      Memory limit in bytes or binary K/M/G units; minimum 64 MiB.
  --progress-interval MS       Progress-report interval in milliseconds; default 5000.
  --quiet                      Suppress progress reporting.
  --force                      Permit replacing an existing output file.
  --output-format vcf|vcf.gz   Override output format inferred from --output.
  --help                       Print this help text.
  --version                    Print version information.

Progress is written to stdout. Diagnostics are written to stderr. VCF output is
written to the explicit file supplied by --output.
)";
}

}  // namespace ctb
```
