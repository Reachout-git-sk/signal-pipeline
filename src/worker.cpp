#include "worker.h"

namespace sp {

WorkerPool::WorkerPool(BoundedQueue<Window>& in, std::size_t workers, std::size_t window_size, double sample_rate,
                       std::size_t top_k, Callback on_result)
    : in_(in), window_size_(window_size), sample_rate_(sample_rate), top_k_(top_k), on_result_(std::move(on_result)) {
    threads_.reserve(workers);
    for (std::size_t i = 0; i < workers; ++i) threads_.emplace_back([this] { loop(); });
}

WorkerPool::~WorkerPool() { join(); }

void WorkerPool::join() {
    for (auto& t : threads_)
        if (t.joinable()) t.join();
}

void WorkerPool::loop() {
    SpectrumAnalyzer analyzer(window_size_, sample_rate_);
    while (auto w = in_.pop()) {
        WindowResult r{w->index, analyzer.top_peaks(w->samples, top_k_)};
        processed_.fetch_add(1, std::memory_order_relaxed);
        if (on_result_) on_result_(r);
    }
}

}  // namespace sp
