#include "common/config.h"
#include "mac/packet_builder.h"
#include "phy/filter.h"
#include "phy/modulator.h"
#include "phy/synchronizer.h"
#include "radio/receiver.h"
#include "radio/transmitter.h"

#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <string>
#include <thread>
#include <vector>

int main() {
    auto usrp = uhd::usrp::multi_usrp::make(TX_SERIAL);

    usrp->set_clock_source("internal");

    usrp->set_tx_subdev_spec(TX_SUBDEV);
    usrp->set_rx_subdev_spec(RX_SUBDEV);

    Transmitter tx(usrp);
    Receiver rx(usrp);

    rx.start();

    PacketBuilder builder;
    const std::string message = "Hello B210!";

    const uint8_t flags = 0;

    std::vector<uint8_t> payload(message.begin(), message.end());

    auto packets = builder.build(payload, flags);

    Modulator modulator;
    RRCFilter rrc_filter;

    for (const auto& packet : packets) {
        std::cout << "Packet built\n";
        std::cout << "------------------------\n";
        std::cout << "Preamble: 0x" << std::hex << std::setw(8) << std::setfill('0')
                  << packet.preamble << std::dec << std::endl;
        std::cout << "Sequence: " << packet.header.sequence << std::endl;

        uint16_t length = get_length(packet.header);
        std::cout << "Length:   " << length << std::endl;

        uint8_t flags_o = get_flags(packet.header);
        std::cout << "Flags:    0x" << std::hex << static_cast<unsigned>(flags_o) << std::dec
                  << std::endl;
        std::cout << "Payload:  " << std::string(packet.payload.begin(), packet.payload.end())
                  << std::endl;
        std::cout << "Checksum: 0x" << std::hex << std::setw(8) << std::setfill('0')
                  << packet.checksum << std::dec << std::endl;

        auto bit_vector = builder.packet_to_bits(packet);

        auto samples = modulator.bpsk_modulate(bit_vector, SAMPLES_PER_SYMBOL);

        std::cout << "\nBPSK Modulation\n";
        std::cout << "------------------------\n";
        std::cout << "Bits:    " << bit_vector.size() << std::endl;
        std::cout << "Samples: " << samples.size() << std::endl;

        std::cout << "First 10 samples:\n";
        for (std::size_t i = 0; i < 10; ++i) {
            std::cout << i << ": " << samples[i].real() << " + j" << samples[i].imag() << std::endl;
        }

        samples = rrc_filter.filter(
            samples, rrc_filter.root_raised_cosine(GAIN, SAMPLE_FREQ, SYMBOL_RATE, ROLL_OFF_FACTOR,
                                                   FILTER_SPAN * SAMPLES_PER_SYMBOL + 1));

        std::cout << "\nRRC Filter\n";
        std::cout << "------------------------\n";
        std::cout << "Bits:    " << bit_vector.size() << std::endl;
        std::cout << "Samples: " << samples.size() << std::endl;

        std::cout << "First 10 samples:\n";
        for (std::size_t i = 0; i < 10; ++i) {
            std::cout << i << ": " << samples[i].real() << " + j" << samples[i].imag() << std::endl;
        }

        float max_magnitude = 0.0f;
        double power = 0.0;

        for (const auto& sample : samples) {
            float magnitude = std::abs(sample);
            max_magnitude = std::max(max_magnitude, magnitude);
            power += std::norm(sample);
        }

        if (max_magnitude > 0.7f) {
            for (auto& sample : samples)
                sample *= 0.7f / max_magnitude;
        }

        double average_power = power / samples.size();
        double rms = std::sqrt(average_power);

        std::cout << "\nRRC Signal Statistics\n";
        std::cout << "------------------------\n";
        std::cout << "Max magnitude:  " << max_magnitude << '\n';
        std::cout << "Average power:  " << average_power << '\n';
        std::cout << "RMS magnitude:  " << rms << '\n';

        std::cout << "\nTransmission" << std::endl;
        std::cout << "------------------------" << std::endl;
        size_t num_sent = tx.send(samples);
        std::cout << "Transmitted " << num_sent << " samples\n";
        std::this_thread::sleep_for(std::chrono::milliseconds(100)); // tmp, change/remove
    }

    Synchronizer synchronizer;

    const auto taps = rrc_filter.root_raised_cosine(GAIN, SAMPLE_FREQ, SYMBOL_RATE, ROLL_OFF_FACTOR,
                                                    FILTER_SPAN * SAMPLES_PER_SYMBOL + 1);
    // For 65 taps keep the previous 64 input samples.
    const std::size_t history_size = taps.size() - 1;
    std::vector<std::complex<float>> history(history_size, {0.0f, 0.0f});

    while (true) {
        auto samples = rx.receive(4096); // TODO Look into chunck size

        if (samples.empty()) {
            continue;
        }
        std::cout << "Received " << samples.size() << " samples\n";

        // Join previous samples so filtering continuous
        std::vector<std::complex<float>> input;
        input.reserve(history.size() + samples.size());
        input.insert(input.end(), history.begin(), history.end());
        input.insert(input.end(), samples.begin(), samples.end());

        auto filtered = rrc_filter.filter(input, taps);

        // Keep only output corresponding to the new chunk
        std::vector<std::complex<float>> filtered_samples(filtered.begin() + history_size,
                                                          filtered.end());
        std::cout << "Filtered " << filtered_samples.size() << " samples\n";

        // Update raw history for the next chunk
        if (input.size() >= history_size) {
            history.assign(input.end() - history_size, input.end());
        }

        auto detections = synchronizer.process(filtered_samples);

        for (const auto offset : detections) {
            std::cout << "Preamble detected at filtered sample offset " << offset << '\n';
        }
    }
    return 0;
}
