#pragma once

#include <atomic>
#include <cstdint>
#include <functional>
#include <thread>
#include <vector>

#include "bounded_queue.h"
#include "fft.h"
#include "window.h"

namespace sp {

struct WindowResult {
    std::uint64_t index;
    std::vector<Peak> peaks;
};

// A pool of threads that pop windows, run the FFT and report the top peaks.
// Each thread owns its own SpectrumAnalyzer (FFT buffers), so the only shared
// state is the queue and the result callback.
class WorkerPool {
public:
    using Callback = std::function<void(const WindowResult&)>;

    WorkerPool(BoundedQueue<Window>& in, std::size_t workers, std::size_t window_size, double sample_rate,
               std::size_t top_k, Callback on_result);
    ~WorkerPool();  // joins
    WorkerPool(const WorkerPool&) = delete;
    WorkerPool& operator=(const WorkerPool&) = delete;

    void join();
    std::uint64_t processed() const { return processed_.load(); }

private:
    void loop();

    BoundedQueue<Window>& in_;
    std::size_t window_size_;
    double sample_rate_;
    std::size_t top_k_;
    Callback on_result_;
    std::atomic<std::uint64_t> processed_{0};
    std::vector<std::thread> threads_;
};

}  // namespace sp
