// End to end: UDP packets -> receiver -> queue -> worker pool -> peaks.
#include <gtest/gtest.h>

#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

#include <atomic>
#include <cmath>
#include <map>
#include <mutex>
#include <random>
#include <thread>

#include "bench.h"
#include "fft.h"
#include "packet.h"
#include "receiver.h"
#include "worker.h"

TEST(Pipeline, FindsBothTonesInEveryWindow) {
    constexpr double fs = 1000;
    constexpr std::size_t window = 1024, windows = 12;
    sp::BoundedQueue<sp::Window> q(8);
    sp::UdpReceiver rx(0, window, q);

    std::mutex mu;
    std::map<std::uint64_t, std::vector<sp::Peak>> results;
    sp::WorkerPool pool(q, 4, window, fs, 2, [&](const sp::WindowResult& r) {
        std::lock_guard lock(mu);
        results[r.index] = r.peaks;
    });
    std::atomic<bool> stop{false};
    std::thread rx_thread([&] { rx.run(stop, windows); });

    int fd = ::socket(AF_INET, SOCK_DGRAM, 0);
    sockaddr_in to{};
    to.sin_family = AF_INET;
    to.sin_port = htons(rx.port());
    to.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    std::mt19937 rng(3);
    std::normal_distribution<double> noise(0, 0.2);
    std::vector<float> s(128);
    std::size_t n = 0;
    for (std::uint32_t seq = 0; n < window * windows; ++seq) {
        for (auto& v : s) {
            const double t = static_cast<double>(n++) / fs;
            v = static_cast<float>(std::sin(2 * sp::kPi * 50 * t) + 0.5 * std::sin(2 * sp::kPi * 120 * t) + noise(rng));
        }
        auto pkt = sp::encode_packet(seq, s.data(), s.size());
        ::sendto(fd, pkt.data(), pkt.size(), 0, reinterpret_cast<sockaddr*>(&to), sizeof to);
        if (seq % 16 == 0) std::this_thread::sleep_for(std::chrono::microseconds(200));  // stay under the socket buffer
    }
    ::close(fd);

    rx_thread.join();
    pool.join();

    ASSERT_EQ(results.size(), windows);
    const double tol = fs / window;
    for (const auto& [idx, peaks] : results) {
        ASSERT_EQ(peaks.size(), 2u) << "window " << idx;
        EXPECT_NEAR(peaks[0].frequency_hz, 50.0, tol) << "window " << idx;
        EXPECT_NEAR(peaks[1].frequency_hz, 120.0, tol) << "window " << idx;
    }
    EXPECT_EQ(pool.processed(), windows);
}

TEST(Bench, FftBenchProcessesEveryWindow) {
    auto r = sp::bench_fft(2, 200, 256, 16);
    EXPECT_EQ(r.windows, 200u);
    EXPECT_GT(r.windows_per_sec, 0);
}

TEST(Bench, BatchingMovesEverySample) {
    auto r = sp::bench_batching(64, 100000, 3, 16);
    EXPECT_EQ(r.samples, 100000u);
    EXPECT_EQ(r.queue_ops, (100000u + 63) / 64);
}
