#include "convert_to_biallelic/pipeline.hpp"

#include <algorithm>
#include <atomic>
#include <condition_variable>
#include <exception>
#include <functional>
#include <limits>
#include <map>
#include <memory>
#include <mutex>
#include <ostream>
#include <stdexcept>
#include <string>
#include <system_error>
#include <thread>
#include <utility>
#include <vector>

#include "convert_to_biallelic/bounded_queue.hpp"
#include "convert_to_biallelic/converter.hpp"
#include "convert_to_biallelic/progress.hpp"

namespace ctb {
namespace {

constexpr std::uint64_t kMinimumMemoryLimit =
    64ULL * 1024ULL * 1024ULL;

class OverageReporter {
public:
    explicit OverageReporter(std::ostream& diagnostics)
        : diagnostics_(diagnostics) {}

    void warn_for_input(std::uint64_t line_number) {
        warn("input line " + std::to_string(line_number));
    }

    void warn_for_sequence(std::uint64_t sequence) {
        warn("pipeline sequence " + std::to_string(sequence));
    }

private:
    void warn(const std::string& subject) {
        bool expected = false;
        if (!emitted_.compare_exchange_strong(
                expected, true, std::memory_order_acq_rel,
                std::memory_order_acquire)) {
            return;
        }

        std::lock_guard<std::mutex> lock(mutex_);
        diagnostics_ << "Warning: " << subject
                     << " requires the one permitted exceptional "
                        "tracked-memory overage"
                     << '\n'
                     << std::flush;
        if (!diagnostics_) {
            throw std::runtime_error(
                "Failed to write pipeline diagnostics");
        }
    }

    std::ostream& diagnostics_;
    std::mutex mutex_;
    std::atomic<bool> emitted_{false};
};

std::uint64_t checked_size(std::size_t value, const char* description) {
    if (value > static_cast<std::size_t>(
                    std::numeric_limits<std::uint64_t>::max())) {
        throw std::overflow_error(std::string(description) + " is too large");
    }
    return static_cast<std::uint64_t>(value);
}

std::uint64_t checked_add(std::uint64_t left,
                          std::uint64_t right,
                          const char* description) {
    if (right > std::numeric_limits<std::uint64_t>::max() - left) {
        throw std::overflow_error(std::string(description) + " overflow");
    }
    return left + right;
}

std::uint64_t checked_multiply(std::uint64_t left,
                               std::uint64_t right,
                               const char* description) {
    if (left != 0 &&
        right > std::numeric_limits<std::uint64_t>::max() / left) {
        throw std::overflow_error(std::string(description) + " overflow");
    }
    return left * right;
}

std::uint64_t string_storage_bytes(const std::string& value) {
    return checked_add(checked_size(value.capacity(), "string capacity"), 1,
                       "string storage");
}

std::uint64_t vector_storage_bytes(
    const std::vector<std::string>& records) {
    return checked_multiply(checked_size(records.capacity(),
                                         "record vector capacity"),
                            sizeof(std::string),
                            "record vector storage");
}

std::uint64_t work_storage_bytes(const RawWorkChunk& chunk) {
    std::uint64_t bytes = vector_storage_bytes(chunk.records);
    for (const std::string& record : chunk.records) {
        bytes = checked_add(bytes, string_storage_bytes(record),
                            "work chunk storage");
    }
    return bytes;
}

std::uint64_t result_storage_bytes(const RawResultChunk& chunk) {
    return string_storage_bytes(chunk.bytes);
}

std::size_t planned_vector_capacity(std::size_t current,
                                    std::size_t required,
                                    std::size_t target) {
    if (required <= current) {
        return current;
    }
    if (required > target) {
        throw std::length_error("Work chunk record target was exceeded");
    }

    std::size_t capacity = current == 0 ? 1 : current;
    while (capacity < required) {
        if (capacity >= target || capacity > target - capacity) {
            capacity = target;
        } else {
            capacity *= 2;
        }
    }
    return capacity;
}

std::uint64_t projected_work_storage(const WorkChunk& chunk,
                                     const std::string& next_record,
                                     std::size_t target_records) {
    if (chunk.data.records.size() ==
        std::numeric_limits<std::size_t>::max()) {
        throw std::overflow_error("Work chunk record count overflow");
    }

    const std::size_t required = chunk.data.records.size() + 1;
    const std::size_t planned = planned_vector_capacity(
        chunk.data.records.capacity(), required, target_records);
    const std::uint64_t added_slots = checked_size(
        planned - chunk.data.records.capacity(),
        "additional record vector capacity");
    const std::uint64_t added_vector_bytes = checked_multiply(
        added_slots, sizeof(std::string), "record vector growth");

    return checked_add(
        checked_add(chunk.permit.bytes(), added_vector_bytes,
                    "projected work chunk storage"),
        string_storage_bytes(next_record), "projected work chunk storage");
}

void publish_memory(ProgressCounters& counters,
                    MemoryBudget& budget,
                    std::mutex& publication_mutex) {
    // Serialize the read-and-publish pair so a delayed publisher cannot store
    // a snapshot older than one already published by another pipeline thread.
    std::lock_guard<std::mutex> lock(publication_mutex);
    counters.tracked_memory.store(budget.current_bytes(),
                                  std::memory_order_relaxed);
}

void resize_result_permit(
    MemoryPermit& permit,
    std::uint64_t bytes,
    std::uint64_t sequence,
    std::uint64_t tracked_allowance,
    const std::atomic<std::uint64_t>& next_written_sequence,
    const std::atomic<bool>& pipeline_cancelled,
    ProgressCounters& counters,
    MemoryBudget& budget,
    std::mutex& coordination_mutex,
    std::condition_variable& memory_changed,
    OverageReporter& overage_reporter);

void append_work_record(WorkChunk& chunk,
                        std::string& record,
                        std::uint64_t line_number,
                        std::size_t target_records,
                        std::uint64_t tracked_allowance,
                        const std::atomic<std::uint64_t>&
                            next_written_sequence,
                        const std::atomic<bool>& pipeline_cancelled,
                        ProgressCounters& counters,
                        MemoryBudget& budget,
                        std::mutex& publication_mutex,
                        std::condition_variable& memory_changed,
                        OverageReporter& overage_reporter) {
    const std::size_t required = chunk.data.records.size() + 1;
    const std::size_t planned = planned_vector_capacity(
        chunk.data.records.capacity(), required, target_records);
    const std::uint64_t planned_bytes = projected_work_storage(
        chunk, record, target_records);

    // vector::reserve keeps the old allocation alive while obtaining the new
    // one. Admit both allocations, plus the incoming record, until reserve
    // returns and the implementation-selected capacity can be reconciled.
    std::uint64_t reserve_time_bytes = planned_bytes;
    if (planned > chunk.data.records.capacity()) {
        reserve_time_bytes = checked_add(
            reserve_time_bytes, vector_storage_bytes(chunk.data.records),
            "reserve-time work chunk storage");
    }
    resize_result_permit(
        chunk.permit, reserve_time_bytes, chunk.data.sequence,
        tracked_allowance, next_written_sequence, pipeline_cancelled,
        counters, budget, publication_mutex, memory_changed,
        overage_reporter);
    if (planned > chunk.data.records.capacity()) {
        chunk.data.records.reserve(planned);
    }

    // The standard permits reserve() to choose a larger capacity than the
    // request. Reconcile that implementation-selected capacity before the
    // record string itself is moved into the vector.
    const std::uint64_t before_move = checked_add(
        work_storage_bytes(chunk.data), string_storage_bytes(record),
        "work chunk storage before record move");
    resize_result_permit(
        chunk.permit, before_move, chunk.data.sequence,
        tracked_allowance, next_written_sequence, pipeline_cancelled,
        counters, budget, publication_mutex, memory_changed,
        overage_reporter);

    if (chunk.data.records.empty()) {
        chunk.data.first_line_number = line_number;
    }
    chunk.data.records.push_back(std::move(record));

    // A nothrow string move normally transfers its capacity. Reconcile the
    // actual capacity so the permit remains exact even for an SSO move.
    resize_result_permit(
        chunk.permit, work_storage_bytes(chunk.data), chunk.data.sequence,
        tracked_allowance, next_written_sequence, pipeline_cancelled,
        counters, budget, publication_mutex, memory_changed,
        overage_reporter);
}

void checked_atomic_add(std::atomic<std::uint64_t>& counter,
                        std::uint64_t additional,
                        const char* description) {
    std::uint64_t current = counter.load(std::memory_order_relaxed);
    for (;;) {
        if (additional >
            std::numeric_limits<std::uint64_t>::max() - current) {
            throw std::overflow_error(std::string(description) + " overflow");
        }
        if (counter.compare_exchange_weak(
                current, current + additional, std::memory_order_relaxed,
                std::memory_order_relaxed)) {
            return;
        }
    }
}

std::uint64_t physical_line_number(std::uint64_t first,
                                   std::size_t offset) {
    const std::uint64_t converted_offset =
        checked_size(offset, "record line offset");
    return checked_add(first, converted_offset, "physical line number");
}

void resize_result_permit(
    MemoryPermit& permit,
    std::uint64_t bytes,
    std::uint64_t sequence,
    std::uint64_t tracked_allowance,
    const std::atomic<std::uint64_t>& next_written_sequence,
    const std::atomic<bool>& pipeline_cancelled,
    ProgressCounters& counters,
    MemoryBudget& budget,
    std::mutex& coordination_mutex,
    std::condition_variable& memory_changed,
    OverageReporter& overage_reporter) {
    if (permit.bytes() == bytes) {
        return;
    }

    std::unique_lock<std::mutex> lock(coordination_mutex);
    for (;;) {
        if (pipeline_cancelled.load(std::memory_order_acquire)) {
            throw std::runtime_error("Pipeline was cancelled");
        }

        const bool shrinking = bytes <= permit.bytes();
        const std::uint64_t current = budget.current_bytes();
        const std::uint64_t additional =
            shrinking ? 0 : bytes - permit.bytes();
        const bool fits_normally =
            shrinking ||
            (current <= tracked_allowance &&
             additional <= tracked_allowance - current);
        const std::uint64_t next =
            next_written_sequence.load(std::memory_order_acquire);
        if (next > sequence) {
            throw std::logic_error(
                "Result sequence advanced before conversion completed");
        }
        const bool is_next_sequence = next == sequence;

        if (fits_normally || is_next_sequence) {
            // A later sequence may use only normal capacity. The next result
            // required by the writer may claim or continue the sole overage,
            // so an over-budget result can never wait in the reorder map while
            // blocking an earlier sequence from finishing.
            permit.resize(bytes, is_next_sequence);
            const std::uint64_t current_after = budget.current_bytes();
            counters.tracked_memory.store(current_after,
                                          std::memory_order_relaxed);
            if (!shrinking && current_after > tracked_allowance) {
                overage_reporter.warn_for_sequence(sequence);
            }
            lock.unlock();
            memory_changed.notify_all();
            return;
        }

        memory_changed.wait(lock);
    }
}

ResultChunk convert_chunk(WorkChunk& work,
                          const AnnotationIndex& annotation,
                          CompatibilityMode compatibility_mode,
                          std::uint64_t tracked_allowance,
                          const std::atomic<std::uint64_t>&
                              next_written_sequence,
                          const std::atomic<bool>& pipeline_cancelled,
                          ProgressCounters& counters,
                          MemoryBudget& budget,
                          std::mutex& memory_publication_mutex,
                          std::condition_variable& memory_changed,
                          OverageReporter& overage_reporter) {
    const std::uint64_t input_records =
        checked_size(work.data.records.size(), "input record count");
    const std::uint64_t input_storage = work_storage_bytes(work.data);
    const std::uint64_t sequence = work.data.sequence;
    resize_result_permit(
        work.permit, input_storage, sequence, tracked_allowance,
        next_written_sequence, pipeline_cancelled, counters, budget,
        memory_publication_mutex, memory_changed, overage_reporter);

    RawResultChunk converted_chunk;
    converted_chunk.sequence = sequence;
    converted_chunk.input_records = input_records;

    resize_result_permit(
        work.permit,
        checked_add(input_storage, result_storage_bytes(converted_chunk),
                    "combined input and result storage"),
        sequence, tracked_allowance, next_written_sequence,
        pipeline_cancelled, counters, budget, memory_publication_mutex,
        memory_changed, overage_reporter);

    const std::function<void(std::size_t, std::size_t)> before_reserve =
        [&](std::size_t planned_output_bytes,
            std::size_t planned_scratch_bytes) {
            const std::uint64_t planned_result = checked_size(
                planned_output_bytes, "planned result storage");
            const std::uint64_t planned_scratch = checked_size(
                planned_scratch_bytes, "planned converter scratch size");
            resize_result_permit(
                work.permit,
                checked_add(
                    checked_add(input_storage, planned_result,
                                "combined input and result storage"),
                    planned_scratch,
                    "combined input, result, and converter scratch storage"),
                sequence, tracked_allowance, next_written_sequence,
                pipeline_cancelled, counters, budget,
                memory_publication_mutex, memory_changed,
                overage_reporter);
        };

    for (std::size_t index = 0; index < work.data.records.size(); ++index) {
        append_converted_record(
            work.data.records[index], annotation,
            physical_line_number(work.data.first_line_number, index),
            compatibility_mode,
            converted_chunk.bytes, converted_chunk.output_records,
            before_reserve);
    }

    // Release source storage before shrinking its reservation away. The
    // remaining permit then describes only the result string capacity.
    work.data = RawWorkChunk{};
    ResultChunk result;
    result.data = std::move(converted_chunk);
    result.permit = std::move(work.permit);
    resize_result_permit(
        result.permit, result_storage_bytes(result.data), sequence,
        tracked_allowance, next_written_sequence, pipeline_cancelled, counters,
        budget, memory_publication_mutex, memory_changed,
        overage_reporter);
    checked_atomic_add(counters.input_records, input_records,
                       "pipeline input record counter");
    return result;
}

class PipelineState {
public:
    bool capture_failure(std::exception_ptr error) noexcept {
        bool first = false;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            if (!first_error_) {
                first_error_ = error;
                cancelled_ = true;
                first = true;
            }
        }
        changed_.notify_all();
        return first;
    }

    std::exception_ptr first_error() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return first_error_;
    }

    bool cancelled() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return cancelled_;
    }

    bool wait_until_written(std::uint64_t count) {
        std::unique_lock<std::mutex> lock(mutex_);
        changed_.wait(lock, [this, count] {
            return cancelled_ || written_chunks_ >= count;
        });
        return !cancelled_;
    }

    void mark_written(std::uint64_t count) {
        {
            std::lock_guard<std::mutex> lock(mutex_);
            if (count < written_chunks_) {
                throw std::logic_error(
                    "Written chunk sequence moved backwards");
            }
            written_chunks_ = count;
        }
        changed_.notify_all();
    }

private:
    mutable std::mutex mutex_;
    std::condition_variable changed_;
    std::exception_ptr first_error_;
    std::uint64_t written_chunks_ = 0;
    bool cancelled_ = false;
};

class ThreadCompletion {
public:
    void mark_exited() noexcept {
        try {
            std::unique_lock<std::mutex> lock(mutex_);
            exited_ = true;
            // Keep the completion mutex locked until the native thread has
            // actually exited. A fallback waiter therefore cannot detach on
            // an about-to-exit signal while wrapper epilogue code is active.
            std::notify_all_at_thread_exit(changed_, std::move(lock));
        } catch (...) {
            // Without publishing completion, a failed join cannot safely
            // recover while pipeline threads still reference stack state.
            std::terminate();
        }
    }

    void wait_until_exited() {
        std::unique_lock<std::mutex> lock(mutex_);
        changed_.wait(lock, [this] { return exited_; });
    }

private:
    std::mutex mutex_;
    std::condition_variable changed_;
    bool exited_ = false;
};

class WorkerLifecycle {
public:
    void add_starting_worker() {
        std::lock_guard<std::mutex> lock(mutex_);
        if (spawning_finished_) {
            throw std::logic_error(
                "Cannot add a worker after spawning has finished");
        }
        if (active_workers_ ==
            std::numeric_limits<std::size_t>::max()) {
            throw std::overflow_error("Active worker count overflow");
        }
        ++active_workers_;
    }

    void rollback_unstarted_worker() {
        std::lock_guard<std::mutex> lock(mutex_);
        if (active_workers_ == 0) {
            throw std::logic_error(
                "Cannot roll back an absent starting worker");
        }
        --active_workers_;
    }

    bool worker_exited_and_should_close() {
        std::lock_guard<std::mutex> lock(mutex_);
        if (active_workers_ == 0) {
            throw std::logic_error(
                "Active worker count underflow");
        }
        --active_workers_;
        return claim_result_close_locked();
    }

    bool finish_spawning_and_should_close() {
        std::lock_guard<std::mutex> lock(mutex_);
        spawning_finished_ = true;
        return claim_result_close_locked();
    }

private:
    bool claim_result_close_locked() noexcept {
        if (!spawning_finished_ || active_workers_ != 0 ||
            result_close_claimed_) {
            return false;
        }
        result_close_claimed_ = true;
        return true;
    }

    std::mutex mutex_;
    std::size_t active_workers_ = 0;
    bool spawning_finished_ = false;
    bool result_close_claimed_ = false;
};

std::size_t queue_capacity(std::size_t conversion_workers) noexcept {
    const std::size_t maximum = std::numeric_limits<std::size_t>::max();
    if (conversion_workers > maximum / 2) {
        return maximum;
    }
    return std::max<std::size_t>(1, conversion_workers * 2);
}

std::uint64_t validate_and_calculate_allowance(
    const AnnotationIndex& annotation,
    const PipelineOptions& options) {
    if (options.threads.conversion_workers == 0) {
        throw std::invalid_argument(
            "Pipeline requires at least one conversion worker");
    }
    if (options.target_records_per_chunk == 0) {
        throw std::invalid_argument(
            "Pipeline record target must be positive");
    }
    if (options.target_bytes_per_chunk == 0) {
        throw std::invalid_argument(
            "Pipeline byte target must be positive");
    }
    if (options.memory_limit_bytes < kMinimumMemoryLimit) {
        throw std::invalid_argument(
            "Pipeline memory limit must be at least 64 MiB");
    }

    const std::uint64_t reserve = options.memory_limit_bytes / 10;
    const std::uint64_t after_reserve =
        options.memory_limit_bytes - reserve;
    if (annotation.estimated_bytes() >= after_reserve) {
        throw std::invalid_argument(
            "Annotation estimate plus the 10% reserve must be below the memory limit");
    }
    return after_reserve - annotation.estimated_bytes();
}

bool read_physical_line(InputSource& input,
                        std::string& line,
                        std::uint64_t& line_number) {
    if (!input.getline(line)) {
        return false;
    }
    if (line_number == std::numeric_limits<std::uint64_t>::max()) {
        throw std::overflow_error("Physical input line number overflow");
    }
    ++line_number;
    return true;
}

}  // namespace

PipelineStats run_pipeline(InputSource& input,
                           OutputSink& output,
                           const AnnotationIndex& annotation,
                           const PipelineOptions& options,
                           std::ostream& progress,
                           std::ostream& diagnostics) {
    if (std::addressof(progress) == std::addressof(diagnostics) ||
        progress.rdbuf() == diagnostics.rdbuf()) {
        throw std::invalid_argument(
            "Progress and diagnostics streams must not share an object or "
            "stream buffer");
    }

    const std::uint64_t tracked_allowance =
        validate_and_calculate_allowance(annotation, options);

    // Header I/O is deliberately complete before any conversion, writer, or
    // reporter thread is started. The first non-header line is retained for
    // the streaming reader below.
    std::uint64_t line_number = 0;
    std::uint64_t header_output_bytes = 0;
    std::string line;
    std::string first_data_line;
    std::uint64_t first_data_line_number = 0;
    bool has_first_data_line = false;

    while (read_physical_line(input, line, line_number)) {
        if (!line.empty() && line.front() == '#') {
            const auto converted = convert_header(
                line, options.compatibility_mode);
            if (converted) {
                const std::uint64_t updated_header_bytes = checked_add(
                    header_output_bytes,
                    checked_size(converted->size(), "header output size"),
                    "header output byte counter");
                output.write(*converted);
                header_output_bytes = updated_header_bytes;
            }
            continue;
        }

        first_data_line = std::move(line);
        first_data_line_number = line_number;
        has_first_data_line = true;
        break;
    }

    ProgressCounters counters;
    MemoryBudget memory_budget(tracked_allowance);
    const std::size_t capacity =
        queue_capacity(options.threads.conversion_workers);
    BoundedQueue<WorkChunk> work_queue(capacity);
    BoundedQueue<ResultChunk> result_queue(capacity);
    PipelineState state;
    std::mutex memory_publication_mutex;
    std::condition_variable memory_changed;
    std::atomic<std::uint64_t> submitted_chunks{0};
    std::atomic<std::uint64_t> next_written_sequence{0};
    std::atomic<std::uint64_t> output_bytes{header_output_bytes};
    std::atomic<bool> pipeline_cancelled{false};
    WorkerLifecycle worker_lifecycle;
    OverageReporter overage_reporter(diagnostics);

    auto record_failure = [&](std::exception_ptr error) noexcept {
        if (error && state.capture_failure(error)) {
            // Do not hold the failure-state mutex while entering any memory or
            // queue operation. Each cancellation path wakes its waiters.
            pipeline_cancelled.store(true, std::memory_order_release);
            memory_budget.cancel();
            memory_changed.notify_all();
            work_queue.cancel();
            result_queue.cancel();
        }
    };

    ProgressReporter reporter(
        counters, options.progress_interval, progress, options.quiet,
        [&](std::exception_ptr error) noexcept { record_failure(error); });

    auto writer_body = [&] {
        try {
            std::map<std::uint64_t, ResultChunk> pending;
            std::uint64_t next_sequence = 0;
            ResultChunk incoming;

            while (result_queue.pop(incoming)) {
                const std::uint64_t sequence = incoming.data.sequence;
                if (sequence < next_sequence) {
                    throw std::runtime_error(
                        "Duplicate result sequence " +
                        std::to_string(sequence));
                }

                const auto insertion =
                    pending.emplace(sequence, std::move(incoming));
                if (!insertion.second) {
                    throw std::runtime_error(
                        "Duplicate result sequence " +
                        std::to_string(sequence));
                }

                for (;;) {
                    auto ready = pending.find(next_sequence);
                    if (ready == pending.end()) {
                        break;
                    }

                    const std::uint64_t output_record_total = checked_add(
                        counters.output_records.load(
                            std::memory_order_relaxed),
                        ready->second.data.output_records,
                        "pipeline output record counter");
                    const std::uint64_t output_byte_total = checked_add(
                        output_bytes.load(std::memory_order_relaxed),
                        checked_size(ready->second.data.bytes.size(),
                                     "result output size"),
                        "pipeline output byte counter");

                    output.write(ready->second.data.bytes);
                    counters.output_records.store(output_record_total,
                                                  std::memory_order_relaxed);
                    output_bytes.store(output_byte_total,
                                       std::memory_order_relaxed);

                    if (next_sequence ==
                        std::numeric_limits<std::uint64_t>::max()) {
                        throw std::overflow_error(
                            "Writer result sequence overflow");
                    }
                    ++next_sequence;
                    pending.erase(ready);
                    publish_memory(counters, memory_budget,
                                   memory_publication_mutex);
                    next_written_sequence.store(next_sequence,
                                                std::memory_order_release);
                    memory_changed.notify_all();
                    state.mark_written(next_sequence);
                }
            }

            const std::uint64_t expected =
                submitted_chunks.load(std::memory_order_acquire);
            if (!pending.empty() || next_sequence != expected) {
                throw std::runtime_error(
                    "Result queue closed with a missing sequence: expected " +
                    std::to_string(next_sequence) + " of " +
                    std::to_string(expected));
            }
        } catch (...) {
            record_failure(std::current_exception());
        }
    };

    auto worker_body = [&] {
        try {
            WorkChunk work;
            while (work_queue.pop(work)) {
                ResultChunk result = convert_chunk(
                    work, annotation, options.compatibility_mode,
                    tracked_allowance,
                    next_written_sequence, pipeline_cancelled, counters,
                    memory_budget, memory_publication_mutex, memory_changed,
                    overage_reporter);
                result_queue.push(std::move(result));
            }
        } catch (...) {
            record_failure(std::current_exception());
        }

        bool should_close_results = false;
        try {
            should_close_results =
                worker_lifecycle.worker_exited_and_should_close();
        } catch (...) {
            record_failure(std::current_exception());
        }

        if (should_close_results) {
            try {
                result_queue.close();
            } catch (...) {
                record_failure(std::current_exception());
            }
        }
    };

    std::thread writer_thread;
    std::vector<std::thread> worker_threads;
    std::shared_ptr<ThreadCompletion> writer_completion;
    std::vector<std::shared_ptr<ThreadCompletion>> worker_completions;

    auto join_safely = [&](
                           std::thread& thread,
                           const std::shared_ptr<ThreadCompletion>& completion)
        noexcept {
        if (!thread.joinable()) {
            return;
        }
        if (thread.get_id() == std::this_thread::get_id() || !completion) {
            // run_pipeline owns and joins these threads only from its reader
            // context. Detaching an active self-thread would invalidate every
            // reference capture, so fail closed if that invariant is broken.
            std::terminate();
        }

        try {
            thread.join();
            return;
        } catch (const std::system_error&) {
            record_failure(std::current_exception());
        } catch (...) {
            record_failure(std::current_exception());
        }

        try {
            completion->wait_until_exited();
        } catch (...) {
            record_failure(std::current_exception());
            // Stack-captured pipeline state must outlive every active thread.
            // A wait failure therefore cannot fall through to active detach.
            std::terminate();
        }

        // Once the wrapper has published completion, none of its stack
        // references can be used again. Retry join, then detach only the
        // already-exited native thread as a last-resort handle cleanup.
        try {
            thread.join();
            return;
        } catch (const std::system_error&) {
            record_failure(std::current_exception());
        } catch (...) {
            record_failure(std::current_exception());
        }

        if (thread.joinable()) {
            try {
                thread.detach();
            } catch (...) {
                record_failure(std::current_exception());
                std::terminate();
            }
        }
    };

    auto finish_worker_spawning = [&]() noexcept {
        try {
            if (worker_lifecycle.finish_spawning_and_should_close()) {
                result_queue.close();
            }
        } catch (...) {
            record_failure(std::current_exception());
        }
    };

    try {
        reporter.start();
        writer_completion = std::make_shared<ThreadCompletion>();
        writer_thread = std::thread(
            [&, completion = writer_completion]() noexcept {
                try {
                    writer_body();
                } catch (...) {
                    record_failure(std::current_exception());
                }
                completion->mark_exited();
            });
        worker_threads.reserve(options.threads.conversion_workers);
        worker_completions.reserve(options.threads.conversion_workers);
        for (std::size_t remaining =
                 options.threads.conversion_workers;
             remaining != 0; --remaining) {
            if (state.cancelled()) {
                break;
            }

            auto completion = std::make_shared<ThreadCompletion>();
            worker_completions.push_back(completion);
            try {
                worker_lifecycle.add_starting_worker();
            } catch (...) {
                worker_completions.pop_back();
                throw;
            }
            try {
                worker_threads.emplace_back(
                    [&, completion = std::move(completion)]() noexcept {
                        try {
                            worker_body();
                        } catch (...) {
                            record_failure(std::current_exception());
                        }
                        completion->mark_exited();
                    });
            } catch (...) {
                const std::exception_ptr creation_error =
                    std::current_exception();
                worker_completions.pop_back();
                record_failure(creation_error);
                try {
                    worker_lifecycle.rollback_unstarted_worker();
                } catch (...) {
                    record_failure(std::current_exception());
                }
                throw;
            }
        }
        finish_worker_spawning();
        if (state.cancelled()) {
            throw std::runtime_error(
                "Pipeline was cancelled during worker creation");
        }

        WorkChunk chunk;
        std::uint64_t chunk_input_bytes = 0;
        std::uint64_t next_sequence = 0;
        const std::uint64_t in_flight_limit =
            checked_size(capacity, "in-flight chunk limit");

        auto begin_chunk = [&] {
            if (next_sequence ==
                std::numeric_limits<std::uint64_t>::max()) {
                throw std::overflow_error("Reader chunk sequence overflow");
            }
            if (next_sequence >= in_flight_limit) {
                const std::uint64_t minimum_written =
                    next_sequence - in_flight_limit + 1;
                if (!state.wait_until_written(minimum_written)) {
                    throw std::runtime_error("Pipeline was cancelled");
                }
            }
            chunk = WorkChunk{};
            chunk.data.sequence = next_sequence;
            chunk.permit = memory_budget.acquire(0, false);
            publish_memory(counters, memory_budget,
                           memory_publication_mutex);
        };

        auto submit_chunk = [&] {
            if (chunk.data.records.empty()) {
                return;
            }
            work_queue.push(std::move(chunk));
            ++next_sequence;
            submitted_chunks.store(next_sequence, std::memory_order_release);
            chunk = WorkChunk{};
            chunk_input_bytes = 0;
        };

        auto process_data_line = [&](std::string& record,
                                     std::uint64_t record_line_number) {
            if (!record.empty() && record.front() == '#') {
                throw std::runtime_error(
                    "Input line " + std::to_string(record_line_number) +
                    ": header line encountered after data began");
            }

            const std::uint64_t record_bytes =
                checked_size(record.size(), "input record size");
            if (chunk.permit.bytes() == 0 && chunk.data.records.empty()) {
                begin_chunk();
            }

            std::uint64_t projected = projected_work_storage(
                chunk, record, options.target_records_per_chunk);
            const bool exceeds_byte_target =
                record_bytes > options.target_bytes_per_chunk ||
                chunk_input_bytes >
                    options.target_bytes_per_chunk -
                        std::min(record_bytes,
                                 options.target_bytes_per_chunk);
            const bool exceeds_memory_target =
                projected > tracked_allowance;

            if (!chunk.data.records.empty() &&
                (exceeds_byte_target || exceeds_memory_target)) {
                submit_chunk();
                begin_chunk();
                projected = projected_work_storage(
                    chunk, record, options.target_records_per_chunk);
            }

            const bool single_record_overage =
                chunk.data.records.empty() && projected > tracked_allowance;
            if (single_record_overage) {
                // An input overage must never sit behind older work while it
                // owns the sole overage slot. Drain all prior sequences first,
                // then the worker that pops this chunk can grow the same permit
                // without deadlocking other workers.
                if (!state.wait_until_written(next_sequence)) {
                    throw std::runtime_error("Pipeline was cancelled");
                }
                overage_reporter.warn_for_input(record_line_number);
            }

            append_work_record(
                chunk, record, record_line_number,
                options.target_records_per_chunk, tracked_allowance,
                next_written_sequence, pipeline_cancelled, counters,
                memory_budget, memory_publication_mutex, memory_changed,
                overage_reporter);
            chunk_input_bytes = checked_add(
                chunk_input_bytes, record_bytes, "chunk input byte count");

            if (chunk.data.records.size() >=
                    options.target_records_per_chunk ||
                chunk_input_bytes >= options.target_bytes_per_chunk ||
                single_record_overage) {
                submit_chunk();
            }
        };

        if (has_first_data_line) {
            process_data_line(first_data_line, first_data_line_number);
        }
        while (read_physical_line(input, line, line_number)) {
            process_data_line(line, line_number);
        }
        submit_chunk();
        work_queue.close();
    } catch (...) {
        record_failure(std::current_exception());
    }
    finish_worker_spawning();

    for (std::size_t index = 0; index < worker_threads.size(); ++index) {
        join_safely(worker_threads[index], worker_completions[index]);
    }
    join_safely(writer_thread, writer_completion);

    if (const std::exception_ptr error = state.first_error()) {
        reporter.stop_without_summary();
        std::rethrow_exception(error);
    }

    try {
        publish_memory(counters, memory_budget, memory_publication_mutex);
        PipelineStats stats;
        stats.input_records =
            counters.input_records.load(std::memory_order_relaxed);
        stats.output_records =
            counters.output_records.load(std::memory_order_relaxed);
        stats.output_bytes = output_bytes.load(std::memory_order_relaxed);
        stats.peak_tracked_bytes = memory_budget.peak_bytes();
        stats.elapsed = reporter.finish();
        return stats;
    } catch (...) {
        reporter.stop_without_summary();
        throw;
    }
}

}  // namespace ctb
