#pragma once

#include <cstdint>
#include <vector>

struct PacketHeader
{
    uint16_t sequence;
    uint16_t length_flags; // [15:4] = length, [3:0] = flags
};

struct Packet
{
    uint32_t preamble;
    PacketHeader header;
    std::vector<uint8_t> payload;
    uint32_t checksum;
};

constexpr uint16_t FLAG_MASK = 0x000F;

inline uint16_t make_length_flags(uint16_t length, uint8_t flags)
{
    return static_cast<uint16_t>((length << 4) | (flags & 0x0F));
}

inline uint16_t get_length(const PacketHeader &header)
{
    return header.length_flags >> 4;
}

inline uint8_t get_flags(const PacketHeader &header)
{
    return static_cast<uint8_t>(header.length_flags & FLAG_MASK);
}
