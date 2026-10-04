#include "mac/packet_builder.h"

#include <cstdint>
#include <iomanip>
#include <iostream>
#include <string>
#include <vector>

int main()
{
    PacketBuilder builder;
    const std::string message = "Hello B210!";

    const uint8_t flags = 0; 

    std::vector<uint8_t> payload(
        message.begin(),
        message.end()
    );

    Packet packet = builder.build(payload, flags);

    std::cout << "Packet built successfully\n";
    std::cout << "------------------------\n";

    std::cout << "Preamble: 0x"
              << std::hex
              << std::setw(8)
              << std::setfill('0')
              << packet.preamble
              << std::dec
              << '\n';

    std::cout << "Sequence: "
              << packet.header.sequence
              << '\n';

    uint16_t length = get_length(packet.header);
    std::cout << "Length:   "
              << length
              << '\n';

    uint8_t flags_o = get_flags(packet.header);
    std::cout << "Flags:    0x"
              << std::hex
              << static_cast<unsigned>(flags_o)
              << std::dec
              << '\n';

    std::cout << "Payload:  "
              << std::string(
                     packet.payload.begin(),
                     packet.payload.end())
              << '\n';

    std::cout << "Checksum: 0x"
              << std::hex
              << std::setw(8)
              << std::setfill('0')
              << packet.checksum
              << std::dec
              << '\n';

    return 0;
}
