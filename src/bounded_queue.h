#pragma once

#include <condition_variable>
#include <cstddef>
#include <deque>
#include <mutex>
#include <optional>
#include <utility>

namespace sp {

// Multi-producer, multi-consumer FIFO with a fixed capacity.
//
// push() blocks while the queue is full (back-pressure: a slow consumer slows
// the producer down instead of letting memory grow without bound). pop() blocks
// while it is empty. close() wakes everyone: pushes start failing and pops
// drain what is left, then return nullopt.
template <typename T>
class BoundedQueue {
public:
    explicit BoundedQueue(std::size_t capacity) : capacity_(capacity == 0 ? 1 : capacity) {}

    BoundedQueue(const BoundedQueue&) = delete;
    BoundedQueue& operator=(const BoundedQueue&) = delete;

    // Returns false if the queue was closed; the item is not enqueued.
    bool push(T item) {
        std::unique_lock lock(mutex_);
        not_full_.wait(lock, [&] { return closed_ || items_.size() < capacity_; });
        if (closed_) return false;
        items_.push_back(std::move(item));
        ++pushes_;
        // notifying under the lock costs a little (the woken thread may briefly block
        // on the mutex) but leaves no window where a waiter can miss the signal, and
        // keeps Helgrind and DRD reports clean
        not_empty_.notify_one();
        return true;
    }

    std::optional<T> pop() {
        std::unique_lock lock(mutex_);
        not_empty_.wait(lock, [&] { return closed_ || !items_.empty(); });
        if (items_.empty()) return std::nullopt;  // closed and drained
        T item = std::move(items_.front());
        items_.pop_front();
        not_full_.notify_one();
        return item;
    }

    void close() {
        std::lock_guard lock(mutex_);
        closed_ = true;
        not_full_.notify_all();
        not_empty_.notify_all();
    }

    std::size_t size() const {
        std::lock_guard lock(mutex_);
        return items_.size();
    }

    std::size_t capacity() const { return capacity_; }

    std::size_t total_pushes() const {
        std::lock_guard lock(mutex_);
        return pushes_;
    }

private:
    const std::size_t capacity_;
    mutable std::mutex mutex_;
    std::condition_variable not_full_;
    std::condition_variable not_empty_;
    std::deque<T> items_;
    std::size_t pushes_ = 0;
    bool closed_ = false;
};

}  // namespace sp
