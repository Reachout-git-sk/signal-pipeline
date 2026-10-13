#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

namespace sp {

struct FftBenchResult {
    std::size_t workers;
    std::uint64_t windows;
    double seconds;
    double windows_per_sec;
};

// Push `windows` copies of a synthetic 50 + 120 Hz window through the queue to
// a pool of `workers` FFT threads and time it end to end.
FftBenchResult bench_fft(std::size_t workers, std::uint64_t windows, std::size_t window_size, std::size_t queue_capacity);

struct BatchBenchResult {
    std::size_t batch;          // samples per queue item
    std::uint64_t samples;
    std::uint64_t queue_ops;    // pushes
    double seconds;
    double samples_per_sec;
};

// Move `samples` floats from one producer to `consumers` threads through the
// queue, `batch` samples per item. Consumers do almost no work, so the time
// is dominated by queue synchronisation: this isolates lock contention.
BatchBenchResult bench_batching(std::size_t batch, std::uint64_t samples, std::size_t consumers, std::size_t queue_capacity);

}  // namespace sp
