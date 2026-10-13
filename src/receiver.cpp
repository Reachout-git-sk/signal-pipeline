#include "receiver.h"

#include <arpa/inet.h>
#include <netinet/in.h>
#include <poll.h>
#include <sys/socket.h>
#include <unistd.h>

#include <cerrno>
#include <cstring>
#include <stdexcept>
#include <string>

#include "packet.h"

namespace sp {

UdpReceiver::UdpReceiver(std::uint16_t port, std::size_t window_size, BoundedQueue<Window>& out)
    : window_size_(window_size), out_(out) {
    fd_ = ::socket(AF_INET, SOCK_DGRAM, 0);
    if (fd_ < 0) throw std::runtime_error(std::string("socket: ") + std::strerror(errno));

    int rcvbuf = 4 << 20;  // a big kernel buffer absorbs bursts while the queue is full
    ::setsockopt(fd_, SOL_SOCKET, SO_RCVBUF, &rcvbuf, sizeof rcvbuf);

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_ANY);
    addr.sin_port = htons(port);
    if (::bind(fd_, reinterpret_cast<sockaddr*>(&addr), sizeof addr) < 0) {
        const std::string err = std::strerror(errno);
        ::close(fd_);
        throw std::runtime_error("bind: " + err);
    }
    socklen_t len = sizeof addr;
    ::getsockname(fd_, reinterpret_cast<sockaddr*>(&addr), &len);
    port_ = ntohs(addr.sin_port);
}

UdpReceiver::~UdpReceiver() {
    if (fd_ >= 0) ::close(fd_);
}

void UdpReceiver::run(const std::atomic<bool>& stop, std::uint64_t max_windows) {
    std::vector<std::uint8_t> buf(65536);
    Window current;
    current.samples.reserve(window_size_);
    std::uint64_t next_index = 0;
    bool have_seq = false;
    std::uint32_t expected_seq = 0;

    pollfd pfd{fd_, POLLIN, 0};
    while (!stop.load(std::memory_order_relaxed)) {
        const int ready = ::poll(&pfd, 1, 100);
        if (ready < 0 && errno != EINTR) break;
        if (ready <= 0) continue;

        const ssize_t n = ::recv(fd_, buf.data(), buf.size(), 0);
        if (n <= 0) continue;
        auto pkt = decode_packet(buf.data(), static_cast<std::size_t>(n));
        if (!pkt) {
            stats_.malformed.fetch_add(1, std::memory_order_relaxed);
            continue;
        }
        stats_.packets.fetch_add(1, std::memory_order_relaxed);
        // a jump forward in sequence numbers means packets were dropped on the way
        if (have_seq && pkt->sequence > expected_seq)
            stats_.lost_packets.fetch_add(pkt->sequence - expected_seq, std::memory_order_relaxed);
        have_seq = true;
        expected_seq = pkt->sequence + 1;

        for (float s : pkt->samples) {
            current.samples.push_back(s);
            if (current.samples.size() == window_size_) {
                current.index = next_index++;
                if (!out_.push(std::move(current))) goto done;  // queue closed
                stats_.windows.fetch_add(1, std::memory_order_relaxed);
                if (max_windows && next_index >= max_windows) goto done;
                current = Window{};
                current.samples.reserve(window_size_);
            }
        }
        stats_.samples.fetch_add(pkt->samples.size(), std::memory_order_relaxed);
    }
done:
    out_.close();
}

}  // namespace sp
