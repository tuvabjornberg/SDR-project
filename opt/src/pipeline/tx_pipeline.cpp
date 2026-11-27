#include "pipeline/tx_pipeline.h"

#include "common/config.h"
#include "common/settings.h"
#include "phy/filter.h"
#include "phy/modulator.h"
#include "common/jpeg.h"

#include <bitset>
#include <cstdint>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <string>

std::vector<std::vector<uint8_t>> TxPipeline::file_reader(const std::string& filename,
                                                          std::size_t payload_size) {
    std::ifstream file(filename, std::ios::binary);

    if (!file.is_open()) {
        throw std::runtime_error("Could not open file: " + filename);
    }

    std::vector<uint8_t> data((std::istreambuf_iterator<char>(file)),
                              std::istreambuf_iterator<char>());

    file.close();

    data = jpegres::add_restart_markers(data);

    std::vector<std::vector<uint8_t>> payloads;

    for (std::size_t i = 0; i < data.size(); i += payload_size) {
        std::size_t chunk_size = std::min(payload_size, data.size() - i);

        payloads.emplace_back(data.begin() + i, data.begin() + i + chunk_size);
    }

    return payloads;
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

        auto bits = m_builder.packet_to_bits(packet);

        auto samples = modulator.bpsk_modulate(bits, SAMPLES_PER_SYMBOL);

        samples = tx_filter.filter(
            samples, tx_filter.root_raised_cosine(GAIN, SAMPLE_FREQ, SYMBOL_RATE, ROLL_OFF_FACTOR,
                                                  FILTER_SPAN * SAMPLES_PER_SYMBOL + 1));

        normalize(samples);

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