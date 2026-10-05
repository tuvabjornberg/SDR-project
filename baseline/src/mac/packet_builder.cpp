#include "mac/packet_builder.h"

#include "mac/preamble.h"
#include "mac/validation.h"

PacketBuilder::PacketBuilder(uint16_t starting_sequence) : m_sequence_(starting_sequence) {}

std::vector<Packet> PacketBuilder::build(const std::vector<uint8_t>& data, uint8_t flags) {
    std::vector<Packet> packets;

    const std::size_t fragment_count =
        std::max<std::size_t>(1, (data.size() + MAX_PAYLOAD_SIZE - 1) / MAX_PAYLOAD_SIZE);
    packets.reserve(fragment_count);

    for (std::size_t i = 0; i < fragment_count; ++i) {
        const std::size_t offset = i * MAX_PAYLOAD_SIZE;

        const std::size_t remaining_data = data.size() - offset;

        const std::size_t size = std::min(remaining_data, MAX_PAYLOAD_SIZE);

        Packet packet;

        packet.preamble = BARKER_PREAMBLE;

        packet.header.sequence = m_sequence_++;
        packet.header.length_flags = make_length_flags(size, flags);

        packet.payload.assign(data.begin() + offset, data.begin() + offset + size);

        packet.checksum = Validator::compute_checksum(packet.payload);

        packets.push_back(std::move(packet));
    }

    return packets;
}

std::vector<uint8_t> PacketBuilder::packet_to_bits(const Packet& packet) {
    std::vector<uint8_t> bits;

    for (int i = 31; i >= 0; --i) {
        bits.push_back((packet.preamble >> i) & 1);
    }

    const auto* header_bytes = reinterpret_cast<const uint8_t*>(&packet.header);

    for (std::size_t i = 0; i < sizeof(PacketHeader); ++i) {
        for (int bit = 7; bit >= 0; --bit) {
            bits.push_back((header_bytes[i] >> bit) & 1);
        }
    }

    for (uint8_t byte : packet.payload) {
        for (int bit = 7; bit >= 0; --bit) {
            bits.push_back((byte >> bit) & 1);
        }
    }

    for (int i = 31; i >= 0; --i) {
        bits.push_back((packet.checksum >> i) & 1);
    }

    return bits;
}
