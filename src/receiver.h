#pragma once

#include <atomic>
#include <cstdint>

#include "bounded_queue.h"
#include "window.h"

namespace sp {

struct ReceiverStats {
    std::atomic<std::uint64_t> packets{0};
    std::atomic<std::uint64_t> samples{0};
    std::atomic<std::uint64_t> windows{0};
    std::atomic<std::uint64_t> malformed{0};
    std::atomic<std::uint64_t> lost_packets{0};   // gaps in the sequence numbers
};

// Owns a UDP socket (RAII: closed in the destructor). Collects samples from
// incoming packets into windows of `window_size` and pushes each full window
// into the queue. One queue operation per window instead of one per packet or
// sample keeps the time spent holding the queue lock small.
class UdpReceiver {
public:
    // port 0 picks a free port; see port()
    UdpReceiver(std::uint16_t port, std::size_t window_size, BoundedQueue<Window>& out);
    ~UdpReceiver();
    UdpReceiver(const UdpReceiver&) = delete;
    UdpReceiver& operator=(const UdpReceiver&) = delete;

    std::uint16_t port() const { return port_; }

    // Blocks until stop is set (checked every 100 ms) or `max_windows` have been
    // produced (0 = unlimited). Closes the queue on return so workers drain and exit.
    void run(const std::atomic<bool>& stop, std::uint64_t max_windows = 0);

    const ReceiverStats& stats() const { return stats_; }

private:
    int fd_ = -1;
    std::uint16_t port_ = 0;
    std::size_t window_size_;
    BoundedQueue<Window>& out_;
    ReceiverStats stats_;
};

}  // namespace sp
