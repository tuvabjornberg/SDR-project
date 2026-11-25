#include "radio/receiver.h"

#include "common/settings.h"
#include "helpers.h"

#include <chrono>
#include <complex>
#include <cstdint>
#include <iostream>
#include <memory>
#include <vector>

int main(int argc, char** argv) {
    TestConfig config;

    if (!parse_arguments(argc, argv, config, Node::RX)) {
        return 1;
    }

    std::cout << "========================================" << std::endl;
    std::cout << "RECEIVER" << std::endl;
    std::cout << "========================================" << std::endl;

    std::cout << "RX serial:      " << config.rx_serial << std::endl;
    std::cout << "Packets:        " << config.num_packets << std::endl;
    std::cout << "Payload size:   " << config.payload_size << " bytes" << std::endl;
    // std::cout << "Capture time:   " << capture_seconds << " seconds\n";

    // Initialize RX SDR only
    auto rx = std::make_unique<Receiver>(config.rx_serial, config.rx_channels);

    const double capture_s = capture_time(config);
    const auto capture_duration = std::chrono::duration<double>(capture_s);

    std::vector<std::vector<std::complex<float>>> captured_samples(config.rx_channels);
    for (auto& channel : captured_samples) {
        channel.reserve(static_cast<std::size_t>(capture_s * SAMPLE_FREQ * 1.2));
    }

    rx->start();

    std::cout << "\n[OK] Receiver starts" << std::endl;

    const auto start = std::chrono::steady_clock::now();
    while (std::chrono::steady_clock::now() - start <
           std::chrono::duration<double>(capture_duration)) {

        auto chunk = rx->receive(settings::RX_CHUNK_SIZE);

        for (std::size_t ch = 0; ch < chunk.samples.size(); ++ch) {

            captured_samples[ch].insert(captured_samples[ch].end(), chunk.samples[ch].begin(),
                                        chunk.samples[ch].end());
        }
    }

    rx->stop();
    rx.reset();

    std::cout << "\n[OK] RX capture complete\n";
    std::cout << "Captured samples: " << captured_samples[0].size() << '\n';

    // Reconstruct the transmitted payloads.
    std::vector<std::vector<uint8_t>> transmitted_payloads;
    transmitted_payloads.reserve(config.num_packets);
    for (std::size_t i = 0; i < config.num_packets; ++i) {
        transmitted_payloads.push_back(make_test_payload(i, config.payload_size));
    }

    if (config.rx_channels == 1) {
        evaluate("RX0 only", true, false, config, transmitted_payloads, captured_samples);
    } else if (config.rx_channels == 2) {
        evaluate("RX0 only", true, false, config, transmitted_payloads, captured_samples);
        evaluate("RX1 only", false, true, config, transmitted_payloads, captured_samples);
        evaluate("RX0+RX1 (MRC)", true, true, config, transmitted_payloads, captured_samples);
    }

    return 0;
}