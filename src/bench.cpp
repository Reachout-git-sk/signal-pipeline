#include "bench.h"

#include <atomic>
#include <chrono>
#include <cmath>
#include <thread>

#include "bounded_queue.h"
#include "fft.h"
#include "window.h"
#include "worker.h"

namespace sp {

namespace {

double seconds_since(std::chrono::steady_clock::time_point t0) {
    return std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
}

}  // namespace

FftBenchResult bench_fft(std::size_t workers, std::uint64_t windows, std::size_t window_size, std::size_t queue_capacity) {
    const double fs = 1000.0;
    std::vector<float> tmpl(window_size);
    for (std::size_t i = 0; i < window_size; ++i) {
        const double t = static_cast<double>(i) / fs;
        tmpl[i] = static_cast<float>(std::sin(2 * kPi * 50 * t) + 0.5 * std::sin(2 * kPi * 120 * t));
    }

    BoundedQueue<Window> q(queue_capacity);
    std::atomic<std::uint64_t> checksum{0};
    const auto t0 = std::chrono::steady_clock::now();
    {
        WorkerPool pool(q, workers, window_size, fs, 2, [&](const WindowResult& r) {
            checksum.fetch_add(static_cast<std::uint64_t>(r.peaks.empty() ? 0 : r.peaks[0].frequency_hz),
                               std::memory_order_relaxed);
        });
        for (std::uint64_t i = 0; i < windows; ++i) q.push(Window{i, tmpl});
        q.close();
        pool.join();
    }
    const double s = seconds_since(t0);
    return {workers, windows, s, static_cast<double>(windows) / s};
}

BatchBenchResult bench_batching(std::size_t batch, std::uint64_t samples, std::size_t consumers, std::size_t queue_capacity) {
    BoundedQueue<std::vector<float>> q(queue_capacity);
    std::atomic<std::uint64_t> received{0};
    std::vector<std::thread> threads;

    const auto t0 = std::chrono::steady_clock::now();
    for (std::size_t c = 0; c < consumers; ++c)
        threads.emplace_back([&] {
            std::uint64_t local = 0;
            double sink = 0;
            while (auto item = q.pop()) {
                for (float v : *item) sink += v;
                local += item->size();
            }
            received.fetch_add(local);
            if (sink == 12345.678) std::abort();  // keep the loop from being optimised away
        });

    std::uint64_t sent = 0;
    while (sent < samples) {
        const std::size_t n = static_cast<std::size_t>(std::min<std::uint64_t>(batch, samples - sent));
        q.push(std::vector<float>(n, 1.0f));
        sent += n;
    }
    q.close();
    for (auto& t : threads) t.join();
    const double s = seconds_since(t0);
    return {batch, received.load(), q.total_pushes(), s, static_cast<double>(received.load()) / s};
}

}  // namespace sp
