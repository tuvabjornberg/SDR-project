#pragma once

#include "packet.h"

#include <cstdint>
#include <vector>

class PacketBuilder {
  public:
    explicit PacketBuilder(uint16_t starting_sequence = 0);

    std::vector<Packet> build(const std::vector<uint8_t>& data, uint8_t flags);

    std::vector<uint8_t> packet_to_bits(const Packet& packet);

  private:
    uint16_t m_sequence_;
};
