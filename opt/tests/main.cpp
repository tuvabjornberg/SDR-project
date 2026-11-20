#include "common/config.h"
#include "common/settings.h"
#include "mac/packet.h"
#include "mac/preamble.h"
#include "pipeline/rx_pipeline.h"
#include "pipeline/tx_pipeline.h"
#include "radio/receiver.h"
#include "radio/transmitter.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <complex>
#include <cstddef>
#include <cstdint>
#include <fstream>
#include <iostream>
#include <random>
#include <string>
#include <sys/resource.h>
#include <thread>
#include <vector>

struct PacketLog {
    uint32_t sequence = 0;
    double tx_time = 0.0;
    double rx_time = 0.0;
    bool received = false;
    std::size_t bit_errors = 0;
};

struct TestConfig {
    std::size_t num_packets = 50;
    std::size_t payload_size = 32;
    int packet_interval_ms = 10;

    std::string mode;
    bool single_sdr;
    std::string tx_serial;
    std::string rx_serial;
};

struct SdrDevices {
    std::unique_ptr<Transmitter> tx;
    std::unique_ptr<Receiver> rx;
};

// Fixed seed
std::mt19937 rng(12345);
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

// Artificial payload
// Counter that wraps after 255
std::vector<uint8_t> make_test_payload(std::size_t sequence, std::size_t payload_size) {
    std::vector<uint8_t> payload(payload_size);

    for (std::size_t i = 0; i < payload_size; ++i) {
        payload[i] = static_cast<uint8_t>((sequence + i) & 0xFF);
    }

    return payload;
}

std::size_t bit_cnt(uint32_t value) {
    std::size_t count = 0;

    while (value != 0) {
        count += value & 1U;
        value >>= 1;
    }

    return count;
}

// void save_results_csv(const TestConfig& config, const TestResults& results,
//                       const std::string& filename) {
//     std::ofstream file(filename);

//     file << "num_packets,payload_size,interval_ms,"
//             "packets_tx,packets_detected,packets_decoded,"
//             ",packets_lost,"
//             "pdr,per,bit_errors,bits_compared,ber,"
//             "rx_overflows,cpu_user,cpu_system\n";

//     file << config.num_packets << "," << config.payload_size << "," << config.packet_interval_ms
//          << "," << results.packets_transmitted << "," << results.packets_detected << ","
//          << results.packets_decoded << "," << results.packets_lost << "," << results.pdr << ","
//          << results.per << "," << results.bit_errors << "," << results.bits_compared << ","
//          << results.ber << "," << results.rx_overflows << "," << results.cpu_user_time << ","
//          << results.cpu_system_time << "\n";
// }

// void save_packet_log(const std::vector<PacketLog>& logs, const std::string& filename) {
//     std::ofstream file(filename);

//     file << "sequence,tx_time,rx_time,latency_ms,received,bit_errors\n";

//     for (const auto& p : logs) {

//         double latency_ms = -1.0;

//         if (p.received) {
//             latency_ms = (p.rx_time - p.tx_time) * 1000.0;
//         }

//         file << p.sequence << "," << p.tx_time << "," << p.rx_time << "," << latency_ms << ","
//              << p.received << "," << p.bit_errors << "\n";
//     }
// }

void print_options() {
    std::cout << "Usage: ./main  [--loopback | --loopback-noise | --siso | --simo | --mimo]  "
                 "[--single-node | --dual-node] --tx-serial <TX_SERIAL> "
                 "--rx-serial <RX_SERIAL>\n";
    std::cout << "Options:\n";
    std::cout << "  --loopback              Run a loopback test without SDR hardware" << std::endl;
    std::cout << "  --loopback-noise        Run a loopback test without SDR hardware and "
                 "with noise added to the signal"
              << std::endl;
    std::cout << "  --siso                  Transmit from 1 channel (TX/RX A) and receive on 1 "
                 "channel (RX2 A)"
              << std::endl;
    std::cout << "  --simo                  Transmit from 1 channel (TX/RX A) and receive on 2 "
                 "channels (RX2 A & RX2 B)"
              << std::endl;
    std::cout << "  --mimo                  Transmit from 2 channels (TX/RX A & TX/RX B) and "
                 "receive on 2 channels (RX2 A & RX2 B)"
              << std::endl;
    std::cout << "  --tx-serial <TX_SERIAL> Specify the serial number of the TX SDR" << std::endl;
    std::cout << "  --rx-serial <RX_SERIAL> Specify the serial number of the RX SDR (required in "
                 "dual-node mode)"
              << std::endl;
}

bool parse_arguments(int argc, char** argv, TestConfig& options) {
    if (argc < 2) {
        std::cerr << "Error: arguments missing" << std::endl;
        print_options();
        return false;
    }

    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];

        if (arg == "--loopback") {
            options.mode = "loopback";
        } else if (arg == "--loopback-noise") {
            options.mode = "loopback-noise";
        } else if (arg == "--siso") {
            options.mode = "siso";
        } else if (arg == "--simo") {
            options.mode = "simo";
        } else if (arg == "--mimo") {
            options.mode = "mimo";
        } else if (arg == "--tx-serial" && i + 1 < argc) {
            options.tx_serial = argv[++i];
        } else if (arg == "--rx-serial" && i + 1 < argc) {
            options.rx_serial = argv[++i];
        } else {
            std::cerr << "Unknown or incomplete argument: " << arg << '\n';
            print_options();
            return false;
        }
    }

    if (options.mode != "loopback" && options.mode != "loopback-noise") {
        if (options.tx_serial.empty() || options.rx_serial.empty()) {
            std::cerr << "Error: --tx-serial and --rx-serial is required" << std::endl;
            print_options();
            return false;
        }

        if (options.tx_serial == options.rx_serial) {
            options.single_sdr = true;
        } else {
            options.single_sdr = false;
        }
    }

    return true;
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

void evaluate(const char* name, bool use0, bool use1, const TestConfig& config,
              const std::vector<std::vector<uint8_t>>& transmitted_payloads,
              const std::vector<std::vector<std::complex<float>>>& captured_samples) {
    auto in = captured_samples;

    if (use0 && in.size() >= 1) {
    } else if (!in.empty()) {
        std::fill(in[0].begin(), in[0].end(), std::complex<float>{});
    }

    if (use1 && in.size() >= 2) {
    } else if (in.size() >= 2) {
        std::fill(in[1].begin(), in[1].end(), std::complex<float>{});
    }

    RxPipeline rx_pipeline;

    auto received_packets = rx_pipeline.decode_packets(in);
    auto rejected_packets = rx_pipeline.rejected();

    auto all_packets = received_packets;
    all_packets.insert(all_packets.end(), rejected_packets.begin(), rejected_packets.end());

    std::cout << name << ": decoded " << received_packets.size() << " / " << config.num_packets
              << ", parser failures " << rx_pipeline.stats().parse_failures << std::endl;

    std::size_t total_bits = 0;
    std::size_t error_bits = 0;

    if (config.mode == "siso" || (use0 && use1)) {
        int invalid_seq = 0;

        for (const auto& packet : all_packets) {
            const auto sequence = packet.header.sequence;

            if (sequence < transmitted_payloads.size()) {
                Packet builder;

                auto original_packet = builder.build(
                    transmitted_payloads[sequence], 0,
                    sequence); // TODO: artificial payload creates 1 packet per message,
                               // but if message is too long, it will be split into
                               // multiple packets. Need to handle this case.

                // std::cout << "Received packet: " << sequence
                //           << ", payload size: " << packet.payload.size() << " bytes,
                //           payload:
                //           ";
                // for (uint8_t byte : packet.payload) {
                //     std::cout << std::hex << std::setw(2) << std::setfill('0')
                //               << static_cast<int>(byte) << " ";
                // }
                // std::cout << std::dec << std::endl;

                const auto& tx_payload = transmitted_payloads[sequence];
                const auto& rx_payload = packet.payload;

                if (transmitted_payloads[sequence].size() != rx_payload.size()) {
                    std::cout << " -> PAYLOAD SIZE MISMATCH, "
                              << "Expected: " << tx_payload.size() << ", Got: " << rx_payload.size()
                              << std::endl;
                }

                size_t preamble_errors = __builtin_popcount(
                    static_cast<unsigned int>(original_packet.preamble ^ packet.preamble));
                size_t header_errors = __builtin_popcount(static_cast<unsigned int>(
                    original_packet.header.sequence ^ packet.header.sequence));
                size_t length_flags_errors = __builtin_popcount(static_cast<unsigned int>(
                    original_packet.header.length_flags ^ packet.header.length_flags));
                size_t checksum_errors = __builtin_popcount(
                    static_cast<unsigned int>(original_packet.checksum ^ packet.checksum));
                std::size_t payload_errors = 0;
                for (std::size_t j = 0; j < tx_payload.size(); ++j) {
                    payload_errors += __builtin_popcount(
                        static_cast<unsigned int>(tx_payload[j] ^ rx_payload[j]));
                }

                size_t packet_errors = preamble_errors + header_errors + length_flags_errors +
                                       payload_errors + checksum_errors;

                const std::size_t packet_bits =
                    tx_payload.size() * 8 + PREAMBLE_SIZE + HEADER_SIZE + CHECKSUM_SIZE;

                total_bits += packet_bits;
                error_bits += packet_errors;

                if (packet_errors != 0) {
                    std::cout << " -> MISMATCH for packet " << sequence
                              << ", valid : " << packet.valid << ""
                              << " \n total bit errors: " << packet_errors << " of "
                              << packet_bits
                              //   << " bits\n preamble: " << preamble_errors
                              //   << "\n header: " << header_errors
                              //   << "\n length_flags: " << length_flags_errors
                              //   << "\n payload: " << payload_errors
                              //   << "\n checksum: " << checksum_errors
                              << std::endl;
                }

            } else {
                std::cout << "Received packet with invalid sequence: " << sequence << '\n';
                ++invalid_seq;
            }
        }

        std::cout << "\n========================================\n";
        std::cout << "TEST RESULTS\n";
        std::cout << "========================================\n";

        const auto& rx_stats = rx_pipeline.stats();
        std::cout << "Packets transmitted:          " << config.num_packets << std::endl;
        std::cout << "Detected packets:             " << rx_stats.preambles_detected << std::endl;
        std::cout << "Successfully decoded packets: " << rx_stats.packets_decoded << std::endl;
        std::cout << "Parser failures:              " << rx_stats.parse_failures << std::endl;
        std::cout << "Packets lost:                 "
                  << config.num_packets - rx_stats.packets_decoded << std::endl;
        std::cout << "Packets w. invalid sequence nr: " << invalid_seq << std::endl;

        const double pdr = static_cast<double>(rx_stats.packets_decoded) / config.num_packets;

        std::cout << "\nPDR: " << pdr * 100.0 << " %" << std::endl;
        std::cout << "PER: " << (1.0 - pdr) * 100.0 << " %" << std::endl;

        const double ber = total_bits > 0 ? static_cast<double>(error_bits) / total_bits : 0.0;

        std::cout << "\nTotal bits compared: " << total_bits << std::endl;
        std::cout << "Bit errors:          " << error_bits << std::endl;
        std::cout << "BER:                 " << ber * 100.0 << " %" << std::endl;
    }
};

//  struct rusage cpu_end{};
//     getrusage(RUSAGE_SELF, &cpu_end);

//     double cpu_user_time = (cpu_end.ru_utime.tv_sec - cpu_start.ru_utime.tv_sec) +
//                            1e-6 * (cpu_end.ru_utime.tv_usec - cpu_start.ru_utime.tv_usec);

//     double cpu_system_time = (cpu_end.ru_stime.tv_sec - cpu_start.ru_stime.tv_sec) +
//                              1e-6 * (cpu_end.ru_stime.tv_usec - cpu_start.ru_stime.tv_usec);

//     std::cout << "CPU user time:   " << cpu_user_time << " s" << std::endl;
//     std::cout << "CPU system time: " << cpu_system_time << " s" << std::endl;
//     std::cout << "CPU total time:  " << cpu_user_time + cpu_system_time << " s" << std::endl;
//     std::cout << "CPU time/packet: " << (cpu_user_time + cpu_system_time) / config.num_packets
//               << " s" << std::endl;

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

    // const std::vector<double> snr_db = {20.0, 20.0, -3.0, -6.0, -18.0};

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
    if (!parse_arguments(argc, argv, config)) {
        return 1;
    }

    std::string mode = config.mode;
    std::string tx_serial = config.tx_serial;
    std::string rx_serial = config.rx_serial;

    // const std::size_t num_tx_channels = (mode == "mimo") ? 2 : 1;
    // const std::size_t num_rx_channels = (mode == "siso") ? 1 : 2;
    const std::size_t num_tx_channels =
        (mode == "mimo" || (mode == "simo" && config.single_sdr)) ? 2 : 1;
    const std::size_t num_rx_channels = (mode == "siso") ? 1 : 2;

    // struct rusage cpu_start{};
    // getrusage(RUSAGE_SELF, &cpu_start);

    // std::vector<PacketLog> packet_logs(config.num_packets);

    std::cout << "========================================" << std::endl;
    std::cout << "B210 PACKET TEST" << std::endl;
    std::cout << "========================================" << std::endl;

    std::cout << "Mode:           " << mode << std::endl;
    std::cout << "TX:             " << config.tx_serial << std::endl;
    std::cout << "TX channels:    "
              << ((mode == "loopback" || mode == "loopback-noise")
                      ? "N/A"
                      : std::to_string(num_tx_channels))
              << std::endl;
    std::cout << "RX:             " << config.rx_serial << std::endl;
    std::cout << "RX channels:    "
              << ((mode == "loopback" || mode == "loopback-noise")
                      ? "N/A"
                      : std::to_string(num_rx_channels))
              << std::endl;
    std::cout << "Packets:        " << config.num_packets << std::endl;
    std::cout << "Payload size:   " << config.payload_size << " bytes" << std::endl;
    std::cout << "Packet interval:" << config.packet_interval_ms << " ms\n" << std::endl;

    if (mode == "loopback") {
        return run_loopback_test(config, false);
    }

    if (mode == "loopback-noise") {
        return run_loopback_test(config, true);
    }

    SdrDevices sdr_devices = initialize_sdrs(config, num_tx_channels, num_rx_channels);
    std::unique_ptr<Transmitter> tx = std::move(sdr_devices.tx);
    std::unique_ptr<Receiver> rx = std::move(sdr_devices.rx);

    // Artificial payload
    std::vector<std::vector<uint8_t>> transmitted_payloads;
    transmitted_payloads.reserve(config.num_packets);
    for (std::size_t i = 0; i < config.num_packets; ++i) {
        transmitted_payloads.push_back(make_test_payload(i, config.payload_size));
    }

    // RX
    // std::array<std::vector<std::complex<float>>, 2> captured_samples; // one buffer per RX
    // channel
    // for (auto& c : captured_samples)
    //     c.reserve(static_cast<std::size_t>(settings::RX_CAPTURE_SECONDS * SAMPLE_FREQ * 1.2));

    std::vector<std::vector<std::complex<float>>> captured_samples(num_rx_channels);
    for (auto& channel : captured_samples) {
        channel.reserve(static_cast<std::size_t>(settings::RX_CAPTURE_SECONDS * SAMPLE_FREQ * 1.2));
    }

    rx->start();

    auto start = std::chrono::steady_clock::now();
    std::thread rx_thread([&]() {
        while (std::chrono::steady_clock::now() - start <
               std::chrono::seconds(settings::RX_CAPTURE_SECONDS)) {

            auto chunk = rx->receive(settings::RX_CHUNK_SIZE);

            for (std::size_t ch = 0; ch < chunk.samples.size(); ++ch) {
                captured_samples[ch].insert(captured_samples[ch].end(), chunk.samples[ch].begin(),
                                            chunk.samples[ch].end());
            }
        }
    });

    std::this_thread::sleep_for(std::chrono::milliseconds(200)); // after rx.start(), before TX

    // TX
    TxPipeline tx_pipeline;
    size_t num_sent;
    constexpr std::size_t LEAD_IN = 2400, TAIL = 400;

    for (std::size_t i = 0; i < config.num_packets; i++) {
        auto samples = tx_pipeline.generate_samples(transmitted_payloads[i]);
        // Inital and final zeros to avoid filter transients
        samples.insert(samples.begin(), LEAD_IN, std::complex<float>{0.f, 0.f});
        samples.insert(samples.end(), TAIL, std::complex<float>{0.f, 0.f});

        // packet_logs[i].sequence = i;
        // packet_logs[i].tx_time =
        //     std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch())
        //         .count();

        std::vector<std::vector<std::complex<float>>> tx_samples;
        tx_samples.resize(num_tx_channels);

        tx_samples[0] = samples;
        if (num_tx_channels == 2) {
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

    // RX
    rx->stop();
    rx_thread.join();
    std::cout << "[OK] RX capture complete" << std::endl;

    if (mode == "siso") {
        evaluate("RX0 only", true, false, config, transmitted_payloads, captured_samples);
    } else if (mode == "simo") {
        evaluate("RX0 only", true, false, config, transmitted_payloads, captured_samples);
        evaluate("RX1 only", false, true, config, transmitted_payloads, captured_samples);
        evaluate("RX0+RX1 (MRC)", true, true, config, transmitted_payloads, captured_samples);
    } else if (mode == "mimo") {
        evaluate("RX0 only", true, false, config, transmitted_payloads, captured_samples);
        evaluate("RX1 only", false, true, config, transmitted_payloads, captured_samples);
        evaluate("RX0+RX1 (MRC)", true, true, config, transmitted_payloads, captured_samples);
    } else if (mode == "loopback" || mode == "loopback-noise") {
        evaluate(mode.data(), true, true, config, transmitted_payloads, captured_samples);
    }

    return 0;
}