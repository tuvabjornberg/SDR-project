#pragma once

#include "packet.h"

#include <cstdint>
#include <span>
#include <vector>

class PacketBuilder
{
public:
    explicit PacketBuilder(uint16_t starting_sequence = 0);

    std::vector<Packet> build(const std::vector<uint8_t>& data, uint8_t flags);

private:
    uint16_t m_sequence_;

    static constexpr std::size_t MAX_PAYLOAD_SIZE = 512;
};



