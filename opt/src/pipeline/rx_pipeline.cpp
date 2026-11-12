#include "pipeline/rx_pipeline.h"

#include "common/config.h"
#include "common/settings.h"
#include "mac/packet.h"
#include "mac/preamble.h"
#include "phy/filter.h"
#include "phy/modulator.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <complex>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <vector>

std::size_t count_bits(uint32_t value) {
    std::size_t count = 0;

    while (value != 0) {
        count += value & 1U;
        value >>= 1;
    }

    return count;
}

std::vector<uint8_t> RxPipeline::packet_to_msg(std::vector<PacketStruct> received_packets) {
    std::vector<uint8_t> received_message;

    for (const auto& packet : received_packets) {

        std::cout << "[RX] Appending sequence " << packet.header.sequence << ", payload size "
                  << packet.payload.size() << " bytes" << std::endl;

        received_message.insert(received_message.end(), packet.payload.begin(),
                                packet.payload.end());
    }

    return received_message;
}

std::vector<PacketStruct> RxPipeline::decode_packets(const Synchronizer::Channels& captured) {
    Synchronizer synchronizer;

    // Coarse CFO correction. Both RX channels share one LO, so the same value applies to both.
    // The per-packet estimate in the synchronizer only has to handle the residual.
    constexpr double CFO_HZ = 1780.0;
    const double w = -2.0 * M_PI * CFO_HZ / SAMPLE_FREQ;

    Synchronizer::Channels filtered;

    for (std::size_t ch = 0; ch < 2; ++ch) {
        const auto& in = captured[ch];

        std::vector<std::complex<float>> corrected(in.size());
        for (std::size_t n = 0; n < in.size(); ++n) {
            const double ph = w * static_cast<double>(n);
            corrected[n] = in[n] * std::complex<float>(std::cos(ph), std::sin(ph));
        }

        RRCFilter rx_filter;
        filtered[ch] = rx_filter.filter(
            corrected, rx_filter.root_raised_cosine(GAIN, SAMPLE_FREQ, SYMBOL_RATE, ROLL_OFF_FACTOR,
                                                    FILTER_SPAN * SAMPLES_PER_SYMBOL + 1));
    }

    const auto detections = synchronizer.process(filtered);
    if (detections.empty()) {
        std::cerr << "[RX] FAIL: No preamble detected\n";
    }
    m_stats.preambles_detected = detections.size();
    std::cout << "[RX] Preambles detected: " << detections.size() << '\n';

    std::vector<PacketStruct> received_packets = demod_parse(filtered, detections);

    std::sort(received_packets.begin(), received_packets.end(),
              [](const PacketStruct& a, const PacketStruct& b) {
                  return a.header.sequence < b.header.sequence;
              });
    m_stats.packets_decoded = received_packets.size();
    std::cout << "[RX] Successfully decoded packets: " << received_packets.size() << '\n';

    return received_packets;
}

std::vector<PacketStruct> RxPipeline::demod_parse(const Synchronizer::Channels& filtered,
                                                  const std::vector<Detection>& detections) {
    Packet builder;
    Demodulator demodulator;
    std::vector<PacketStruct> received_packets;

    const std::size_t n_samples = std::min(filtered[0].size(), filtered[1].size());
    const std::size_t max_samples = (MIN_PACKET_SIZE + MAX_PAYLOAD_SIZE * 8) * SAMPLES_PER_SYMBOL;

    for (const auto& detection : detections) {
        if (detection.offset >= n_samples) {
            ++m_stats.invalid_offsets;
            continue;
        }

        const std::size_t end = std::min(n_samples, detection.offset + max_samples);
        std::vector<std::complex<float>> combined(end - detection.offset);

        // Maximal-ratio combining: weight each channel by conj(g) (its gain and phase estimated
        // on the preamble), after removing the residual CFO rotation.
        const double gain_norm = std::norm(detection.g[0]) + std::norm(detection.g[1]) + 1e-30;

        for (std::size_t n = 0; n < combined.size(); ++n) {
            std::complex<double> sum{};
            for (std::size_t ch = 0; ch < 2; ++ch) {
                sum += std::conj(detection.g[ch]) *
                       std::complex<double>(filtered[ch][detection.offset + n]);
            }
            const double ph = 2.0 * M_PI * detection.cfo_hz * static_cast<double>(n) / SAMPLE_FREQ;
            combined[n] = std::complex<float>(sum * std::polar(1.0, -ph) / gain_norm);
        }

        auto bits = demodulator.bpsk_demodulate(combined, SAMPLES_PER_SYMBOL, 0);

        bool ok = true;
        const auto parsed_packet = builder.parse(bits, &ok);
        if (!parsed_packet.has_value()) {
            ++m_stats.parse_failures;
            continue;
        }
        if (!ok) {
            ++m_stats.checksum_failures;
            std::cerr << "[RX] Checksum failure, seq " << parsed_packet->header.sequence
                      << ", metric " << detection.metric << '\n';
            m_rejected.push_back(*parsed_packet);
            continue;
        }

        received_packets.push_back(*parsed_packet);
    }

    return received_packets;
}