#include "helpers.h"
#include "pipeline/tx_pipeline.h"

#include <iostream>
#include <memory>

int main(int argc, char** argv) {
    TestConfig config;
    if (!parse_arguments(argc, argv, config, Node::TX)) {
        return 1;
    }

    std::cout << "========================================" << std::endl;
    std::cout << "TRANSMITTER" << std::endl;
    std::cout << "========================================" << std::endl;

    std::cout << "TX serial:     " << config.tx_serial << std::endl;
    std::cout << "TX channels:   " << config.tx_channels << std::endl;
    std::cout << "Packets:       " << config.num_packets << std::endl;
    std::cout << "Payload size:  " << config.payload_size << " bytes" << std::endl;

    // Initialize TX SDR only
    auto tx = std::make_unique<Transmitter>(config.tx_serial, config.tx_channels);

    // Artificial payload
    std::vector<std::vector<uint8_t>> transmitted_payloads;
    transmitted_payloads.reserve(config.num_packets);
    for (std::size_t i = 0; i < config.num_packets; ++i) {
        transmitted_payloads.push_back(make_test_payload(i, config.payload_size));
    }

    TxPipeline tx_pipeline;
    size_t num_sent;

    std::cout << "[TX] SDR initialized. Starting transmission in 3 seconds..." << std::endl;
    std::this_thread::sleep_for(std::chrono::seconds(4));
    std::cout << "[TX] Transmission started" << std::endl;

    const auto t0 = std::chrono::steady_clock::now();

    for (std::size_t i = 0; i < config.num_packets; ++i) {
        auto samples = tx_pipeline.generate_samples(transmitted_payloads[i]);

        if (samples.empty()) {
            std::cerr << "[ERROR] TX generated no samples\n";
            return 1;
        }

        // Avoid filter transients
        samples.insert(samples.begin(), config.LEAD_IN, std::complex<float>{0.f, 0.f});
        samples.insert(samples.end(), config.TAIL, std::complex<float>{0.f, 0.f});

        std::vector<std::vector<std::complex<float>>> tx_samples;
        tx_samples.resize(config.tx_channels);

        tx_samples[0] = samples;
        if (config.tx_channels == 2) {
            tx_samples[1] = samples;
        }

        num_sent = tx->send(tx_samples);

        if (num_sent != samples.size()) {
            std::cerr << "[ERROR] Sample count mismatch: expected " << samples.size() << ", sent "
                      << num_sent << '\n';
            return 1;
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(settings::TX_DELAY_MS));
    }

    std::cout << "[OK] Transmission complete" << std::endl;
    std::this_thread::sleep_for(std::chrono::milliseconds(500));

    double tx_s = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
    std::cout << "TX loop took " << tx_s << " s\n";

    return 0;
}