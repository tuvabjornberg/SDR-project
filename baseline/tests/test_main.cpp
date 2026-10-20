#include "common/config.h"
#include "mac/packet.h"
#include "mac/preamble.h"
#include "phy/filter.h"
#include "phy/modulator.h"
#include "phy/synchronizer.h"
#include "pipeline/rx_pipeline.h"
#include "pipeline/tx_pipeline.h"

#include <algorithm>
#include <cassert>
#include <cmath>
#include <complex>
#include <cstdint>
#include <iostream>
#include <string>
#include <vector>

int test_mod(std::vector<uint8_t> bits) {
    constexpr std::size_t SAMPLES_PER_SYMBOL = 4;

    Modulator modulator;
    const auto samples = modulator.bpsk_modulate(bits, SAMPLES_PER_SYMBOL);

    // ex 3 symbols × 4 samples/symbol = 12 size
    assert(samples.size() == bits.size() * SAMPLES_PER_SYMBOL);

    for (std::size_t i = 0; i < samples.size(); ++i) {
        const std::size_t symbol_index = i / SAMPLES_PER_SYMBOL;

        const float expected = bits[symbol_index] ? 1.0f : -1.0f;

        assert(std::abs(samples[i].real() - expected) < 1e-6f);

        assert(std::abs(samples[i].imag()) < 1e-6f);
    }

    Demodulator demodulator;

    auto recovered_bits = demodulator.bpsk_demodulate(samples, SAMPLES_PER_SYMBOL);
    assert(recovered_bits.size() == bits.size());

    std::size_t errors = 0;

    for (std::size_t i = 0; i < bits.size() && i < recovered_bits.size(); ++i) {

        if (bits[i] != recovered_bits[i]) {
            ++errors;
        }
    }

    std::cout << "Original bits:  " << bits.size() << '\n';
    std::cout << "Recovered bits: " << recovered_bits.size() << '\n';
    std::cout << "Bit errors:     " << errors << '\n';

    assert(errors == 0);

    std::cout << "BPSK modulator test PASSED\n";

    return 0;
}

std::size_t bit_cnt(uint32_t value) {
    std::size_t count = 0;

    while (value != 0) {
        count += value & 1U;
        value >>= 1;
    }

    return count;
}

int test_sync() {
    Packet builder;
    Modulator modulator;
    Demodulator demodulator;
    RRCFilter tx_filter;
    RRCFilter rx_filter;
    Synchronizer synchronizer;

    const std::string message = "Hello B210!";
    const std::vector<uint8_t> payload(message.begin(), message.end());

    auto packets = builder.build(payload, 0);

    const auto taps = tx_filter.root_raised_cosine(GAIN, SAMPLE_FREQ, SYMBOL_RATE, ROLL_OFF_FACTOR,
                                                   FILTER_SPAN * SAMPLES_PER_SYMBOL + 1);

    const auto rx_taps = rx_filter.root_raised_cosine(
        GAIN, SAMPLE_FREQ, SYMBOL_RATE, ROLL_OFF_FACTOR, FILTER_SPAN * SAMPLES_PER_SYMBOL + 1);

    std::vector<uint8_t> received_payload;
    for (const auto& packet : packets) {
        std::cout << "\n========================================\n";
        std::cout << "Testing packet\n";
        std::cout << "========================================\n";

        // --------------------------------------------------
        // 1. PACKET -> BITS
        // --------------------------------------------------
        const auto original_bits = builder.packet_to_bits(packet);

        std::cout << "Original bits: " << original_bits.size() << '\n';

        std::cout << "Expected preamble: 0x" << std::hex << BARKER_PREAMBLE << std::dec << '\n';

        // --------------------------------------------------
        // 2. BPSK MODULATION
        // --------------------------------------------------
        auto tx_samples = modulator.bpsk_modulate(original_bits, SAMPLES_PER_SYMBOL);

        std::cout << "Modulated samples: " << tx_samples.size() << '\n';

        // --------------------------------------------------
        // 3. TX RRC FILTER
        // --------------------------------------------------
        tx_samples = tx_filter.filter(tx_samples, taps);

        std::cout << "TX filtered samples: " << tx_samples.size() << '\n';

        // --------------------------------------------------
        // 4. ADD CHANNEL DELAY
        // --------------------------------------------------
        auto tx_stream = tx_samples;

        // --------------------------------------------------
        // 5. RX RRC FILTER
        // --------------------------------------------------
        auto rx_samples = rx_filter.filter(tx_stream, rx_taps);

        std::cout << "RX filtered samples: " << rx_samples.size() << '\n';

        // --------------------------------------------------
        // 6. DEMODULATION: TEST EVERY SAMPLING PHASE
        // --------------------------------------------------

        const std::size_t total_delay = (taps.size() - 1) / 2 + (rx_taps.size() - 1) / 2;

        std::cout << "Total filter delay: " << total_delay << '\n';

        std::size_t best_phase = 0;
        std::size_t best_errors = PREAMBLE_SIZE + 1;
        std::vector<uint8_t> packet_bits;

        for (std::size_t phase = 0; phase < SAMPLES_PER_SYMBOL; ++phase) {
            const std::size_t start = total_delay + phase;

            if (start >= rx_samples.size()) {
                continue;
            }

            std::vector<std::complex<float>> aligned_samples(rx_samples.begin() + start,
                                                             rx_samples.end());

            auto bits = demodulator.bpsk_demodulate(aligned_samples, SAMPLES_PER_SYMBOL, 0);

            if (bits.size() < PREAMBLE_SIZE) {
                continue;
            }

            uint32_t received_preamble = 0;

            for (std::size_t i = 0; i < PREAMBLE_SIZE; ++i) {
                received_preamble = (received_preamble << 1) | bits[i];
            }

            const std::size_t normal_errors = bit_cnt(received_preamble ^ BARKER_PREAMBLE);
            const std::size_t inverted_errors = bit_cnt((~received_preamble) ^ BARKER_PREAMBLE);
            const bool inverted = inverted_errors < normal_errors;
            const std::size_t errors = std::min(normal_errors, inverted_errors);

            std::cout << "Phase " << phase << ": preamble errors = " << errors << '\n';

            if (errors < best_errors) {
                best_errors = errors;
                best_phase = phase;
                packet_bits = std::move(bits);

                if (inverted) {
                    for (auto& bit : packet_bits) {
                        bit ^= 1U;
                    }
                }
            }
        }

        if (packet_bits.empty() || best_errors > 3) {
            std::cerr << "FAIL: No valid preamble sampling phase found\n";
            return 1;
        }

        std::cout << "Selected phase: " << best_phase << '\n';
        std::cout << "Preamble errors: " << best_errors << '\n';
        std::cout << "Recovered bits: " << packet_bits.size() << '\n';

        std::cout << "TX bits: " << std::endl;
        std::cout << "pre: ";
        for (std::size_t i = 0; i < std::min<std::size_t>(31, original_bits.size()); ++i) {
            std::cout << static_cast<unsigned>(original_bits[i]);
        }
        std::cout << " header: ";
        for (std::size_t i = 32; i < std::min<std::size_t>(64, original_bits.size()); ++i) {
            std::cout << static_cast<unsigned>(original_bits[i]);
        }

        std::cout << "\nRX bits: " << std::endl;
        std::cout << "pre: ";
        for (std::size_t i = 0; i < std::min<std::size_t>(31, packet_bits.size()); ++i) {
            std::cout << static_cast<unsigned>(packet_bits[i]);
        }
        std::cout << " header: ";
        for (std::size_t i = 32; i < std::min<std::size_t>(64, packet_bits.size()); ++i) {
            std::cout << static_cast<unsigned>(packet_bits[i]);
        }

        // --------------------------------------------------
        // 9. PARSE PACKET
        // --------------------------------------------------
        std::cout << "\n--- Packet Parsing ---\n";

        const auto parsed_packet = builder.parse(packet_bits);

        if (!parsed_packet.has_value()) {
            std::cerr << "FAIL: Packet parsing failed\n";
            synchronizer.reset();
            return 1;
        }

        // --------------------------------------------------
        // 10. VERIFY PACKET
        // --------------------------------------------------
        std::cout << "\n--- Verification ---\n";

        received_payload.insert(received_payload.end(), parsed_packet->payload.begin(),
                                parsed_packet->payload.end());

        std::cout << "Packet payload size: " << parsed_packet->payload.size() << '\n';

        std::cout << "Accumulated payload size: " << received_payload.size() << '\n';

        std::cout << "Checksum: 0x" << std::hex << parsed_packet->checksum << std::dec << '\n';
        synchronizer.reset();
    }

    if (received_payload != payload) {
        std::cerr << "FAIL: Reassembled payload mismatch\n";
        std::cerr << "Expected size: " << payload.size() << '\n';
        std::cerr << "Received size: " << received_payload.size() << '\n';
        return 1;
    }

    std::cout << "\nPASS: Entire message successfully received\n";
    std::cout << "Payload size: " << received_payload.size() << '\n';

    return 0;
}

int test_pipeline() {
    TxPipeline tx;
    RxPipeline rx;

    const std::string message = "Hello B210!";
    const std::vector<uint8_t> original_payload(message.begin(), message.end());

    std::cout << "\n========================================\n";
    std::cout << "TX/RX PIPELINE INTEGRATION TEST\n";
    std::cout << "========================================\n";

    std::cout << "\n--- TX Pipeline ---\n";
    std::cout << "Original payload size: " << original_payload.size() << " bytes\n";

    const auto tx_samples = tx.generate_samples(original_payload);

    std::cout << "Generated TX samples: " << tx_samples.size() << '\n';

    if (tx_samples.empty()) {
        std::cerr << "FAIL: TX pipeline generated no samples\n";
        return 1;
    }

    const auto rx_input = tx_samples;

    std::cout << "\n--- RX Pipeline ---\n";
    const auto received_packets = rx.decode_packets(rx_input);
    std::cout << "Decoded packets: " << received_packets.size() << '\n';

    if (received_packets.empty()) {
        std::cerr << "FAIL: RX pipeline decoded no packets\n";
        return 1;
    }

    const auto received_payload = rx.packet_to_msg(received_packets);

    std::cout << "\n--- Payload Verification ---\n";
    std::cout << "Expected payload size: " << original_payload.size() << " bytes\n";
    std::cout << "Received payload size: " << received_payload.size() << " bytes\n";

    if (received_payload != original_payload) {
        std::cerr << "FAIL: Payload mismatch\n";
        std::cerr << "Expected: " << std::string(original_payload.begin(), original_payload.end())
                  << '\n';
        std::cerr << "Received: " << std::string(received_payload.begin(), received_payload.end())
                  << '\n';
        return 1;
    }

    std::cout << "Original message: "
              << std::string(original_payload.begin(), original_payload.end()) << '\n';
    std::cout << "Received message: "
              << std::string(received_payload.begin(), received_payload.end()) << '\n';
    std::cout << "\nPASS: TX/RX pipeline integration test passed\n";

    return 0;
}

int main() {
    test_mod({1, 0, 1});
    test_mod({
        0, 1, 0, 1, // Alternating bits
        1, 1, 1, 1, // Consecutive ones
        0, 0, 0, 0, // Consecutive zeros
        1, 0, 0, 1, // Mixed pattern
        1, 1, 0, 1  // Irregular pattern
    });
    // test_sync();
    test_pipeline();

    return 0;
}
