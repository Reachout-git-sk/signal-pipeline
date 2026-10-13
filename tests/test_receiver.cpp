#include <gtest/gtest.h>

#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

#include <atomic>
#include <thread>

#include "packet.h"
#include "receiver.h"

namespace {

// RAII sender socket for the tests
struct UdpSender {
    int fd = ::socket(AF_INET, SOCK_DGRAM, 0);
    sockaddr_in to{};
    explicit UdpSender(std::uint16_t port) {
        to.sin_family = AF_INET;
        to.sin_port = htons(port);
        to.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    }
    ~UdpSender() { ::close(fd); }
    void send(const std::vector<std::uint8_t>& b) {
        ::sendto(fd, b.data(), b.size(), 0, reinterpret_cast<const sockaddr*>(&to), sizeof to);
    }
};

}  // namespace

TEST(Packet, RoundTrip) {
    std::vector<float> s = {0.0f, -1.5f, 3.25f, 1e-7f};
    auto bytes = sp::encode_packet(42, s.data(), s.size());
    EXPECT_EQ(bytes.size(), sp::kHeaderBytes + 16);
    auto p = sp::decode_packet(bytes.data(), bytes.size());
    ASSERT_TRUE(p);
    EXPECT_EQ(p->sequence, 42u);
    EXPECT_EQ(p->samples, s);
}

TEST(Packet, RejectsMalformed) {
    std::vector<float> s(10, 1.0f);
    auto good = sp::encode_packet(1, s.data(), s.size());
    EXPECT_FALSE(sp::decode_packet(good.data(), 5));                       // truncated header
    EXPECT_FALSE(sp::decode_packet(good.data(), good.size() - 1));         // truncated payload
    auto bad_magic = good;
    bad_magic[0] ^= 0xff;
    EXPECT_FALSE(sp::decode_packet(bad_magic.data(), bad_magic.size()));
    auto bad_count = good;
    bad_count[8] = 11;                                                      // claims more samples than sent
    EXPECT_FALSE(sp::decode_packet(bad_count.data(), bad_count.size()));
    EXPECT_THROW(sp::encode_packet(0, s.data(), sp::kMaxSamplesPerPacket + 1), std::invalid_argument);
}

TEST(Receiver, BatchesPacketsIntoWindowsInOrder) {
    sp::BoundedQueue<sp::Window> q(16);
    sp::UdpReceiver rx(0, 256, q);
    std::atomic<bool> stop{false};
    std::thread t([&] { rx.run(stop, 4); });

    UdpSender tx(rx.port());
    std::vector<float> s(100);
    float next = 0;
    for (std::uint32_t seq = 0; seq < 11; ++seq) {   // 1100 samples -> 4 full windows of 256
        for (auto& v : s) v = next++;
        tx.send(sp::encode_packet(seq, s.data(), s.size()));
    }
    tx.send({1, 2, 3});  // garbage is counted, not fatal

    std::vector<sp::Window> got;
    while (auto w = q.pop()) got.push_back(std::move(*w));
    stop = true;
    t.join();

    ASSERT_EQ(got.size(), 4u);
    for (std::size_t i = 0; i < got.size(); ++i) {
        EXPECT_EQ(got[i].index, i);
        ASSERT_EQ(got[i].samples.size(), 256u);
        EXPECT_EQ(got[i].samples.front(), static_cast<float>(i * 256));  // no samples lost or reordered
        EXPECT_EQ(got[i].samples.back(), static_cast<float>(i * 256 + 255));
    }
    EXPECT_EQ(rx.stats().lost_packets.load(), 0u);
}

TEST(Receiver, CountsSequenceGapsAsLostPackets) {
    sp::BoundedQueue<sp::Window> q(16);
    sp::UdpReceiver rx(0, 100, q);
    std::atomic<bool> stop{false};
    std::thread t([&] { rx.run(stop, 2); });

    UdpSender tx(rx.port());
    std::vector<float> s(100, 1.0f);
    tx.send(sp::encode_packet(0, s.data(), s.size()));
    tx.send(sp::encode_packet(3, s.data(), s.size()));  // 1 and 2 never arrive
    while (q.pop()) {}
    stop = true;
    t.join();
    EXPECT_EQ(rx.stats().lost_packets.load(), 2u);
}

TEST(Receiver, StopFlagEndsRunAndClosesQueue) {
    sp::BoundedQueue<sp::Window> q(4);
    sp::UdpReceiver rx(0, 1024, q);
    std::atomic<bool> stop{false};
    std::thread t([&] { rx.run(stop); });
    stop = true;
    t.join();
    EXPECT_FALSE(q.pop().has_value());
}
