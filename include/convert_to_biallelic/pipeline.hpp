#pragma once

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <iosfwd>
#include <type_traits>

#include "convert_to_biallelic/annotation_index.hpp"
#include "convert_to_biallelic/memory_budget.hpp"
#include "convert_to_biallelic/types.hpp"
#include "convert_to_biallelic/vcf_io.hpp"

namespace ctb {

struct WorkChunk {
    RawWorkChunk data;
    MemoryPermit permit;
};

struct ResultChunk {
    RawResultChunk data;
    MemoryPermit permit;
};

static_assert(std::is_nothrow_move_constructible<WorkChunk>::value,
              "WorkChunk must be nothrow move constructible");
static_assert(std::is_nothrow_move_assignable<WorkChunk>::value,
              "WorkChunk must be nothrow move assignable");
static_assert(std::is_nothrow_destructible<WorkChunk>::value,
              "WorkChunk must be nothrow destructible");
static_assert(std::is_nothrow_move_constructible<ResultChunk>::value,
              "ResultChunk must be nothrow move constructible");
static_assert(std::is_nothrow_move_assignable<ResultChunk>::value,
              "ResultChunk must be nothrow move assignable");
static_assert(std::is_nothrow_destructible<ResultChunk>::value,
              "ResultChunk must be nothrow destructible");

struct PipelineOptions {
    ThreadAllocation threads;
    std::uint64_t memory_limit_bytes =
        2ULL * 1024ULL * 1024ULL * 1024ULL;
    std::size_t target_records_per_chunk = 512;
    std::uint64_t target_bytes_per_chunk =
        8ULL * 1024ULL * 1024ULL;
    std::chrono::milliseconds progress_interval{5000};
    bool quiet = false;
    CompatibilityMode compatibility_mode = CompatibilityMode::strict;
};

PipelineStats run_pipeline(InputSource& input,
                           OutputSink& output,
                           const AnnotationIndex& annotation,
                           const PipelineOptions& options,
                           std::ostream& progress,
                           std::ostream& diagnostics);

}  // namespace ctb
