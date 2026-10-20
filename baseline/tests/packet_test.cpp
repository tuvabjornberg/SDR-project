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
#include <cstdint>
#include <fstream>
#include <iostream>
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
    std::size_t num_packets = 200;
    std::size_t payload_size = 32;
    int packet_interval_ms = 20;
};

struct TestResults {
    std::size_t packets_transmitted = 0;
    std::size_t packets_decoded = 0;
    std::size_t packets_detected = 0;
    std::size_t packets_received = 0;
    std::size_t packets_lost = 0;

    std::size_t bit_errors = 0;
    std::size_t bits_compared = 0;

    std::size_t rx_overflows = 0;

    double pdr = 0.0;
    double per = 0.0;
    double ber = 0.0;

    double cpu_user_time = 0.0;
    double cpu_system_time = 0.0;
};

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

void calculate_packet_statistics(const std::vector<PacketStruct>& received_packets,
                                 std::size_t num_packets, TestResults& results,
                                 std::vector<PacketLog>& packet_logs) {
    results.packets_received = received_packets.size();

    for (const auto& packet : received_packets) {
        const auto seq = packet.header.sequence;

        if (seq < packet_logs.size()) {
            packet_logs[seq].received = true;

            // placeholder until real RX timestamps
            packet_logs[seq].rx_time =
                std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch())
                    .count();
        }
    }

    results.packets_lost = num_packets - results.packets_received;
    results.pdr = static_cast<double>(results.packets_received) / static_cast<double>(num_packets);
    results.per = 1.0 - results.pdr;
}

void calculate_bit_statistics(const std::vector<PacketStruct>& received_packets,
                              const std::vector<std::vector<uint8_t>>& transmitted_payloads,
                              TestResults& results, std::vector<PacketLog>& packet_logs) {

    for (const auto& packet : received_packets) {
        std::size_t packet_bit_errors = 0;

        const std::size_t sequence = packet.header.sequence;
        if (sequence >= transmitted_payloads.size()) {
            continue;
        }

        const auto& expected = transmitted_payloads[sequence];
        const auto& actual = packet.payload;

        const std::size_t common_size = std::min(expected.size(), actual.size());
        for (std::size_t i = 0; i < common_size; ++i) {
            auto errors = bit_cnt(static_cast<uint32_t>(expected[i] ^ actual[i]));

            packet_bit_errors += errors;
            results.bit_errors += errors;
            results.bits_compared += 8;
        }

        if (sequence < packet_logs.size()) {
            packet_logs[sequence].bit_errors = packet_bit_errors;
        }
    }

    if (results.bits_compared > 0) {
        results.ber =
            static_cast<double>(results.bit_errors) / static_cast<double>(results.bits_compared);
    }
}

void print_results(const TestConfig& config, const TestResults& results) {
    std::cout << "\n";
    std::cout << "========================================\n";
    std::cout << "TEST RESULTS\n";
    std::cout << "========================================\n";

    std::cout << "Packets transmitted: " << results.packets_transmitted << '\n';
    std::cout << "Packets detected:    " << results.packets_detected << '\n';
    std::cout << "Packets decoded:     " << results.packets_decoded << '\n';
    std::cout << "Packets received:    " << results.packets_received << '\n';
    std::cout << "Packets lost:        " << results.packets_lost << '\n';

    std::cout << "\n";

    std::cout << "PDR: " << results.pdr * 100.0 << " %\n";
    std::cout << "PER: " << results.per * 100.0 << " %\n";

    std::cout << "\n";

    std::cout << "Bit errors: " << results.bit_errors << '\n';
    std::cout << "Bits compared: " << results.bits_compared << '\n';
    std::cout << "BER: " << results.ber << '\n';

    std::cout << "\n";

    std::cout << "RX overflows: " << results.rx_overflows << '\n';

    std::cout << "\n";

    std::cout << "CPU user time:   " << results.cpu_user_time << " s\n";
    std::cout << "CPU system time: " << results.cpu_system_time << " s\n";
    std::cout << "CPU total time:  " << results.cpu_user_time + results.cpu_system_time << " s\n";
    std::cout << "CPU time/packet: "
              << (results.cpu_user_time + results.cpu_system_time) / config.num_packets << " s\n";

    std::cout << "========================================\n";
}

void save_results_csv(const TestConfig& config, const TestResults& results,
                      const std::string& filename) {
    std::ofstream file(filename);

    file << "num_packets,payload_size,interval_ms,"
            "packets_tx,packets_detected,packets_decoded,"
            "packets_received,packets_lost,"
            "pdr,per,bit_errors,bits_compared,ber,"
            "rx_overflows,cpu_user,cpu_system\n";

    file << config.num_packets << "," << config.payload_size << "," << config.packet_interval_ms
         << "," << results.packets_transmitted << "," << results.packets_detected << ","
         << results.packets_decoded << "," << results.packets_received << ","
         << results.packets_lost << "," << results.pdr << "," << results.per << ","
         << results.bit_errors << "," << results.bits_compared << "," << results.ber << ","
         << results.rx_overflows << "," << results.cpu_user_time << "," << results.cpu_system_time
         << "\n";
}

void save_packet_log(const std::vector<PacketLog>& logs, const std::string& filename) {
    std::ofstream file(filename);

    file << "sequence,tx_time,rx_time,latency_ms,received,bit_errors\n";

    for (const auto& p : logs) {

        double latency_ms = -1.0;

        if (p.received) {
            latency_ms = (p.rx_time - p.tx_time) * 1000.0;
        }

        file << p.sequence << "," << p.tx_time << "," << p.rx_time << "," << latency_ms << ","
             << p.received << "," << p.bit_errors << "\n";
    }
}

int main() {
    TestConfig config;
    TestResults results;

    struct rusage cpu_start{};
    getrusage(RUSAGE_SELF, &cpu_start);

    std::vector<PacketLog> packet_logs(config.num_packets);

    std::cout << "========================================\n";
    std::cout << "B210 PACKET TEST\n";
    std::cout << "========================================\n";

    std::cout << "Packets:        " << config.num_packets << '\n';
    std::cout << "Payload size:   " << config.payload_size << " bytes\n";
    std::cout << "Packet interval:" << config.packet_interval_ms << " ms\n";

    // When running with 1 SDR
    // auto usrp = uhd::usrp::multi_usrp::make(TX_SERIAL);

    // usrp->set_clock_source("internal");
    // usrp->set_tx_subdev_spec(TX_SUBDEV);
    // usrp->set_rx_subdev_spec(RX_SUBDEV);

    // Transmitter tx(usrp);
    // Receiver rx(usrp);

    // When running with 2 SDRs
    Transmitter tx;
    Receiver rx;

    // Artificial payload
    std::vector<std::vector<uint8_t>> transmitted_payloads;
    transmitted_payloads.reserve(config.num_packets);
    for (std::size_t i = 0; i < config.num_packets; ++i) {
        transmitted_payloads.push_back(make_test_payload(i, config.payload_size));
    }

    // RX
    std::vector<std::complex<float>> captured_samples;
    // captured_samples.reserve(config.num_packets * config.payload_size * SAMPLES_PER_SYMBOL);
    captured_samples.reserve(settings::RX_CAPTURE_SECONDS * 2e6 * 1.1);

    rx.start();

    auto start = std::chrono::steady_clock::now();
    std::thread rx_thread([&]() {
        while (std::chrono::steady_clock::now() - start <
               std::chrono::seconds(settings::RX_CAPTURE_SECONDS)) {

            auto chunk = rx.receive(settings::RX_CHUNK_SIZE);

            captured_samples.insert(captured_samples.end(), chunk.samples.begin(),
                                    chunk.samples.end());
        }
    });

    // TX
    TxPipeline tx_pipeline;
    size_t num_sent;
    constexpr std::size_t LEAD_IN = 2400, TAIL = 400;

    for (std::size_t i = 0; i < config.num_packets; i++) {
        auto samples = tx_pipeline.generate_samples(transmitted_payloads[i]);
        // Inital and final zeros to avoid filter transients
        samples.insert(samples.begin(), LEAD_IN, std::complex<float>{0.f, 0.f});
        samples.insert(samples.end(), TAIL, std::complex<float>{0.f, 0.f});

        packet_logs[i].sequence = i;
        packet_logs[i].tx_time =
            std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch())
                .count();

        num_sent = tx.send(samples);
        ++results.packets_transmitted;

        if (num_sent != samples.size()) {
            std::cerr << "[ERROR] Sample count mismatch: expected " << samples.size() << ", sent "
                      << num_sent << '\n';
            return 1;
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(settings::TX_DELAY_MS));
    }

    std::cout << "[OK] Transmission complete\n\n";
    std::this_thread::sleep_for(std::chrono::milliseconds(500));

    // RX
    rx.stop();
    rx_thread.join();

    RxPipeline rx_pipeline;
    auto received_packets = rx_pipeline.decode_packets(captured_samples);

    // Statistics
    results.packets_decoded = received_packets.size();

    calculate_packet_statistics(received_packets, config.num_packets, results, packet_logs);
    calculate_bit_statistics(received_packets, transmitted_payloads, results, packet_logs);
    calculate_bit_statistics(rx_pipeline.rejected(), transmitted_payloads, results, packet_logs);
    results.packets_detected = received_packets.size() + rx_pipeline.rejected().size();

    struct rusage cpu_end{};
    getrusage(RUSAGE_SELF, &cpu_end);

    results.cpu_user_time = (cpu_end.ru_utime.tv_sec - cpu_start.ru_utime.tv_sec) +
                            1e-6 * (cpu_end.ru_utime.tv_usec - cpu_start.ru_utime.tv_usec);

    results.cpu_system_time = (cpu_end.ru_stime.tv_sec - cpu_start.ru_stime.tv_sec) +
                              1e-6 * (cpu_end.ru_stime.tv_usec - cpu_start.ru_stime.tv_usec);

    print_results(config, results);
    save_results_csv(config, results, "tests/outlog/tot_packets.csv");
    save_packet_log(packet_logs, "tests/outlog/ind_packets.csv");

    std::cout << "Received:\n";

    for (const auto& p : received_packets) {
        std::cout << p.header.sequence << ' ';
    }

    return 0;
}