#include "pipeline/tx_pipeline.h"

#include "common/config.h"
#include "common/settings.h"
#include "phy/filter.h"
#include "phy/modulator.h"

#include <bitset>
#include <cstdint>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <string>

std::vector<uint8_t> TxPipeline::file_reader(int argc, char* argv[]) {
    if (argc < 2) {
        std::cerr << "Usage: " << argv[0] << " <filename.txt>" << std::endl;
    }

    std::ifstream file(argv[1]);

    if (!file.is_open()) {
        std::cerr << "Error: Could not open file: " << argv[1] << std::endl;
    }

    std::stringstream buffer;
    buffer << file.rdbuf();

    std::string message = buffer.str();

    file.close();

    // Remove trailing newline characters
    while (!message.empty() && (message.back() == '\n' || message.back() == '\r')) {
        message.pop_back();
    }

    std::vector<uint8_t> payload(message.begin(), message.end());

#if VERBOSE
    std::cout << "[INPUT]" << std::endl;
    std::cout << "\tFile: " << argv[1] << std::endl;
#endif

    return payload;
}

std::vector<std::complex<float>> TxPipeline::generate_samples(const std::vector<uint8_t>& payload) {
    Modulator modulator;
    RRCFilter tx_filter;

    auto packets = m_builder.build(payload, 0);

#if VERBOSE
    std::cout << "\tOriginal message size (payload): " << payload.size() << " bytes\n";
    std::cout << "\tPackets generated: " << packets.size() << '\n';
#endif

    std::vector<std::complex<float>> tx_samples;
    int i = 0;
    for (const auto& packet : packets) {
#if VERBOSE
        std::cout << "\tPacket " << i << ": Sequence = " << packet.header.sequence
                  << ", Preamble: 0x" << std::hex << std::setw(8) << std::setfill('0')
                  << packet.preamble << std::dec << ", Length = " << get_length(packet.header)
                  << ", Flags: 0x" << std::hex << static_cast<unsigned>(get_flags(packet.header))
                  << std::dec << ", Checksum: 0x" << std::hex << std::setw(8) << std::setfill('0')
                  << packet.checksum << std::dec << std::endl;
        std::cout << "\tPayload:  " << std::string(packet.payload.begin(), packet.payload.end())
                  << std::endl;
#endif

        // if (packet.header.sequence > 195) {
        //     std::cout << "\tPacket " << i << ": Sequence = " << packet.header.sequence
        //               << ", Preamble: 0x" << std::hex << std::setw(8) << std::setfill('0')
        //               << packet.preamble << std::dec << ", Length = " << get_length(packet.header)
        //               << ", Flags: 0x" << std::hex
        //               << static_cast<unsigned>(get_flags(packet.header)) << std::dec
        //               << ", Checksum: 0x" << std::hex << std::setw(8) << std::setfill('0')
        //               << packet.checksum << std::dec << std::endl;

        //     std::bitset<32> payload_bits;
        //     for (std::size_t bit = 0; bit < std::min<std::size_t>(32, packet.payload.size() * 8);
        //          ++bit) {
        //         const std::size_t byte_index = bit / 8;
        //         const std::size_t bit_index = bit % 8;
        //         const bool value = (packet.payload[byte_index] >> (7 - bit_index)) & 0x1u;
        //         payload_bits.set(31 - bit, value);
        //     }
        //     std::cout << "\tPayload:  " << payload_bits << std::endl;
        // }

        auto bits = m_builder.packet_to_bits(packet);

#if VERBOSE
        // std::cout << "\n[Packet to bits]\n";
        // std::cout << "\tpre: ";
        // for (std::size_t i = 0; i < 32; ++i) {
        //     std::cout << static_cast<int>(bits[i]);
        // }
        // std::cout << "\n\tseq: ";
        // for (std::size_t i = 32; i < 48; ++i) {
        //     std::cout << static_cast<int>(bits[i]);
        // }
        // std::cout << "\n\tlengthflags: ";
        // for (std::size_t i = 48; i < 64; ++i) {
        //     std::cout << static_cast<int>(bits[i]);
        // }
        // std::cout << "\n\tchecksum: ";
        // for (std::size_t i = 152; i < 184; ++i) {
        //     std::cout << static_cast<int>(bits[i]);
        // }
        // std::cout << "\n";

        // // std::cout << '\n';
        // std::cout << "Complete pre+head:";
        // for (size_t i = 0; i < 64; i++) {
        //     std::cout << static_cast<int>(bits[i]);
        // }
        // std::cout << '\n';
        // std::cout << '\n';
#endif

        auto samples = modulator.bpsk_modulate(bits, SAMPLES_PER_SYMBOL);

#if VERBOSE
        // std::cout << "[After mod]\n";
        // std::cout << "\tpre: ";
        // for (size_t bit = 0; bit < 32; ++bit) {
        //     std::cout << (samples[bit * SAMPLES_PER_SYMBOL].real() > 0.0f);
        // }
        // std::cout << '\n';
        // std::cout << "\tseq: ";
        // for (size_t bit = 32; bit < 48; ++bit) {
        //     std::cout << (samples[bit * SAMPLES_PER_SYMBOL].real() > 0.0f);
        // }
        // std::cout << '\n';
        // std::cout << "\tlengthflags: ";
        // for (size_t bit = 48; bit < 64; ++bit) {
        //     std::cout << (samples[bit * SAMPLES_PER_SYMBOL].real() > 0.0f);
        // }
        // std::cout << '\n';
        // std::cout << "\tchecksum: ";
        // for (size_t bit = 152; bit < 184; ++bit) {
        //     std::cout << (samples[bit * SAMPLES_PER_SYMBOL].real() > 0.0f);
        // }
        // std::cout << '\n';

        // std::cout << "[MODULATION]" << std::endl;
        // std::cout << "\tBits:    " << bits.size() << std::endl;
        // std::cout << "\tSamples: " << samples.size() << std::endl;
#endif

        samples = tx_filter.filter(
            samples, tx_filter.root_raised_cosine(GAIN, SAMPLE_FREQ, SYMBOL_RATE, ROLL_OFF_FACTOR,
                                                  FILTER_SPAN * SAMPLES_PER_SYMBOL + 1));

#if VERBOSE
        // std::cout << "[After filter]\n";
        // const size_t FILTER_DELAY = 32;
        // std::cout << "\tpre: ";
        // for (size_t bit = 0; bit < 32; ++bit) {
        //     size_t idx = FILTER_DELAY + bit * SAMPLES_PER_SYMBOL;

        //     std::cout << (samples[idx].real() >= 0.0f);
        // }
        // std::cout << '\n';
        // std::cout << "\tseq: ";
        // for (size_t bit = 32; bit < 48; ++bit) {
        //     size_t idx = FILTER_DELAY + bit * SAMPLES_PER_SYMBOL;

        //     std::cout << (samples[idx].real() >= 0.0f);
        // }
        // std::cout << '\n';
        // std::cout << "\tlengthflags: ";
        // for (size_t bit = 48; bit < 64; ++bit) {
        //     size_t idx = FILTER_DELAY + bit * SAMPLES_PER_SYMBOL;

        //     std::cout << (samples[idx].real() >= 0.0f);
        // }
        // std::cout << '\n';
        // std::cout << "\tchecksum: ";
        // for (size_t bit = 152; bit < 184; ++bit) {
        //     size_t idx = FILTER_DELAY + bit * SAMPLES_PER_SYMBOL;

        //     std::cout << (samples[idx].real() >= 0.0f);
        // }
        // std::cout << '\n';

        std::cout << "[Filter]" << std::endl;
        std::cout << "\tBits:    " << bits.size() << std::endl;
        std::cout << "\tSamples: " << samples.size() << std::endl;
#endif

        normalize(samples);

#if VERBOSE
        // std::cout << "[After normalize]\n";
        // std::cout << "\tpre: ";
        // for (size_t bit = 0; bit < 32; ++bit) {
        //     size_t idx = FILTER_DELAY + bit * SAMPLES_PER_SYMBOL;

        //     std::cout << (samples[idx].real() >= 0.0f);
        // }
        // std::cout << '\n';
        // std::cout << "\tseq: ";
        // for (size_t bit = 32; bit < 48; ++bit) {
        //     size_t idx = FILTER_DELAY + bit * SAMPLES_PER_SYMBOL;

        //     std::cout << (samples[idx].real() >= 0.0f);
        // }
        // std::cout << '\n';
        // std::cout << "\tlengthflags: ";
        // for (size_t bit = 48; bit < 64; ++bit) {
        //     size_t idx = FILTER_DELAY + bit * SAMPLES_PER_SYMBOL;

        //     std::cout << (samples[idx].real() >= 0.0f);
        // }
        // std::cout << '\n';
#endif
        tx_samples.insert(tx_samples.end(), samples.begin(), samples.end());

        i++;
    }

    return tx_samples;
}

void TxPipeline::normalize(std::vector<std::complex<float>>& samples) {
    float max_magnitude = 0.0f;

    for (auto& sample : samples)
        max_magnitude = std::max(max_magnitude, std::abs(sample));

    if (max_magnitude > settings::TX_MAX_AMPLITUDE) {
        const float scale = settings::TX_MAX_AMPLITUDE / max_magnitude;

        for (auto& sample : samples) {
            sample *= scale;
        }
    }

#if VERBOSE
    double power = 0.0;
    max_magnitude = 0.0f;

    for (const auto& sample : samples) {
        max_magnitude = std::max(max_magnitude, std::abs(sample));
        power += std::norm(sample);
    }

    const double average_power = power / static_cast<double>(samples.size());

    const double rms = std::sqrt(average_power);
    std::cout << "\tMax magnitude:  " << max_magnitude << std::endl;
    std::cout << "\tAverage power:  " << average_power << std::endl;
    std::cout << "\tRMS magnitude:  " << rms << std::endl;
#endif
}