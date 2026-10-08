#pragma once
#include <queue>
#include <mutex>
#include <condition_variable>
#include <optional>

// A bounded-lifetime work queue for the crawl frontier.
// pop() blocks until an item is available OR the queue is closed AND
// drained, at which point it returns std::nullopt so workers know to exit.
// active_count_ tracks in-flight items so close() can be triggered
// automatically once the frontier is empty and nobody is still fetching
// (otherwise the workers would shut down while a fetch that might enqueue
// new links is still in progress).
template <typename T>
class ThreadSafeQueue {
public:
    void push(T item) {
        std::lock_guard<std::mutex> lock(mutex_);
        if (closed_) return;
        queue_.push(std::move(item));
        cv_.notify_one();
    }

    // Call when a worker picks up an item that may itself produce more work.
    void markActive() {
        std::lock_guard<std::mutex> lock(mutex_);
        ++active_;
    }
    // Call when that item is fully processed (its own push()es, if any,
    // already happened before this call).
    void markDone() {
        std::lock_guard<std::mutex> lock(mutex_);
        --active_;
        if (queue_.empty() && active_ == 0) {
            closed_ = true;
            cv_.notify_all();
        }
    }

    std::optional<T> pop() {
        std::unique_lock<std::mutex> lock(mutex_);
        cv_.wait(lock, [&] { return !queue_.empty() || closed_; });
        if (queue_.empty()) return std::nullopt;
        T item = std::move(queue_.front());
        queue_.pop();
        return item;
    }

    void closeNow() {
        std::lock_guard<std::mutex> lock(mutex_);
        closed_ = true;
        cv_.notify_all();
    }

private:
    std::mutex mutex_;
    std::condition_variable cv_;
    std::queue<T> queue_;
    int active_ = 0;
    bool closed_ = false;
};
