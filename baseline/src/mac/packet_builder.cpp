#include "mac/packet_builder.h"
#include "mac/validation.h"

PacketBuilder::PacketBuilder(uint16_t starting_sequence): m_sequence_(starting_sequence)
{
}

Packet PacketBuilder::build(std::vector<uint8_t> payload, uint8_t flags)
{
    Packet packet;

    packet.preamble = 0;

    packet.header.sequence = m_sequence_++;
    packet.header.length_flags = make_length_flags(payload.size(), flags);

    //packet.header.length_flags = (static_cast<uint16_t>(payload.size()) << 4) && flags;

    packet.payload.assign(payload.begin(), payload.end());

    packet.checksum = 0;

    return packet;
}


