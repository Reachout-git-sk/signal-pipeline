#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <vector>

namespace sp {

// Wire format, little endian:
//   uint32 magic 'SP01' | uint32 sequence | uint16 sample count | uint16 reserved | float32 samples[count]
inline constexpr std::uint32_t kPacketMagic = 0x31305053;  // "SP01"
inline constexpr std::size_t kHeaderBytes = 12;
inline constexpr std::size_t kMaxSamplesPerPacket = 360;   // keeps a packet under a 1500 byte MTU

struct Packet {
    std::uint32_t sequence = 0;
    std::vector<float> samples;
};

std::vector<std::uint8_t> encode_packet(std::uint32_t sequence, const float* samples, std::size_t count);

// nullopt for anything that is not a well-formed packet
std::optional<Packet> decode_packet(const std::uint8_t* data, std::size_t len);

}  // namespace sp
