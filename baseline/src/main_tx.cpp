#include "mac/packet_builder.h"
#include "phy/modulator.h"

#include <cstdint>
#include <iomanip>
#include <iostream>
#include <string>
#include <vector>

static constexpr uint32_t SAMPLE_RATE = 2000000; // samples/sec
static constexpr uint32_t SYMBOL_RATE = 250000;  // symbols/sec
static_assert(SAMPLE_RATE % SYMBOL_RATE == 0, "SAMPLE_RATE must be an integer multiple of SYMBOL_RATE");
static constexpr uint32_t SAMPLES_PER_SYMBOL = SAMPLE_RATE / SYMBOL_RATE;

static constexpr std::size_t MAX_PAYLOAD_SIZE = 512;

int main()
{
    PacketBuilder builder;
    const std::string message = "Hello B210!";

    const uint8_t flags = 0;

    std::vector<uint8_t> payload(message.begin(), message.end());

    auto packets = builder.build(payload, flags);

    for (const auto &packet : packets)
    {
        std::cout << "Packet built successfully\n";
        std::cout << "------------------------\n";

        std::cout << "Preamble: 0x"
                  << std::hex
                  << std::setw(8)
                  << std::setfill('0')
                  << packet.preamble
                  << std::dec
                  << std::endl;

        std::cout << "Sequence: "
                  << packet.header.sequence
                  << std::endl;

        uint16_t length = get_length(packet.header);
        std::cout << "Length:   "
                  << length
                  << std::endl;

        uint8_t flags_o = get_flags(packet.header);
        std::cout << "Flags:    0x"
                  << std::hex
                  << static_cast<unsigned>(flags_o)
                  << std::dec
                  << std::endl;

        std::cout << "Payload:  "
                  << std::string(
                         packet.payload.begin(),
                         packet.payload.end())
                  << std::endl;

        std::cout << "Checksum: 0x"
                  << std::hex
                  << std::setw(8)
                  << std::setfill('0')
                  << packet.checksum
                  << std::dec
                  << std::endl;

        auto bit_vector = builder.packet_to_bits(packet);

        Modulator modulator;
        auto samples = modulator.bpsk_modulate(bit_vector, SAMPLES_PER_SYMBOL);

        std::cout << "\nBPSK Modulation\n";
        std::cout << "------------------------\n";
        std::cout << "Bits:    "
                  << bit_vector.size()
                  << std::endl;

        std::cout << "Samples: "
                  << samples.size()
                  << std::endl;

        std::cout << "First 10 samples:\n";
        for (std::size_t i = 0; i < 10; ++i)
        {
            std::cout
                << i
                << ": "
                << samples[i].real()
                << " + j"
                << samples[i].imag()
                << std::endl;
        }
    }

    return 0;
}
