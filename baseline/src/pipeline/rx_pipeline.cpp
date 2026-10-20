#include "pipeline/rx_pipeline.h"

#include "common/config.h"
#include "common/settings.h"
#include "mac/packet.h"
#include "mac/preamble.h"
#include "phy/filter.h"
#include "phy/modulator.h"

#include <algorithm>
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

// static void remove_dc(std::vector<std::complex<float>>& x, std::size_t half) {
//     const std::size_t n = x.size();
//     std::vector<std::complex<double>> cum(n + 1);
//     for (std::size_t i = 0; i < n; ++i)
//         cum[i + 1] = cum[i] + std::complex<double>(x[i]);
//     std::vector<std::complex<float>> out(n);
//     for (std::size_t i = 0; i < n; ++i) {
//         const std::size_t lo = i > half ? i - half : 0;
//         const std::size_t hi = std::min(n, i + half + 1);
//         out[i] = x[i] - std::complex<float>((cum[hi] - cum[lo]) / double(hi - lo));
//     }
//     x = std::move(out);
// }

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

std::vector<PacketStruct>
RxPipeline::decode_packets(const std::vector<std::complex<float>>& captured_samples) {
    RRCFilter rx_filter;
    Synchronizer synchronizer;

    constexpr double CFO_HZ = 1780.0; // measured: RX sees TX this far high

    std::vector<std::complex<float>> corrected(captured_samples.size());
    const double w = -2.0 * M_PI * CFO_HZ / SAMPLE_FREQ; // SAMPLE_FREQ = 2e6
    for (std::size_t n = 0; n < captured_samples.size(); ++n) {
        const double ph = w * static_cast<double>(n);
        corrected[n] = captured_samples[n] * std::complex<float>(std::cos(ph), std::sin(ph));
    }

#if VERBOSE
    double raw_power = 0.0;
    float raw_max = 0.0f;

    for (const auto& sample : captured_samples) {
        raw_power += std::norm(sample);
        raw_max = std::max(raw_max, std::abs(sample));
    }

    raw_power /= static_cast<double>(captured_samples.size());

    std::cout << "[Filter]" << std::endl;
    std::cout << "\tAverage power: " << raw_power << std::endl;
    std::cout << "\tRMS magnitude: " << std::sqrt(raw_power) << std::endl;
    std::cout << "\tMaximum magnitude: " << raw_max << std::endl;

    // std::cout << "\n[Captured samples]\n";
    // std::cout << "\tpre: ";
    // for (size_t bit = 0; bit < 32; ++bit) {
    //     size_t idx = bit * SAMPLES_PER_SYMBOL;
    //     std::cout << (captured_samples[idx].real() >= 0.0f);
    // }
    // std::cout << '\n';
    // std::cout << "\tseq: ";
    // for (size_t bit = 32; bit < 48; ++bit) {
    //     size_t idx = bit * SAMPLES_PER_SYMBOL;
    //     std::cout << (captured_samples[idx].real() >= 0.0f);
    // }
    // std::cout << '\n';
    // std::cout << "\tlengthflags: ";
    // for (size_t bit = 48; bit < 64; ++bit) {
    //     size_t idx = bit * SAMPLES_PER_SYMBOL;
    //     std::cout << (captured_samples[idx].real() >= 0.0f);
    // }
    // std::cout << '\n';
    // std::cout << "\tchecksum: ";
    // for (size_t bit = 152; bit < 184; ++bit) {
    //     size_t idx = bit * SAMPLES_PER_SYMBOL;
    //     std::cout << (captured_samples[idx].real() >= 0.0f);
    // }
    // std::cout << '\n';
#endif

    auto filtered_samples = rx_filter.filter(
        corrected,
        rx_filter.root_raised_cosine(GAIN, SAMPLE_FREQ, SYMBOL_RATE, ROLL_OFF_FACTOR,
                                     FILTER_SPAN * SAMPLES_PER_SYMBOL + 1));

#if VERBOSE
    std::cout << "\tFiltered samples: " << filtered_samples.size() << std::endl;
#endif
    // remove_dc(filtered_samples, 256); // ~64-symbol window

    const auto detections = synchronizer.process(filtered_samples);
    if (detections.empty()) {
        std::cerr << "[RX] FAIL: No preamble detected\n";
    }
    std::cout << "[RX] Preambles detected: " << detections.size() << '\n';

    std::vector<PacketStruct> received_packets = demod_parse(filtered_samples, detections);

    std::sort(received_packets.begin(), received_packets.end(),
              [](const PacketStruct& a, const PacketStruct& b) {
                  return a.header.sequence < b.header.sequence;
              });

    std::cout << "[RX] Successfully decoded packets: " << received_packets.size() << '\n';

    return received_packets;
}

std::vector<PacketStruct>
RxPipeline::demod_parse(const std::vector<std::complex<float>>& filtered_samples,
                        const std::vector<Detection>& detections) {
    Packet builder;
    Demodulator demodulator;
    std::vector<PacketStruct> received_packets;

    const std::size_t max_samples = (MIN_PACKET_SIZE + MAX_PAYLOAD_SIZE * 8) * SAMPLES_PER_SYMBOL;

    for (const auto& detection : detections) {
        if (detection.offset >= filtered_samples.size()) {
            continue;
        }

        const std::size_t end = std::min(filtered_samples.size(), detection.offset + max_samples);
        const std::vector<std::complex<float>> aligned_samples(
            filtered_samples.begin() + detection.offset, filtered_samples.begin() + end);

#if VERBOSE
        std::cout << "\tDecoding packet " << packet_index << " at sample offset " << sync_offset
                  << std::endl;
#endif

        if (detection.offset >= filtered_samples.size()) {
            std::cerr << "[RX] Skipping candidate: invalid sample offset\n";
            continue;
        }

        auto bits = demodulator.bpsk_demodulate(aligned_samples, SAMPLES_PER_SYMBOL, 0);

#if VERBOSE
        // std::cout << "\n[Aligned samples]\n";
        // for (size_t i = 0; i < 32; ++i) {
        //     size_t idx = i * SAMPLES_PER_SYMBOL + detection.phase;

        //     std::cout << aligned_samples[idx].real() << ' ';
        // }
        // std::cout << '\n';
        // for (int p = 0; p < SAMPLES_PER_SYMBOL; ++p) {
        //     std::cout << "phase " << p << ": ";
        //     for (int sym = 0; sym < 8; ++sym) {
        //         size_t idx = sym * SAMPLES_PER_SYMBOL + p;
        //         std::cout << aligned_samples[idx].real() << ' ';
        //     }
        //     std::cout << '\n';
        // }

        std::cout << "\n[Before inversion]\n";
        std::cout << "\tpre: ";
        for (std::size_t i = 0; i < 32 && i < bits.size(); ++i) {
            std::cout << int(bits[i]);
        }
        std::cout << "\n\tseq: ";
        for (std::size_t i = 32; i < 48 && i < bits.size(); ++i) {
            std::cout << int(bits[i]);
        }
        std::cout << "\n\tlengthflags: ";
        for (std::size_t i = 48; i < 64 && i < bits.size(); ++i) {
            std::cout << int(bits[i]);
        }
        std::cout << "\n\tchecksum: ";
        for (std::size_t i = 152; i < 184 && i < bits.size(); ++i) {
            std::cout << int(bits[i]);
        }
        std::cout << '\n';
        std::cout << '\n';
#endif

        if (detection.inverted) {
            for (auto& bit : bits) {
                bit ^= 1U;
            }
        }

#if VERBOSE
        std::cout << "[After inversion]\n";
        std::cout << "\tpre: ";
        for (std::size_t i = 0; i < 32 && i < bits.size(); ++i) {
            std::cout << int(bits[i]);
        }
        std::cout << "\n\tseq: ";
        for (std::size_t i = 32; i < 48 && i < bits.size(); ++i) {
            std::cout << int(bits[i]);
        }
        std::cout << "\n\tlengthflags: ";
        for (std::size_t i = 48; i < 64 && i < bits.size(); ++i) {
            std::cout << int(bits[i]);
        }
        std::cout << "\n\tchecksum: ";
        for (std::size_t i = 152; i < 184 && i < bits.size(); ++i) {
            std::cout << int(bits[i]);
        }
        std::cout << '\n';
        std::cout << '\n';

        uint32_t test = 0;

        for (std::size_t i = 0; i < PREAMBLE_SIZE; ++i) {
            if (i >= bits.size()) {
                break;
            }
            test = (test << 1) | (bits[i] & 1U);
        }

        std::cout << "\tSignal inverted: " << (detection.inverted ? "yes" : "no") << std::endl;
        std::cout << "\tExpected preamble: 0x" << std::hex << BARKER_PREAMBLE << std::dec
                  << std::endl;
        std::cout << "\tPreamble after correction: 0x" << std::hex << test << std::dec << std::endl;
#endif

        bool ok = true;
        const auto parsed_packet = builder.parse(bits, &ok);
        if (!parsed_packet.has_value()) {
            // std::cerr << "[RX] Packet parse failed\n";
            continue;
        }
        if (!ok) {
            m_rejected.push_back(*parsed_packet);
            continue;
        }

        received_packets.push_back(*parsed_packet);
    }

    return received_packets;
}