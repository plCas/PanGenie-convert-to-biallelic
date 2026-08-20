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
