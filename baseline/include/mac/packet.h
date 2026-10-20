#pragma once

#include <cstdint>
#include <optional>
#include <vector>

struct PacketHeader {
    uint16_t sequence;
    uint16_t length_flags; // [15:4] = length, [3:0] = flags
};

struct PacketStruct {
    uint32_t preamble;
    PacketHeader header;
    std::vector<uint8_t> payload;
    uint32_t checksum;
};

constexpr uint16_t FLAG_MASK = 0x000F;

inline uint16_t make_length_flags(uint16_t length, uint8_t flags) {
    return static_cast<uint16_t>((length << 4) | (flags & 0x0F));
}

inline uint16_t get_length(const PacketHeader& header) {
    return header.length_flags >> 4;
}

inline uint8_t get_flags(const PacketHeader& header) {
    return static_cast<uint8_t>(header.length_flags & FLAG_MASK);
}

class Packet {
  public:
    explicit Packet(uint16_t starting_sequence = 0);

    std::vector<PacketStruct> build(const std::vector<uint8_t>& data, uint8_t flags);

    std::optional<PacketStruct> parse(const std::vector<uint8_t>& raw_bits,
                                      bool* checksum_ok = nullptr);
    std::vector<uint8_t> packet_to_bits(const PacketStruct& packet);

  private:
    uint16_t m_sequence;
};
