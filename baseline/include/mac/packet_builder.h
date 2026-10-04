#pragma once

#include "packet.h"

#include <cstdint>
#include <span>
#include <vector>


class PacketBuilder {
public:
    explicit PacketBuilder(uint16_t starting_sequence = 0);

    Packet build(std::vector<uint8_t> payload, uint8_t flags = 0);

private:
    uint16_t m_sequence_;
};


