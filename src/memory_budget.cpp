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
