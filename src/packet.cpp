#include "packet.h"

#include <cstring>
#include <stdexcept>

namespace sp {

namespace {

void put_u32(std::uint8_t* p, std::uint32_t v) {
    for (int i = 0; i < 4; ++i) p[i] = static_cast<std::uint8_t>(v >> (8 * i));
}

std::uint32_t get_u32(const std::uint8_t* p) {
    std::uint32_t v = 0;
    for (int i = 0; i < 4; ++i) v |= static_cast<std::uint32_t>(p[i]) << (8 * i);
    return v;
}

}  // namespace

std::vector<std::uint8_t> encode_packet(std::uint32_t sequence, const float* samples, std::size_t count) {
    if (count > kMaxSamplesPerPacket) throw std::invalid_argument("too many samples for one packet");
    std::vector<std::uint8_t> out(kHeaderBytes + count * sizeof(float));
    put_u32(out.data(), kPacketMagic);
    put_u32(out.data() + 4, sequence);
    out[8] = static_cast<std::uint8_t>(count & 0xff);
    out[9] = static_cast<std::uint8_t>(count >> 8);
    out[10] = out[11] = 0;
    for (std::size_t i = 0; i < count; ++i) {
        std::uint32_t bits;
        std::memcpy(&bits, &samples[i], sizeof bits);
        put_u32(out.data() + kHeaderBytes + 4 * i, bits);
    }
    return out;
}

std::optional<Packet> decode_packet(const std::uint8_t* data, std::size_t len) {
    if (len < kHeaderBytes || get_u32(data) != kPacketMagic) return std::nullopt;
    const std::size_t count = static_cast<std::size_t>(data[8]) | (static_cast<std::size_t>(data[9]) << 8);
    if (count > kMaxSamplesPerPacket || len != kHeaderBytes + count * sizeof(float)) return std::nullopt;

    Packet p;
    p.sequence = get_u32(data + 4);
    p.samples.resize(count);
    for (std::size_t i = 0; i < count; ++i) {
        const std::uint32_t bits = get_u32(data + kHeaderBytes + 4 * i);
        std::memcpy(&p.samples[i], &bits, sizeof bits);
    }
    return p;
}

}  // namespace sp
