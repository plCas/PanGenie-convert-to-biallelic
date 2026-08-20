#include "convert_to_biallelic/annotation_index.hpp"
#include "convert_to_biallelic/cli.hpp"
#include "convert_to_biallelic/output_transaction.hpp"
#include "convert_to_biallelic/pipeline.hpp"
#include "convert_to_biallelic/progress.hpp"
#include "convert_to_biallelic/vcf_io.hpp"

#include <exception>
#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <string>
#include <string_view>

namespace {

constexpr std::string_view kVersion = "convert-to-biallelic 0.1.0\n";

void write_stdout(std::string_view text) {
    std::cout << text << std::flush;
    if (!std::cout) {
        throw std::runtime_error("Failed to write standard output");
    }
}

void report_error(std::string_view message) noexcept {
    try {
        std::cerr << "Error: " << message << '\n' << std::flush;
    } catch (...) {
        // There is no secondary reporting channel. Preserve the primary
        // failure and its exit code if stderr itself is unavailable.
    }
}

ctb::AnnotationIndex load_annotation_from_config(const ctb::Config& config) {
    // Annotation loading is intentionally single-streamed and uses no HTSlib
    // I/O workers so the full configured thread budget remains available to
    // the subsequently opened conversion input and output.
    ctb::InputSource annotation_input(config.variants, 0);
    ctb::AnnotationIndex annotation =
        ctb::load_annotation(annotation_input, config.memory_limit_bytes);
    annotation_input.close();
    return annotation;
}

int run_conversion(const ctb::Config& config) {
    ctb::AnnotationIndex annotation = load_annotation_from_config(config);

    const bool compressed_output =
        config.output_format == ctb::OutputFormat::vcf_gz;
    // InputSource discovers compression only after hts_open. Pass the worker
    // count that would apply to compressed input; the constructor ignores it
    // for plain input. This keeps the conversion input single-open while the
    // final allocation still returns that unit to conversion for plain VCF.
    const ctb::ThreadAllocation compressed_candidate =
        ctb::allocate_threads(config, true, compressed_output);
    ctb::InputSource input(config.input,
                           compressed_candidate.input_io_workers);
    const ctb::ThreadAllocation threads =
        ctb::allocate_threads(config, input.compressed(), compressed_output);

    ctb::PipelineOptions options;
    options.threads = threads;
    options.memory_limit_bytes = config.memory_limit_bytes;
    options.progress_interval = config.progress_interval;
    options.quiet = config.quiet;
    options.compatibility_mode = config.compatibility_mode;

    ctb::OutputTransaction transaction(config.output, config.force);
    const int sink_fd = transaction.take_sink_fd();
    ctb::OutputSink output(sink_fd, transaction.temporary_path(),
                           config.output_format,
                           threads.output_io_workers);

    ctb::PipelineStats stats =
        ctb::run_pipeline(input, output, annotation, options,
                          std::cout, std::cerr);

    input.close();
    output.flush();
    output.close();
    transaction.commit();

    if (!config.quiet) {
        const std::string summary = ctb::format_final_summary(stats);
        write_stdout(summary + "\n");
    }
    return 0;
}

}  // namespace

int main(int argc, char** argv) {
    ctb::Config config;
    try {
        try {
            config = ctb::parse_cli(argc, argv);
        } catch (const ctb::UsageRequested& request) {
            write_stdout(request.version() ? kVersion
                                           : std::string_view(ctb::usage_text()));
            return 0;
        } catch (const std::invalid_argument& error) {
            report_error(error.what());
            return 2;
        }

        return run_conversion(config);
    } catch (const std::exception& error) {
        report_error(error.what());
        return 1;
    } catch (...) {
        report_error("Unknown non-standard exception");
        return 1;
    }
}
