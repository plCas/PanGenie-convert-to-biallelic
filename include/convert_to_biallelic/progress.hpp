#pragma once

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <exception>
#include <functional>
#include <iosfwd>
#include <mutex>
#include <string>
#include <thread>

namespace ctb {

struct PipelineStats;

struct ProgressCounters {
    std::atomic<std::uint64_t> input_records{0};
    std::atomic<std::uint64_t> output_records{0};
    std::atomic<std::uint64_t> tracked_memory{0};
};

class ProgressReporter {
public:
    ProgressReporter(ProgressCounters& counters,
                     std::chrono::milliseconds interval,
                     std::ostream& output,
                     bool quiet,
                     std::function<void(std::exception_ptr)> error_callback =
                         {});
    ~ProgressReporter();

    ProgressReporter(const ProgressReporter&) = delete;
    ProgressReporter& operator=(const ProgressReporter&) = delete;
    ProgressReporter(ProgressReporter&&) = delete;
    ProgressReporter& operator=(ProgressReporter&&) = delete;

    void start();
    std::chrono::steady_clock::duration finish();
    void stop_without_summary() noexcept;

private:
    enum class State { idle, running, finishing, stopped, finished };

    void record_error(std::exception_ptr error) noexcept;
    void join_thread_safely() noexcept;
    void run() noexcept;

    ProgressCounters& counters_;
    std::chrono::milliseconds interval_;
    std::ostream& output_;
    bool quiet_;
    std::function<void(std::exception_ptr)> error_callback_;

    std::mutex mutex_;
    std::condition_variable changed_;
    std::thread thread_;
    std::chrono::steady_clock::time_point started_{};
    std::exception_ptr thread_error_;
    State state_ = State::idle;
    bool stopping_ = false;
    bool thread_exited_ = true;
};

std::string format_final_summary(const PipelineStats& stats);

}  // namespace ctb
