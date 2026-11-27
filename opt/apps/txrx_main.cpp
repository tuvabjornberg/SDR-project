// #include "common/config.h"
#include "common/settings.h"
// #include "mac/packet.h"
#include "mac/preamble.h"
// #include "pipeline/rx_pipeline.h"
#include "helpers.h"
#include "pipeline/tx_pipeline.h"
#include "radio/receiver.h"
#include "radio/transmitter.h"

#include <chrono>
#include <cmath>
#include <complex>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <random>
#include <string>
#include <sys/resource.h>
#include <thread>
#include <vector>

// Fixed seed
std::mt19937 noise_rng(987654);

static void add_awgn(std::vector<std::complex<float>>& samples, double snr_db,
                     std::size_t skip = 0) {
    double power = 0.0;
    for (const auto& s : samples)
        power += std::norm(s);
    power /= samples.size();

    const double noise_power = power / std::pow(10.0, snr_db / 10.0);
    std::normal_distribution<float> n(0.f, static_cast<float>(std::sqrt(noise_power / 2.0)));

    for (std::size_t k = skip; k < samples.size(); ++k)
        samples[k] += std::complex<float>{n(noise_rng), n(noise_rng)};
}

SdrDevices initialize_sdrs(const TestConfig& options, std::size_t num_tx_channels,
                           std::size_t num_rx_channels) {
    SdrDevices devices;

    if (options.single_sdr) {
        auto usrp = uhd::usrp::multi_usrp::make("serial=" + options.tx_serial);

        usrp->set_clock_source("internal");
        usrp->set_tx_subdev_spec(TX_SUBDEV);
        usrp->set_rx_subdev_spec(RX_SUBDEV);

        devices.tx = std::make_unique<Transmitter>(usrp, num_tx_channels);
        devices.rx = std::make_unique<Receiver>(usrp, num_rx_channels);

    } else {
        devices.tx = std::make_unique<Transmitter>(options.tx_serial, num_tx_channels);
        devices.rx = std::make_unique<Receiver>(options.rx_serial, num_rx_channels);
    }

    return devices;
}

int run_loopback_test(const TestConfig& config, bool use_noise) {
    constexpr std::size_t LEAD_IN = 2400;
    constexpr std::size_t TAIL = 400;
    constexpr std::size_t PACKET_GAP = 2400;

    TxPipeline tx_pipeline;

    std::vector<std::vector<uint8_t>> transmitted_payloads;
    std::vector<std::complex<float>> tx_samples;

    const std::vector<std::optional<double>> SNR_DB = {
        std::nullopt, // 0: no noise
        std::nullopt, // 1: no noise
        -3.0,         // 2: light
        -6.0,         // 3: medium
        -18.0         // 4: heavy
    };

    for (std::size_t i = 0; i < config.num_packets; ++i) {
        auto payload = make_test_payload(i, config.payload_size);
        transmitted_payloads.push_back(payload);

        auto packet_samples = tx_pipeline.generate_samples(payload);

        if (packet_samples.empty()) {
            std::cerr << "TX generated no samples\n";
            return 1;
        }

        if (i == 0) {
            tx_samples.insert(tx_samples.end(), LEAD_IN, std::complex<float>{0.f, 0.f});
        } else {
            tx_samples.insert(tx_samples.end(), PACKET_GAP, std::complex<float>{0.f, 0.f});
        }

        if (use_noise) {
            constexpr std::size_t PREAMBLE_GUARD =
                PREAMBLE_SAMPLES + (FILTER_SPAN * SAMPLES_PER_SYMBOL) / 2;
            add_awgn(packet_samples, *SNR_DB[i % SNR_DB.size()], PREAMBLE_GUARD);
        }

        tx_samples.insert(tx_samples.end(), packet_samples.begin(), packet_samples.end());
    }

    tx_samples.insert(tx_samples.end(), TAIL, std::complex<float>{0.f, 0.f});

    std::vector<std::vector<std::complex<float>>> captured_samples(2);

    captured_samples[0] = tx_samples;
    captured_samples[1].resize(tx_samples.size(), std::complex<float>{0.f, 0.f});

    evaluate(use_noise ? "LOOPBACK WITH NOISE" : "LOOPBACK", true, true, config,
             transmitted_payloads, captured_samples);

    /* Results for noise should be:
            Packets transmitted:          50
            Detected packets:             45
            Successfully decoded packets: 21
            Parser failures:              24
            Packets lost:                 29
            Packets w. invalid sequence nr: 6

            PDR: 42 %
            PER: 58 %

            Total bits compared: 13728
            Bit errors:          359
            BER:                 2.61509 %
    */

    return 0;
}

int main(int argc, char** argv) {
    TestConfig config;
    if (!parse_arguments(argc, argv, config, Node::TXRX)) {
        return 1;
    }

    std::cout << "========================================" << std::endl;
    std::cout << "B210 PACKET TEST" << std::endl;
    std::cout << "========================================" << std::endl;

    std::cout << "Mode:           " << config.mode << std::endl;
    std::cout << "TX:             " << config.tx_serial << std::endl;
    std::cout << "TX channels:    "
              << ((config.mode == "loopback" || config.mode == "loopback-noise")
                      ? "N/A"
                      : std::to_string(config.tx_channels))
              << std::endl;
    std::cout << "RX:             " << config.rx_serial << std::endl;
    std::cout << "RX channels:    "
              << ((config.mode == "loopback" || config.mode == "loopback-noise")
                      ? "N/A"
                      : std::to_string(config.rx_channels))
              << std::endl;
    std::cout << "Packets:        " << config.num_packets << std::endl;
    std::cout << "Payload size:   " << config.payload_size << " bytes" << std::endl;
    std::cout << "Packet interval:" << config.packet_interval_ms << " ms\n" << std::endl;

    if (config.mode == "loopback") {
        return run_loopback_test(config, false);
    }
    if (config.mode == "loopback-noise") {
        return run_loopback_test(config, true);
    }
    if (!config.replay_file.empty()) {
        return run_replay_test(config.replay_file);
    }

    SdrDevices sdr_devices = initialize_sdrs(config, config.tx_channels, config.rx_channels);
    std::unique_ptr<Transmitter> tx = std::move(sdr_devices.tx);
    std::unique_ptr<Receiver> rx = std::move(sdr_devices.rx);

    TxPipeline tx_pipeline;
    std::vector<std::vector<uint8_t>> transmitted_payloads;
    if (config.input_file.empty()) {
        // Artificial payload
        transmitted_payloads.reserve(config.num_packets);
        for (std::size_t i = 0; i < config.num_packets; ++i) {
            transmitted_payloads.push_back(make_test_payload(i, config.payload_size));
        }
    } else {
        transmitted_payloads = tx_pipeline.file_reader(config.input_file, config.payload_size);
        std::cout << "Number of packets generated from " << config.input_file << ": "
                  << transmitted_payloads.size() << std::endl;
        config.num_packets = transmitted_payloads.size();
    }

    // RX
    config.capture_s = capture_time(config);
    const auto capture_duration = std::chrono::duration<double>(config.capture_s);

    std::vector<std::vector<std::complex<float>>> captured_samples(config.rx_channels);
    for (auto& channel : captured_samples) {
        channel.reserve(static_cast<std::size_t>(config.capture_s * SAMPLE_FREQ * 1.2));
    }

    rx->start();

    auto start = std::chrono::steady_clock::now();
    std::thread rx_thread([&]() {
        while (std::chrono::steady_clock::now() - start < capture_duration) {

            auto chunk = rx->receive(settings::RX_CHUNK_SIZE);

            for (std::size_t ch = 0; ch < chunk.samples.size(); ++ch) {
                captured_samples[ch].insert(captured_samples[ch].end(), chunk.samples[ch].begin(),
                                            chunk.samples[ch].end());
            }
        }
    });

    std::this_thread::sleep_for(std::chrono::milliseconds(200)); // after rx.start(), before TX

    // TX
    size_t num_sent;

    const auto t0 = std::chrono::steady_clock::now();

    for (std::size_t i = 0; i < transmitted_payloads.size(); ++i) {
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

    // RX
    rx->stop();
    rx_thread.join();
    std::cout << "[OK] RX capture complete" << std::endl;

    if (config.record) { // Recording
        std::string recording_dir = "tests/recordings/" + config.label;
        std::filesystem::create_directories(recording_dir);

        // RX samples
        for (std::size_t ch = 0; ch < captured_samples.size(); ++ch) {
            std::string filename = recording_dir + "/rx" + std::to_string(ch) + ".iq";
            iq_recording::save_channel(captured_samples[ch], filename);
            std::cout << "Saved RX channel " << ch << ": " << captured_samples[ch].size()
                      << " samples\n";
        }

        // Payload
        std::string payload_filename = recording_dir + "/payloads.bin";
        iq_recording::save_payloads(transmitted_payloads, payload_filename);

        // Config
        save_metadata(config, recording_dir);

        std::cout << "[OK] Recording saved to: " << recording_dir << std::endl;
    }

    if (config.mode == "siso") {
        evaluate("RX0 only", true, false, config, transmitted_payloads, captured_samples);
    } else if (config.mode == "simo") {
        evaluate("RX0 only", true, false, config, transmitted_payloads, captured_samples);
        evaluate("RX1 only", false, true, config, transmitted_payloads, captured_samples);
        evaluate("RX0+RX1 (MRC)", true, true, config, transmitted_payloads, captured_samples);
    } else if (config.mode == "mimo") {
        // evaluate("RX0 only", true, false, config, transmitted_payloads, captured_samples);
        // evaluate("RX1 only", false, true, config, transmitted_payloads, captured_samples);
        evaluate("RX0+RX1 (MRC)", true, true, config, transmitted_payloads, captured_samples);
    } else if (config.mode == "loopback" || config.mode == "loopback-noise") {
        evaluate(config.mode.data(), true, true, config, transmitted_payloads, captured_samples);
    }

    return 0;
}