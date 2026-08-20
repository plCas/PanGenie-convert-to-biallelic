#include "convert_to_biallelic/progress.hpp"

#include <algorithm>
#include <iomanip>
#include <limits>
#include <ostream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <system_error>
#include <utility>

#include "convert_to_biallelic/types.hpp"

namespace ctb {
namespace {

constexpr std::uint64_t kBytesPerMebibyte = 1024ULL * 1024ULL;

std::string format_elapsed(
    std::chrono::steady_clock::duration duration) {
    using Seconds = std::chrono::seconds;
    const auto elapsed = std::max(Seconds::zero(),
                                  std::chrono::duration_cast<Seconds>(duration));
    const auto total_seconds = elapsed.count();
    const auto hours = total_seconds / 3600;
    const auto minutes = (total_seconds / 60) % 60;
    const auto seconds = total_seconds % 60;

    std::ostringstream formatted;
    formatted << std::setfill('0') << std::setw(2) << hours << ':'
              << std::setw(2) << minutes << ':' << std::setw(2) << seconds;
    return formatted.str();
}

std::uint64_t records_per_second(
    std::uint64_t records,
    std::chrono::steady_clock::duration duration) noexcept {
    const long double seconds =
        std::chrono::duration<long double>(duration).count();
    if (records == 0 || seconds <= 0.0L) {
        return 0;
    }

    const long double rate = static_cast<long double>(records) / seconds;
    const long double maximum = static_cast<long double>(
        std::numeric_limits<std::uint64_t>::max());
    return rate >= maximum ? std::numeric_limits<std::uint64_t>::max()
                           : static_cast<std::uint64_t>(rate);
}

std::string periodic_line(
    std::chrono::steady_clock::duration elapsed,
    std::uint64_t input_records,
    std::uint64_t output_records,
    std::uint64_t recent_rate,
    std::uint64_t tracked_memory) {
    std::ostringstream line;
    line << '[' << format_elapsed(elapsed) << "] input=" << input_records
         << " output=" << output_records << " rate=" << recent_rate
         << " records/s memory=" << tracked_memory / kBytesPerMebibyte
         << " MiB";
    return line.str();
}

}  // namespace

std::string format_final_summary(const PipelineStats& stats) {
    std::ostringstream line;
    line << "Finished: input=" << stats.input_records
         << " output=" << stats.output_records
         << " elapsed=" << format_elapsed(stats.elapsed)
         << " average=" << records_per_second(stats.input_records,
                                                stats.elapsed)
         << " records/s peak_tracked_memory="
         << stats.peak_tracked_bytes / kBytesPerMebibyte << " MiB";
    return line.str();
}

ProgressReporter::ProgressReporter(ProgressCounters& counters,
                                   std::chrono::milliseconds interval,
                                   std::ostream& output,
                                   bool quiet,
                                   std::function<void(std::exception_ptr)>
                                       error_callback)
    : counters_(counters),
      interval_(interval),
      output_(output),
      quiet_(quiet),
      error_callback_(std::move(error_callback)) {}

ProgressReporter::~ProgressReporter() {
    stop_without_summary();
}

void ProgressReporter::start() {
    std::lock_guard<std::mutex> lock(mutex_);
    if (state_ != State::idle) {
        throw std::logic_error("Progress reporter has already been started");
    }

    started_ = std::chrono::steady_clock::now();
    stopping_ = false;
    thread_error_ = nullptr;
    thread_exited_ = true;
    state_ = State::running;

    if (quiet_ || interval_ <= std::chrono::milliseconds::zero()) {
        return;
    }

    try {
        thread_exited_ = false;
        thread_ = std::thread(&ProgressReporter::run, this);
    } catch (...) {
        state_ = State::idle;
        stopping_ = false;
        thread_exited_ = true;
        throw;
    }
}

std::chrono::steady_clock::duration ProgressReporter::finish() {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (state_ != State::running) {
            throw std::logic_error(
                "Progress reporter is not running or was already finished");
        }
        if (thread_.joinable() &&
            thread_.get_id() == std::this_thread::get_id()) {
            throw std::logic_error(
                "Progress reporter cannot be finished from its own thread");
        }
        state_ = State::finishing;
        stopping_ = true;
    }
    changed_.notify_all();
    join_thread_safely();

    const auto elapsed = std::chrono::steady_clock::now() - started_;
    std::exception_ptr thread_error;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        state_ = State::finished;
        thread_error = thread_error_;
    }

    if (thread_error) {
        std::rethrow_exception(thread_error);
    }
    return elapsed;
}

void ProgressReporter::stop_without_summary() noexcept {
    try {
        {
            std::lock_guard<std::mutex> lock(mutex_);
            if (state_ != State::running) {
                return;
            }
            stopping_ = true;
            if (thread_.joinable() &&
                thread_.get_id() == std::this_thread::get_id()) {
                // The internal thread cannot join itself. Leave the reporter
                // in `running` state so an owning/control thread can join it
                // after this callback returns and run() exits.
                changed_.notify_all();
                return;
            }
            state_ = State::stopped;
        }
        changed_.notify_all();
        join_thread_safely();
    } catch (...) {
        record_error(std::current_exception());
    }
}

void ProgressReporter::record_error(std::exception_ptr error) noexcept {
    if (!error) {
        return;
    }

    bool first = false;
    try {
        {
            std::lock_guard<std::mutex> lock(mutex_);
            if (!thread_error_) {
                thread_error_ = error;
                first = true;
            }
            stopping_ = true;
        }
        changed_.notify_all();
    } catch (...) {
        std::terminate();
    }

    if (first && error_callback_) {
        try {
            error_callback_(error);
        } catch (...) {
            // The original reporter failure remains the authoritative error.
            // Error callbacks are notifications and must not escape this
            // no-throw reporter boundary.
        }
    }
}

void ProgressReporter::join_thread_safely() noexcept {
    if (!thread_.joinable()) {
        return;
    }
    if (thread_.get_id() == std::this_thread::get_id()) {
        // The public lifecycle rejects self-finish and leaves self-stop for an
        // owning control thread. Destroying `this` from run() is unsupported
        // and cannot be made lifetime-safe by detaching an active thread.
        std::terminate();
    }

    try {
        thread_.join();
        return;
    } catch (const std::system_error&) {
        record_error(std::current_exception());
    } catch (...) {
        record_error(std::current_exception());
    }

    try {
        std::unique_lock<std::mutex> lock(mutex_);
        changed_.wait(lock, [this] { return thread_exited_; });
    } catch (...) {
        record_error(std::current_exception());
        // Detaching before run() exits would permit use-after-destruction.
        std::terminate();
    }

    // Retry a normal join after confirmed thread exit. Detach is used only if
    // the platform still rejects that join, and is then lifetime-safe because
    // run() has published its terminal state already.
    try {
        thread_.join();
        return;
    } catch (const std::system_error&) {
        record_error(std::current_exception());
    } catch (...) {
        record_error(std::current_exception());
    }

    if (thread_.joinable()) {
        try {
            thread_.detach();
        } catch (...) {
            record_error(std::current_exception());
            std::terminate();
        }
    }
}

void ProgressReporter::run() noexcept {
    try {
        auto previous_time = std::chrono::steady_clock::now();
        std::uint64_t previous_input =
            counters_.input_records.load(std::memory_order_relaxed);

        for (;;) {
            {
                std::unique_lock<std::mutex> lock(mutex_);
                if (changed_.wait_for(lock, interval_,
                                      [this] { return stopping_; })) {
                    break;
                }
            }

            const auto now = std::chrono::steady_clock::now();
            const std::uint64_t input =
                counters_.input_records.load(std::memory_order_relaxed);
            const std::uint64_t output =
                counters_.output_records.load(std::memory_order_relaxed);
            const std::uint64_t memory =
                counters_.tracked_memory.load(std::memory_order_relaxed);
            const std::uint64_t delta =
                input >= previous_input ? input - previous_input : 0;
            const std::uint64_t rate =
                records_per_second(delta, now - previous_time);

            output_ << periodic_line(now - started_, input, output, rate,
                                     memory)
                    << '\n'
                    << std::flush;
            if (!output_) {
                throw std::runtime_error(
                    "Failed to write periodic progress output");
            }
            previous_time = now;
            previous_input = input;
        }
    } catch (...) {
        record_error(std::current_exception());
    }

    try {
        std::unique_lock<std::mutex> lock(mutex_);
        thread_exited_ = true;
        // Publish completion only at native thread exit. A join fallback can
        // therefore detach only after this reporter has stopped using `this`.
        std::notify_all_at_thread_exit(changed_, std::move(lock));
    } catch (...) {
        std::terminate();
    }
}

}  // namespace ctb
