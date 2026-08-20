# Task 6 Full-File Static Review Package

## No-Git-base note

This source-only review did not use a Git base, diff, status, commit, branch, or
worktree. The package below contains exact fenced copies of all current Task 6
API and implementation files.

## Review boundary

The exact files below were compared statically with
`.superpowers/sdd/task-6-brief.md` and the memory/queue portions of the approved
source-only plan. The move/release/resize/overage/cancel/wait transition audit,
overflow analysis, lock-order review, and deferred runtime verification are
documented in `.superpowers/sdd/task-6-report.md`.

No compiler, build, executable, test, benchmark, or Git command was run.

## `include/convert_to_biallelic/memory_budget.hpp`

```cpp
#pragma once

#include <condition_variable>
#include <cstdint>
#include <mutex>

namespace ctb {

class MemoryBudget;

// A nonempty permit is bound to its originating budget. That budget must
// outlive the permit, and one permit object must not be accessed concurrently.
class MemoryPermit {
public:
    MemoryPermit() = default;
    ~MemoryPermit() noexcept;

    MemoryPermit(MemoryPermit&& other) noexcept;
    MemoryPermit& operator=(MemoryPermit&& other) noexcept;

    MemoryPermit(const MemoryPermit&) = delete;
    MemoryPermit& operator=(const MemoryPermit&) = delete;

    std::uint64_t bytes() const noexcept;
    void resize(std::uint64_t bytes, bool allow_one_overage);

private:
    MemoryPermit(MemoryBudget* owner,
                 std::uint64_t bytes,
                 bool overage) noexcept;

    void release() noexcept;

    MemoryBudget* owner_ = nullptr;
    std::uint64_t bytes_ = 0;
    bool overage_ = false;

    friend class MemoryBudget;
};

class MemoryBudget {
public:
    explicit MemoryBudget(std::uint64_t tracked_limit);

    MemoryPermit acquire(std::uint64_t bytes, bool allow_one_overage);
    void cancel();

    std::uint64_t current_bytes() const;
    std::uint64_t peak_bytes() const;

private:
    bool fits_normal_locked(std::uint64_t additional) const noexcept;
    void add_locked(std::uint64_t bytes) noexcept;
    void subtract_locked(std::uint64_t bytes) noexcept;
    void resize(MemoryPermit& permit,
                std::uint64_t bytes,
                bool allow_one_overage);
    void release(std::uint64_t bytes, bool overage) noexcept;

    mutable std::mutex mutex_;
    std::condition_variable changed_;
    std::uint64_t limit_;

    // During the one permitted overage, the exact tracked sum can require 65
    // bits even though every reservation is uint64_t. current_high_ represents
    // the 2^64 bit and current_ stores the low 64 bits. Public reports saturate.
    std::uint64_t current_ = 0;
    bool current_high_ = false;
    std::uint64_t peak_ = 0;

    bool cancelled_ = false;
    bool overage_active_ = false;

    friend class MemoryPermit;
};

}  // namespace ctb
```

## `src/memory_budget.cpp`

```cpp
#include "convert_to_biallelic/memory_budget.hpp"

#include <algorithm>
#include <limits>
#include <stdexcept>

namespace ctb {
namespace {

constexpr const char* kCancelledMessage = "Memory budget is cancelled";

}  // namespace

MemoryPermit::MemoryPermit(MemoryBudget* owner,
                           std::uint64_t bytes,
                           bool overage) noexcept
    : owner_(owner), bytes_(bytes), overage_(overage) {}

MemoryPermit::~MemoryPermit() noexcept {
    release();
}

MemoryPermit::MemoryPermit(MemoryPermit&& other) noexcept
    : owner_(other.owner_),
      bytes_(other.bytes_),
      overage_(other.overage_) {
    other.owner_ = nullptr;
    other.bytes_ = 0;
    other.overage_ = false;
}

MemoryPermit& MemoryPermit::operator=(MemoryPermit&& other) noexcept {
    if (this == &other) {
        return *this;
    }

    release();
    owner_ = other.owner_;
    bytes_ = other.bytes_;
    overage_ = other.overage_;
    other.owner_ = nullptr;
    other.bytes_ = 0;
    other.overage_ = false;
    return *this;
}

std::uint64_t MemoryPermit::bytes() const noexcept {
    return bytes_;
}

void MemoryPermit::resize(std::uint64_t bytes,
                          bool allow_one_overage) {
    if (owner_ == nullptr) {
        throw std::logic_error(
            "Cannot resize an empty or moved-from memory permit");
    }
    owner_->resize(*this, bytes, allow_one_overage);
}

void MemoryPermit::release() noexcept {
    if (owner_ == nullptr) {
        return;
    }

    owner_->release(bytes_, overage_);
    owner_ = nullptr;
    bytes_ = 0;
    overage_ = false;
}

MemoryBudget::MemoryBudget(std::uint64_t tracked_limit)
    : limit_(tracked_limit) {
    if (tracked_limit == 0) {
        throw std::invalid_argument(
            "Memory budget tracked limit must be positive");
    }
}

MemoryPermit MemoryBudget::acquire(std::uint64_t bytes,
                                   bool allow_one_overage) {
    std::unique_lock<std::mutex> lock(mutex_);
    if (cancelled_) {
        throw std::runtime_error(kCancelledMessage);
    }
    if (bytes == 0) {
        return MemoryPermit(this, 0, false);
    }

    changed_.wait(lock, [this, bytes, allow_one_overage] {
        return cancelled_ || fits_normal_locked(bytes) ||
               (allow_one_overage && !overage_active_);
    });

    if (cancelled_) {
        throw std::runtime_error(kCancelledMessage);
    }

    const bool overage = !fits_normal_locked(bytes);
    add_locked(bytes);
    if (overage) {
        overage_active_ = true;
    }
    return MemoryPermit(this, bytes, overage);
}

void MemoryBudget::cancel() {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (cancelled_) {
            return;
        }
        cancelled_ = true;
    }
    changed_.notify_all();
}

std::uint64_t MemoryBudget::current_bytes() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return current_high_ ? std::numeric_limits<std::uint64_t>::max()
                         : current_;
}

std::uint64_t MemoryBudget::peak_bytes() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return peak_;
}

bool MemoryBudget::fits_normal_locked(
    std::uint64_t additional) const noexcept {
    return !current_high_ && current_ <= limit_ &&
           additional <= limit_ - current_;
}

void MemoryBudget::add_locked(std::uint64_t bytes) noexcept {
    const std::uint64_t maximum =
        std::numeric_limits<std::uint64_t>::max();

    if (current_high_) {
        // A valid budget state cannot exceed twice UINT64_MAX: before an
        // overage the aggregate is at most the limit, and the sole overage
        // permit itself is at most UINT64_MAX. Clamp only as a defensive guard
        // against a corrupted state.
        current_ = bytes > maximum - current_ ? maximum : current_ + bytes;
        peak_ = maximum;
        return;
    }

    if (bytes > maximum - current_) {
        current_ = bytes - (maximum - current_) - 1;
        current_high_ = true;
        peak_ = maximum;
        return;
    }

    current_ += bytes;
    peak_ = std::max(peak_, current_);
}

void MemoryBudget::subtract_locked(std::uint64_t bytes) noexcept {
    const std::uint64_t maximum =
        std::numeric_limits<std::uint64_t>::max();

    if (current_high_) {
        if (bytes <= current_) {
            current_ -= bytes;
            return;
        }

        current_ = maximum - (bytes - current_ - 1);
        current_high_ = false;
        return;
    }

    // Correctly owned permits always satisfy bytes <= current_. Avoid unsigned
    // wrap if a future caller violates that accounting invariant.
    current_ = bytes > current_ ? 0 : current_ - bytes;
}

void MemoryBudget::resize(MemoryPermit& permit,
                          std::uint64_t bytes,
                          bool allow_one_overage) {
    std::unique_lock<std::mutex> lock(mutex_);
    if (cancelled_) {
        throw std::runtime_error(kCancelledMessage);
    }

    if (bytes <= permit.bytes_) {
        const std::uint64_t released = permit.bytes_ - bytes;
        subtract_locked(released);
        permit.bytes_ = bytes;

        bool cleared_overage = false;
        if (permit.overage_ && fits_normal_locked(0)) {
            permit.overage_ = false;
            overage_active_ = false;
            cleared_overage = true;
        }

        lock.unlock();
        if (released != 0 || cleared_overage) {
            changed_.notify_all();
        }
        return;
    }

    const std::uint64_t additional = bytes - permit.bytes_;
    changed_.wait(lock, [this, &permit, additional, allow_one_overage] {
        return cancelled_ || fits_normal_locked(additional) ||
               (allow_one_overage &&
                (permit.overage_ || !overage_active_));
    });

    if (cancelled_) {
        throw std::runtime_error(kCancelledMessage);
    }

    const bool fits_normally = fits_normal_locked(additional);
    add_locked(additional);
    permit.bytes_ = bytes;

    bool cleared_overage = false;
    if (fits_normally) {
        if (permit.overage_) {
            permit.overage_ = false;
            overage_active_ = false;
            cleared_overage = true;
        }
    } else if (!permit.overage_) {
        permit.overage_ = true;
        overage_active_ = true;
    }

    lock.unlock();
    if (cleared_overage) {
        changed_.notify_all();
    }
}

void MemoryBudget::release(std::uint64_t bytes, bool overage) noexcept {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        subtract_locked(bytes);
        if (overage) {
            overage_active_ = false;
        }
    }
    changed_.notify_all();
}

}  // namespace ctb
```

## `include/convert_to_biallelic/bounded_queue.hpp`

```cpp
#pragma once

#include <condition_variable>
#include <cstddef>
#include <deque>
#include <mutex>
#include <optional>
#include <stdexcept>
#include <type_traits>
#include <utility>

namespace ctb {

// Lock ordering: callers must acquire or resize memory permits outside queue
// operations. The queue never calls a memory budget while holding its mutex.
// Cancellation moves queued values out before destroying them, and pop assigns
// to the caller's output only after unlocking, so permit releases do not nest a
// memory-budget lock inside the queue lock. Element move construction and the
// destruction of a moved-from element must likewise not acquire/resize memory.
// As with other synchronization containers, all users must stop before the
// queue itself is destroyed. The nothrow element constraints make queue-lock
// transitions indivisible with respect to element moves and destruction; they
// do not replace the rule that those operations must avoid memory-budget locks.
template <class T>
class BoundedQueue {
    static_assert(std::is_nothrow_move_constructible<T>::value,
                  "BoundedQueue<T> requires nothrow move construction");
    static_assert(std::is_nothrow_move_assignable<T>::value,
                  "BoundedQueue<T> requires nothrow move assignment");
    static_assert(std::is_nothrow_destructible<T>::value,
                  "BoundedQueue<T> requires nothrow destruction");

public:
    explicit BoundedQueue(std::size_t capacity) : capacity_(capacity) {
        if (capacity == 0) {
            throw std::invalid_argument(
                "Bounded queue capacity must be positive");
        }
    }

    ~BoundedQueue() noexcept {
        cancel();
    }

    BoundedQueue(const BoundedQueue&) = delete;
    BoundedQueue& operator=(const BoundedQueue&) = delete;
    BoundedQueue(BoundedQueue&&) = delete;
    BoundedQueue& operator=(BoundedQueue&&) = delete;

    void push(T item) {
        std::unique_lock<std::mutex> lock(mutex_);
        not_full_.wait(lock, [this] {
            return cancelled_ || closed_ || queue_.size() < capacity_;
        });

        if (cancelled_) {
            throw std::runtime_error("Bounded queue is cancelled");
        }
        if (closed_) {
            throw std::runtime_error("Bounded queue is closed");
        }

        try {
            queue_.push_back(std::move(item));
        } catch (...) {
            // This producer consumed a capacity wake but did not consume the
            // slot. Restore one wake after dropping the queue mutex.
            lock.unlock();
            not_full_.notify_one();
            throw;
        }

        lock.unlock();
        not_empty_.notify_one();
    }

    bool pop(T& out) {
        std::optional<T> item;
        {
            std::unique_lock<std::mutex> lock(mutex_);
            not_empty_.wait(lock, [this] {
                return cancelled_ || closed_ || !queue_.empty();
            });

            if (cancelled_) {
                throw std::runtime_error("Bounded queue is cancelled");
            }
            if (queue_.empty()) {
                return false;
            }

            item.emplace(std::move(queue_.front()));
            queue_.pop_front();
        }

        // Nothrow assignment stays outside the queue lock so replacing an
        // existing permit in out cannot nest a memory-budget release.
        not_full_.notify_one();
        out = std::move(*item);
        return true;
    }

    void close() {
        {
            std::lock_guard<std::mutex> lock(mutex_);
            if (closed_) {
                return;
            }
            closed_ = true;
        }
        not_empty_.notify_all();
        not_full_.notify_all();
    }

    void cancel() noexcept {
        {
            std::lock_guard<std::mutex> lock(mutex_);
            if (cancelled_) {
                return;
            }
            cancelled_ = true;
            queue_.swap(discarded_);
        }

        // Publish cancellation promptly. Permit destruction may contend on the
        // memory-budget mutex, so it happens only after queue waiters are awake.
        not_empty_.notify_all();
        not_full_.notify_all();
        discarded_.clear();
    }

private:
    const std::size_t capacity_;
    std::mutex mutex_;
    std::condition_variable not_empty_;
    std::condition_variable not_full_;
    std::deque<T> queue_;
    // Construct this spare buffer with the queue so cancellation can empty the
    // live FIFO by noexcept swap without allocating while it is unwinding.
    std::deque<T> discarded_;
    bool closed_ = false;
    bool cancelled_ = false;
};

}  // namespace ctb
```
