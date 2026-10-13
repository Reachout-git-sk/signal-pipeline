// signal_pipeline run   [--port 9000] [--workers 4] [--window 1024] [--rate 1000] [--windows N] [--queue 64]
// signal_pipeline bench fft      [--workers 1,2,4,8] [--windows 20000] [--window 1024]
// signal_pipeline bench batching [--samples 8388608] [--consumers 4] [--batches 1,16,64,256,1024]
#include <atomic>
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <mutex>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

#include "bench.h"
#include "receiver.h"
#include "worker.h"

namespace {

std::atomic<bool> g_stop{false};

void on_signal(int) { g_stop = true; }

std::map<std::string, std::string> parse_flags(int argc, char** argv, int first) {
    std::map<std::string, std::string> f;
    for (int i = first; i < argc; ++i) {
        std::string k = argv[i];
        if (k.rfind("--", 0) != 0 || i + 1 >= argc) {
            std::fprintf(stderr, "bad argument: %s\n", argv[i]);
            std::exit(2);
        }
        f[k.substr(2)] = argv[++i];
    }
    return f;
}

std::string get(const std::map<std::string, std::string>& f, const std::string& k, const std::string& def) {
    auto it = f.find(k);
    return it == f.end() ? def : it->second;
}

std::vector<std::size_t> list(const std::string& s) {
    std::vector<std::size_t> out;
    std::stringstream ss(s);
    for (std::string part; std::getline(ss, part, ',');) out.push_back(std::stoul(part));
    return out;
}

int cmd_run(const std::map<std::string, std::string>& f) {
    const auto port = static_cast<std::uint16_t>(std::stoul(get(f, "port", "9000")));
    const auto workers = std::stoul(get(f, "workers", "4"));
    const auto window = std::stoul(get(f, "window", "1024"));
    const double rate = std::stod(get(f, "rate", "1000"));
    const auto max_windows = std::stoull(get(f, "windows", "0"));

    sp::BoundedQueue<sp::Window> queue(std::stoul(get(f, "queue", "64")));
    sp::UdpReceiver rx(port, window, queue);
    std::printf("listening on udp/%u, window %zu samples at %.0f Hz, %zu workers\n", rx.port(), window, rate, workers);

    std::mutex print_mu;
    sp::WorkerPool pool(queue, workers, window, rate, 3, [&](const sp::WindowResult& r) {
        std::lock_guard lock(print_mu);
        std::printf("window %6llu:", static_cast<unsigned long long>(r.index));
        for (const auto& p : r.peaks) std::printf("  %7.2f Hz (%.3f)", p.frequency_hz, p.magnitude);
        std::printf("\n");
        std::fflush(stdout);
    });
    rx.run(g_stop, max_windows);
    pool.join();

    const auto& s = rx.stats();
    std::printf("packets %llu, samples %llu, windows %llu, processed %llu, lost packets %llu, malformed %llu\n",
                static_cast<unsigned long long>(s.packets.load()), static_cast<unsigned long long>(s.samples.load()),
                static_cast<unsigned long long>(s.windows.load()), static_cast<unsigned long long>(pool.processed()),
                static_cast<unsigned long long>(s.lost_packets.load()), static_cast<unsigned long long>(s.malformed.load()));
    return 0;
}

int cmd_bench(const std::string& what, const std::map<std::string, std::string>& f) {
    std::printf("hardware threads: %u\n", std::thread::hardware_concurrency());
    if (what == "fft") {
        const auto windows = std::stoull(get(f, "windows", "20000"));
        const auto window = std::stoul(get(f, "window", "1024"));
        double base = 0;
        std::printf("%8s %10s %10s %14s %8s\n", "workers", "windows", "seconds", "windows/sec", "speedup");
        for (auto w : list(get(f, "workers", "1,2,4,8"))) {
            auto r = sp::bench_fft(w, windows, window, 64);
            if (base == 0) base = r.windows_per_sec;
            std::printf("%8zu %10llu %10.3f %14.0f %7.2fx\n", r.workers, static_cast<unsigned long long>(r.windows),
                        r.seconds, r.windows_per_sec, r.windows_per_sec / base);
        }
        return 0;
    }
    if (what == "batching") {
        const auto samples = std::stoull(get(f, "samples", "8388608"));
        const auto consumers = std::stoul(get(f, "consumers", "4"));
        std::printf("%10s %12s %12s %10s %16s\n", "batch", "samples", "queue ops", "seconds", "samples/sec");
        for (auto b : list(get(f, "batches", "1,16,64,256,1024"))) {
            auto r = sp::bench_batching(b, samples, consumers, 64);
            std::printf("%10zu %12llu %12llu %10.3f %16.0f\n", r.batch, static_cast<unsigned long long>(r.samples),
                        static_cast<unsigned long long>(r.queue_ops), r.seconds, r.samples_per_sec);
        }
        return 0;
    }
    std::fprintf(stderr, "unknown benchmark %s (fft | batching)\n", what.c_str());
    return 2;
}

}  // namespace

int main(int argc, char** argv) {
    std::signal(SIGINT, on_signal);
    std::signal(SIGTERM, on_signal);
    if (argc >= 2 && std::strcmp(argv[1], "run") == 0) return cmd_run(parse_flags(argc, argv, 2));
    if (argc >= 3 && std::strcmp(argv[1], "bench") == 0) return cmd_bench(argv[2], parse_flags(argc, argv, 3));
    std::fprintf(stderr, "usage: %s run [flags] | bench fft|batching [flags]\n", argv[0]);
    return 2;
}
