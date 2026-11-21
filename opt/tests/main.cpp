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
#include <filesystem>
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
    std::size_t num_packets = 100;
    std::size_t payload_size = 32;
    int packet_interval_ms = 10;

    std::string mode;
    bool single_sdr = false;
    std::string tx_serial;
    std::string rx_serial;

    std::string out_dir = "tests/outlog"; // where the CSV files go
    std::string label = "default";        // groups the runs of one experiment
    std::string run_id;                   // set in main()
};

struct SdrDevices {
    std::unique_ptr<Transmitter> tx;
    std::unique_ptr<Receiver> rx;
};

struct PacketResult {
    std::size_t sequence = 0;
    bool present = false; // some decoded candidate carried this sequence number
    bool valid = false;   // checksum passed
    std::size_t packet_bits = 0;
    std::size_t bit_errors = 0;
    std::size_t preamble_errors = 0;
    std::size_t seq_errors = 0;
    std::size_t length_errors = 0;
    std::size_t payload_errors = 0;
    std::size_t checksum_errors = 0;
    std::size_t rx_payload_bytes = 0;
    std::vector<std::size_t>
        error_positions; // bit index inside the packet (0 = first preamble bit)
};

struct RunInfo {
    std::string run;
    std::string label;
    std::string mode;
    std::string variant; // RX0 only / RX1 only / RX0+RX1 (MRC)
    double tx_gain = 0.0;
    double rx_gain = 0.0;
    double sample_rate = 0.0;
    double symbol_rate = 0.0;
    std::size_t num_packets = 0;
    std::size_t payload_bytes = 0;
    std::size_t detected = 0;
    std::size_t parse_failures = 0;
    std::size_t invalid_seq = 0; // decoded candidates whose sequence number was out of range
};

struct RunSummary {
    std::size_t present = 0;
    std::size_t decoded_valid = 0; // checksum passed
    std::size_t exact = 0;         // bit-exact
    std::size_t lost = 0;          // no candidate at all for this sequence number
    std::size_t bits_compared = 0;
    std::size_t bit_errors = 0;
    double pdr_valid = 0.0;
    double pdr_exact = 0.0;
    double ber = 0.0;
};

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

inline const char* field_of(std::size_t bit, std::size_t payload_bytes) {
    const std::size_t header_end = PREAMBLE_SIZE + HEADER_SIZE;
    const std::size_t payload_end = header_end + payload_bytes * 8;
    if (bit < PREAMBLE_SIZE)
        return "preamble";
    if (bit < PREAMBLE_SIZE + 16)
        return "seq";
    if (bit < header_end)
        return "length";
    if (bit < payload_end)
        return "payload";
    return "checksum";
}

inline std::ofstream open_append(const std::string& path, const std::string& header) {
    const bool fresh = !std::filesystem::exists(path) || std::filesystem::file_size(path) == 0;
    std::ofstream f(path, std::ios::app);
    if (fresh)
        f << header << '\n';
    return f;
}

inline const char* status_of(const PacketResult& r) {
    if (!r.present)
        return "lost";
    if (!r.valid)
        return "invalid";                             // checksum failed
    return r.bit_errors == 0 ? "ok" : "valid_errors"; // checksum ok but something else is wrong
}

// per_seq[s] is the result for transmitted sequence number s (present == false -> lost).
inline RunSummary write_results(const std::string& out_dir, const RunInfo& info,
                                const std::vector<PacketResult>& per_seq) {
    std::filesystem::create_directories(out_dir);

    RunSummary s;
    for (const auto& r : per_seq) {
        if (!r.present)
            continue;
        ++s.present;
        if (r.valid)
            ++s.decoded_valid;
        if (r.bit_errors == 0)
            ++s.exact;
        s.bits_compared += r.packet_bits;
        s.bit_errors += r.bit_errors;
    }
    s.lost = info.num_packets - s.present;
    s.pdr_valid = info.num_packets ? double(s.decoded_valid) / double(info.num_packets) : 0.0;
    s.pdr_exact = info.num_packets ? double(s.exact) / double(info.num_packets) : 0.0;
    s.ber = s.bits_compared ? double(s.bit_errors) / double(s.bits_compared) : 0.0;

    const std::string key = info.run + "," + info.label + "," + info.mode + "," + info.variant;
    const std::string key_cols = "run,label,mode,variant";

    {
        auto f = open_append(out_dir + "/summary.csv",
                             key_cols + ",tx_gain,rx_gain,sample_rate,symbol_rate,payload_bytes,"
                                        "num_packets,detected,present,decoded_valid,exact,lost,"
                                        "parse_failures,invalid_seq,pdr_valid,pdr_exact,"
                                        "bits_compared,bit_errors,ber");
        f << key << ',' << info.tx_gain << ',' << info.rx_gain << ',' << info.sample_rate << ','
          << info.symbol_rate << ',' << info.payload_bytes << ',' << info.num_packets << ','
          << info.detected << ',' << s.present << ',' << s.decoded_valid << ',' << s.exact << ','
          << s.lost << ',' << info.parse_failures << ',' << info.invalid_seq << ',' << s.pdr_valid
          << ',' << s.pdr_exact << ',' << s.bits_compared << ',' << s.bit_errors << ',' << s.ber
          << '\n';
    }

    {
        auto f = open_append(out_dir + "/packets.csv",
                             key_cols + ",sequence,status,valid,bit_errors,preamble_errors,"
                                        "seq_errors,length_errors,payload_errors,checksum_errors,"
                                        "packet_bits,rx_payload_bytes");
        for (std::size_t seq = 0; seq < per_seq.size(); ++seq) {
            const auto& r = per_seq[seq];
            f << key << ',' << seq << ',' << status_of(r) << ',' << (r.present && r.valid ? 1 : 0)
              << ',' << r.bit_errors << ',' << r.preamble_errors << ',' << r.seq_errors << ','
              << r.length_errors << ',' << r.payload_errors << ',' << r.checksum_errors << ','
              << r.packet_bits << ',' << r.rx_payload_bytes << '\n';
        }
    }

    {
        auto f = open_append(out_dir + "/bit_errors.csv", key_cols + ",sequence,bit_index,field");
        for (std::size_t seq = 0; seq < per_seq.size(); ++seq) {
            for (std::size_t pos : per_seq[seq].error_positions) {
                f << key << ',' << seq << ',' << pos << ',' << field_of(pos, info.payload_bytes)
                  << '\n';
            }
        }
    }

    return s;
}

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
    std::cout << "  --out <dir>             Directory for the CSV files (default tests/outlog)"
              << std::endl;
    std::cout << "  --label <text>          Name of the experiment, stored in every CSV row"
              << std::endl;
    std::cout << "  --packets <text>        Number of packets to be transmitted (default is 100)" << std::endl;
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
        } else if (arg == "--out" && i + 1 < argc) {
            options.out_dir = argv[++i];
        } else if (arg == "--label" && i + 1 < argc) {
            options.label = argv[++i];
        } else if (arg == "--packets" && i + 1 < argc) {
            options.num_packets = static_cast<size_t>(std::stoull(argv[++i]));
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

inline PacketResult compare_packets(const PacketStruct& tx, const PacketStruct& rx) {
    Packet builder;
    const auto tx_bits = builder.packet_to_bits(tx);
    const auto rx_bits = builder.packet_to_bits(rx);

    PacketResult r;
    r.sequence = tx.header.sequence;
    r.present = true;
    r.valid = rx.valid;
    r.packet_bits = tx_bits.size();
    r.rx_payload_bytes = rx.payload.size();

    const std::size_t seq_end = PREAMBLE_SIZE + 16;
    const std::size_t header_end = PREAMBLE_SIZE + HEADER_SIZE;
    const std::size_t payload_end = header_end + tx.payload.size() * 8;

    for (std::size_t i = 0; i < tx_bits.size(); ++i) {
        const bool wrong = (i >= rx_bits.size()) || (rx_bits[i] != tx_bits[i]);
        if (!wrong)
            continue;

        ++r.bit_errors;
        r.error_positions.push_back(i);

        if (i < PREAMBLE_SIZE)
            ++r.preamble_errors;
        else if (i < seq_end)
            ++r.seq_errors;
        else if (i < header_end)
            ++r.length_errors;
        else if (i < payload_end)
            ++r.payload_errors;
        else
            ++r.checksum_errors;
    }

    return r;
}

// Several candidates can carry the same sequence number (duplicate detections, or a corrupted
// sequence field). Keep the best one: checksum-valid first, then fewest bit errors.
inline bool better(const PacketResult& a, const PacketResult& b) {
    if (a.valid != b.valid)
        return a.valid;
    return a.bit_errors < b.bit_errors;
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

    const auto& rx_stats = rx_pipeline.stats();

    std::cout << name << ": decoded " << received_packets.size() << " / " << config.num_packets
              << ", parser failures " << rx_stats.parse_failures << std::endl;

    std::vector<PacketResult> per_seq(config.num_packets);
    for (std::size_t s = 0; s < per_seq.size(); ++s) {
        per_seq[s].sequence = s;
    }

    Packet builder;
    int invalid_seq = 0;

    for (const auto& packet : all_packets) {
        const std::size_t sequence = packet.header.sequence;

        if (sequence >= transmitted_payloads.size() || sequence >= per_seq.size()) {
            ++invalid_seq;
            continue;
        }

        const auto original =
            builder.build(transmitted_payloads[sequence], 0, static_cast<uint16_t>(sequence));
        PacketResult r = compare_packets(original, packet);
        if (r.bit_errors != 0) {
            std::cout << " -> MISMATCH for packet " << sequence << ", valid: " << packet.valid
                      << "\n total bit errors: " << r.bit_errors << " of " << r.packet_bits
                      << "\n preamble: " << r.preamble_errors << ", seq: " << r.seq_errors
                      << ", length: " << r.length_errors << ", payload: " << r.payload_errors
                      << ", checksum: " << r.checksum_errors << std::endl;
        }

        if (!per_seq[sequence].present || better(r, per_seq[sequence]))
            per_seq[sequence] = std::move(r);
    }

    RunInfo info;
    info.run = config.run_id;
    info.label = config.label;
    info.mode = config.mode;
    info.variant = name;
    info.tx_gain = TX_GAIN;
    info.rx_gain = RX_GAIN;
    info.sample_rate = SAMPLE_FREQ;
    info.symbol_rate = SYMBOL_RATE;
    info.num_packets = config.num_packets;
    info.payload_bytes = config.payload_size;
    info.detected = rx_stats.preambles_detected;
    info.parse_failures = rx_stats.parse_failures;
    info.invalid_seq = invalid_seq;

    const RunSummary s = write_results(config.out_dir, info, per_seq);

    if (config.mode == "siso" || (use0 && use1)) {
        std::cout << "\n========================================\n";
        std::cout << "TEST RESULTS\n";
        std::cout << "========================================\n";

        std::cout << "Packets transmitted:                  " << config.num_packets << std::endl;
        std::cout << "Detected preambles:                   " << rx_stats.preambles_detected
                  << std::endl;
        std::cout << "Successfully decoded packets:         " << s.decoded_valid << std::endl;
        std::cout << "Parser failures:                      " << rx_stats.parse_failures
                  << std::endl;
        std::cout << "Packets lost:                         " << s.lost << std::endl;
        std::cout << "Packets with invalid sequence:        " << invalid_seq << std::endl;
        std::cout << "Duplicate sequence (nothing decoded): "
                  << rx_stats.preambles_detected - s.present - invalid_seq << std::endl;

        std::cout << "\nPDR (checksum ok): " << s.pdr_valid * 100.0 << " %" << std::endl;
        std::cout << "PDR (bit-exact):   " << s.pdr_exact * 100.0 << " %" << std::endl;

        std::cout << "\nBits compared: " << s.bits_compared << std::endl;
        std::cout << "Bit errors:    " << s.bit_errors << std::endl;
        std::cout << "BER:           " << s.ber * 100.0 << " %" << std::endl;
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

    config.run_id = std::to_string(std::time(nullptr));

    std::string mode = config.mode;
    std::string tx_serial = config.tx_serial;
    std::string rx_serial = config.rx_serial;

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
    constexpr std::size_t LEAD_IN = 2400, TAIL = 400;
    const double per_packet_s = (LEAD_IN + TAIL) / double(SAMPLE_FREQ) +
                                (MIN_PACKET_SIZE + config.payload_size * 8) / double(SYMBOL_RATE) +
                                settings::TX_DELAY_MS / 1000.0;
    const double capture_s = config.num_packets * per_packet_s * 1.7 + 2.0; // margin
    const auto capture_duration = std::chrono::duration<double>(capture_s);

    std::vector<std::vector<std::complex<float>>> captured_samples(num_rx_channels);
    for (auto& channel : captured_samples) {
        channel.reserve(static_cast<std::size_t>(capture_s * SAMPLE_FREQ * 1.2));
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
    TxPipeline tx_pipeline;
    size_t num_sent;
    auto t0 = std::chrono::steady_clock::now();

    for (std::size_t i = 0; i < config.num_packets; i++) {
        auto samples = tx_pipeline.generate_samples(transmitted_payloads[i]);
        // Inital and final zeros to avoid filter transients
        samples.insert(samples.begin(), LEAD_IN, std::complex<float>{0.f, 0.f});
        samples.insert(samples.end(), TAIL, std::complex<float>{0.f, 0.f});

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

    double tx_s = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
    std::cout << "TX loop took " << tx_s << " s\n";

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
        // evaluate("RX0 only", true, false, config, transmitted_payloads, captured_samples);
        // evaluate("RX1 only", false, true, config, transmitted_payloads, captured_samples);
        evaluate("RX0+RX1 (MRC)", true, true, config, transmitted_payloads, captured_samples);
    } else if (mode == "loopback" || mode == "loopback-noise") {
        evaluate(mode.data(), true, true, config, transmitted_payloads, captured_samples);
    }

    return 0;
}