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
