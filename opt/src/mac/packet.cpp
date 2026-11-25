#include "mac/packet.h"

#include "common/config.h"
#include "mac/preamble.h"
#include "mac/validation.h"

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <vector>

Packet::Packet(uint16_t starting_sequence) : m_sequence(starting_sequence) {}

std::vector<PacketStruct> Packet::build(const std::vector<uint8_t>& data, uint8_t flags) {
    std::vector<PacketStruct> packets;

    const std::size_t fragment_count =
        std::max<std::size_t>(1, (data.size() + MAX_PAYLOAD_SIZE - 1) / MAX_PAYLOAD_SIZE);

    packets.reserve(fragment_count);

    for (std::size_t i = 0; i < fragment_count; ++i) {
        const std::size_t offset = i * MAX_PAYLOAD_SIZE;

        const std::size_t remaining_data = data.size() - offset;

        const std::size_t size = std::min(remaining_data, MAX_PAYLOAD_SIZE);

        PacketStruct packet;

        packet.preamble = BARKER_PREAMBLE;

        packet.header.sequence = m_sequence++;
        packet.header.length_flags = make_length_flags(size, flags);

        packet.payload.assign(data.begin() + offset, data.begin() + offset + size);

        packet.checksum = Validator::compute_checksum(packet.payload);

        packets.push_back(std::move(packet));
    }

    return packets;
}

PacketStruct Packet::build(const std::vector<uint8_t>& data, uint8_t flags, uint16_t sequence) {

    const std::size_t size = std::min(data.size(), MAX_PAYLOAD_SIZE);

    PacketStruct packet;

    packet.preamble = BARKER_PREAMBLE;

    packet.header.sequence = sequence;
    packet.header.length_flags = make_length_flags(size, flags);

    packet.payload.assign(data.begin(), data.begin() + size);

    packet.checksum = Validator::compute_checksum(packet.payload);

    return packet;
}

std::vector<uint8_t> Packet::packet_to_bits(const PacketStruct& packet) {
    std::vector<uint8_t> bits;

    for (int i = 31; i >= 0; --i) {
        bits.push_back((packet.preamble >> i) & 1);
    }

    for (int i = 15; i >= 0; --i) {
        bits.push_back((packet.header.sequence >> i) & 1);
    }

    for (int i = 15; i >= 0; --i) {
        bits.push_back((packet.header.length_flags >> i) & 1);
    }

    for (uint8_t byte : packet.payload) {
        for (int bit = 7; bit >= 0; --bit) {
            bits.push_back((byte >> bit) & 1);
        }
    }

    for (int i = 31; i >= 0; --i) {
        bits.push_back((packet.checksum >> i) & 1);
    }

    // whiten(bits, PREAMBLE_SIZE);

    return bits;
}

std::optional<PacketStruct> Packet::parse(const std::vector<uint8_t>& bits) {
    PacketStruct packet{};
    packet.valid = true;

    if (bits.size() < MIN_PACKET_SIZE) {
        std::cerr << "[Parser]: insufficient bits for packet\n";
        packet.valid = false;
        // return std::nullopt;
    }

    std::size_t offset = 0;

    auto read_bits = [&](std::size_t count, uint32_t& value) -> bool {
        if (count > 32 || offset > bits.size() || count > bits.size() - offset) {
            return false;
        }

        value = 0;

        for (std::size_t i = 0; i < count; ++i) {
            if (bits[offset] > 1) {
                return false;
            }

            value = (value << 1) | bits[offset++];
        }

        return true;
    };

    uint32_t preamble = 0;
    uint32_t sequence = 0;
    uint32_t length_flags = 0;

    if (!read_bits(PREAMBLE_SIZE, preamble)) {
        std::cerr << "[Parser]: failed to read preamble\n";
        packet.valid = false;
        // return std::nullopt;
    }

    if (!read_bits(16, sequence) || !read_bits(16, length_flags)) {
        std::cerr << "[Parser]: incomplete header\n";
        packet.valid = false;
        // return std::nullopt;
    }

    PacketHeader header{};
    header.sequence = static_cast<uint16_t>(sequence);
    header.length_flags = static_cast<uint16_t>(length_flags);

    if (preamble != BARKER_PREAMBLE) {
        std::cerr << "[Parser]: invalid preamble"
                  << ", Sequence: " << header.sequence << ", Expected: 0x" << std::hex
                  << BARKER_PREAMBLE << ", Got: 0x" << preamble << std::dec << '\n';
        packet.valid = false;
        // return std::nullopt;
    }

    const uint16_t payload_length = get_length(header);

    if (payload_length > MAX_PAYLOAD_SIZE) {
        std::cerr << "[Parser]: payload length exceeds maximum"
                  << ", Sequence: " << header.sequence << ", Expected <= " << MAX_PAYLOAD_SIZE
                  << ", Got: " << payload_length << '\n';
        packet.valid = false;
        // return std::nullopt;
    }

    const std::size_t payload_bits = static_cast<std::size_t>(payload_length) * 8;

    if (offset > bits.size() || payload_bits > bits.size() - offset ||
        CHECKSUM_SIZE > bits.size() - offset - payload_bits) {
        std::cerr << "[Parser]: incomplete payload or checksum\n";
        packet.valid = false;
        // return std::nullopt;
    }

    packet.preamble = preamble;
    packet.header = header;

    packet.payload.reserve(payload_length);

    for (std::size_t i = 0; i < payload_length; ++i) {
        uint32_t byte = 0;

        if (!read_bits(8, byte)) {
            std::cerr << "[Parser]: failed to read payload\n";
            packet.valid = false;
            // return std::nullopt;
        }

        packet.payload.push_back(static_cast<uint8_t>(byte));
    }

    uint32_t received_checksum = 0;

    if (!read_bits(CHECKSUM_SIZE, received_checksum)) {
        std::cerr << "[Parser]: incomplete checksum\n";
        packet.valid = false;
        // return std::nullopt;
    }

    packet.checksum = received_checksum;

    const uint32_t calculated_checksum = Validator::compute_checksum(packet.payload);

    if (received_checksum != calculated_checksum) {
        std::cerr << "[Parser]: checksum mismatch, Expected: 0x" << std::hex << calculated_checksum
                  << ", Got: 0x" << received_checksum << std::dec << std::endl;
        packet.valid = false;
    }

    return packet;
}