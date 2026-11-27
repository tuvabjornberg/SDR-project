#pragma once
// #include "common/config.h"
#include "common/jpeg.h"
#include "common/settings.h"
#include "mac/packet.h"
#include "pipeline/rx_pipeline.h"
#include "radio/receiver.h"
#include "radio/transmitter.h"

#include <algorithm>
#include <cmath>
#include <complex>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <sys/resource.h>
#include <unordered_map>
#include <vector>

struct PacketLog {
    uint32_t sequence = 0;
    double tx_time = 0.0;
    double rx_time = 0.0;
    bool received = false;
    std::size_t bit_errors = 0;
};

struct TestConfig {
    std::size_t num_packets = 0;
    std::size_t payload_size = 32;
    int packet_interval_ms = 10;

    std::string mode;
    std::size_t tx_channels;
    std::size_t rx_channels;
    bool single_sdr = false;
    std::string tx_serial;
    std::string rx_serial;
    double capture_s = 0.0;

    std::string out_dir = "tests/outlog"; // where the CSV files go
    std::string label = "default";        // groups the runs of one experiment
    std::string run_id;                   // set in main()
    bool record = false;
    std::string replay_file;

    std::size_t LEAD_IN = 2400;
    std::size_t TAIL = 400;

    std::string input_file = "";
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
    PipelineTimings timings;
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

// Artificial payload
// Counter that wraps after 255
inline std::vector<uint8_t> make_test_payload(std::size_t sequence, std::size_t payload_size) {
    std::vector<uint8_t> payload(payload_size);

    for (std::size_t i = 0; i < payload_size; ++i) {
        payload[i] = static_cast<uint8_t>((sequence + i) & 0xFF);
    }

    return payload;
}

inline std::size_t bit_cnt(uint32_t value) {
    std::size_t count = 0;

    while (value != 0) {
        count += value & 1U;
        value >>= 1;
    }

    return count;
}

inline std::vector<uint8_t>
reconstruct_bytes(const std::vector<std::vector<std::complex<float>>>& captured_samples) {
    std::vector<uint8_t> data;

    for (const auto& packet : captured_samples) {
        for (const auto& sample : packet) {
            uint8_t byte_I = static_cast<uint8_t>(std::round(sample.real()));
            uint8_t byte_Q = static_cast<uint8_t>(std::round(sample.imag()));

            data.push_back(byte_I);
            data.push_back(byte_Q);
        }
    }

    return data;
}

struct Reconstruction {
    std::vector<uint8_t> data;
    std::vector<uint8_t> lost; // 1 = byte never received
};

inline Reconstruction
reconstruct_file(const std::vector<PacketStruct>& received_packets,
                 const std::vector<std::vector<uint8_t>>& transmitted_payloads) {
    std::vector<std::size_t> start(transmitted_payloads.size() + 1, 0);
    for (std::size_t i = 0; i < transmitted_payloads.size(); ++i)
        start[i + 1] = start[i] + transmitted_payloads[i].size();

    Reconstruction r;
    r.data.assign(start.back(), 0x00);
    r.lost.assign(start.back(), 1);

    for (const auto& packet : received_packets) {
        if (!packet.valid)
            continue;
        const std::size_t seq = packet.header.sequence;
        if (seq >= transmitted_payloads.size())
            continue;

        const std::size_t n = std::min(packet.payload.size(), transmitted_payloads[seq].size());
        std::copy_n(packet.payload.begin(), n, r.data.begin() + start[seq]);
        std::fill_n(r.lost.begin() + start[seq], n, uint8_t{0});
    }
    return r;
}

inline std::vector<uint8_t> flatten(const std::vector<std::vector<uint8_t>>& p) {
    std::vector<uint8_t> out;
    for (const auto& v : p)
        out.insert(out.end(), v.begin(), v.end());
    return out;
}

inline void save_jpeg(const std::vector<uint8_t>& data, const std::string& filename,
                      const std::string& out_dir) {
    std::filesystem::create_directories(out_dir);
    std::ofstream file(filename + ".jpg", std::ios::binary);

    if (!file.is_open()) {
        throw std::runtime_error("Could not create output file");
    }

    file.write(reinterpret_cast<const char*>(data.data()), data.size());
    file.close();
}

inline void save_metadata(const TestConfig& config, const std::string& recording_dir) {
    std::ofstream file(recording_dir + "/metadata.csv");

    if (!file)
        throw std::runtime_error("Could not create metadata.csv");

    file << "key,value\n";

    file << "label," << config.label << '\n';
    file << "run_id," << config.run_id << '\n';
    file << "mode," << config.mode << '\n';

    file << "tx_channels," << config.tx_channels << '\n';
    file << "rx_channels," << config.rx_channels << '\n';
    file << "single_sdr," << config.single_sdr << '\n';

    file << "tx_serial," << config.tx_serial << '\n';
    file << "rx_serial," << config.rx_serial << '\n';

    file << "num_packets," << config.num_packets << '\n';
    file << "payload_size," << config.payload_size << '\n';
    file << "packet_interval_ms," << config.packet_interval_ms << '\n';

    file << "sample_rate," << SAMPLE_FREQ << '\n';
    file << "symbol_rate," << SYMBOL_RATE << '\n';

    file << "tx_gain," << TX_GAIN << '\n';
    file << "rx_gain," << RX_GAIN << '\n';

    file << "capture_s," << config.capture_s << '\n';
    file << "lead_in," << config.LEAD_IN << '\n';
    file << "tail," << config.TAIL << '\n';

    if (!file)
        throw std::runtime_error("Error writing metadata.csv");
}

inline TestConfig load_metadata(const std::string& recording_dir) {
    std::ifstream file(recording_dir + "/metadata.csv");

    if (!file)
        throw std::runtime_error("Could not open metadata.csv");

    std::unordered_map<std::string, std::string> values;

    std::string line;
    std::getline(file, line); // Skip CSV header

    while (std::getline(file, line)) {
        const auto separator = line.find(',');

        if (separator == std::string::npos)
            continue;

        std::string key = line.substr(0, separator);
        std::string value = line.substr(separator + 1);

        values[key] = value;
    }

    auto get = [&](const std::string& key) -> std::string {
        auto it = values.find(key);

        if (it == values.end())
            throw std::runtime_error("Missing metadata field: " + key);

        return it->second;
    };

    TestConfig config;

    config.label = get("label");
    config.run_id = get("run_id");
    config.mode = get("mode");

    config.tx_channels = std::stoull(get("tx_channels"));
    config.rx_channels = std::stoull(get("rx_channels"));
    config.single_sdr = std::stoi(get("single_sdr")) != 0;

    config.tx_serial = get("tx_serial");
    config.rx_serial = get("rx_serial");

    config.num_packets = std::stoull(get("num_packets"));
    config.payload_size = std::stoull(get("payload_size"));
    config.packet_interval_ms = std::stoi(get("packet_interval_ms"));

    // config.capture_s = std::stod(get("capture_s"));
    config.LEAD_IN = std::stoull(get("lead_in"));
    config.TAIL = std::stoull(get("tail"));

    return config;
}

// ----------------------- Write to csv ------------------------
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
                                        "bits_compared,bit_errors,ber,"
                                        "cfo_correction_ms,rrc_filter_ms,synchronization_ms,"
                                        "demodulation_ms,parsing_ms,total_ms");
        f << key << ',' << TX_GAIN << ',' << RX_GAIN << ',' << info.sample_rate << ','
          << info.symbol_rate << ',' << info.payload_bytes << ',' << info.num_packets << ','
          << info.detected << ',' << s.present << ',' << s.decoded_valid << ',' << s.exact << ','
          << s.lost << ',' << info.parse_failures << ',' << info.invalid_seq << ',' << s.pdr_valid
          << ',' << s.pdr_exact << ',' << s.bits_compared << ',' << s.bit_errors << ',' << s.ber
          << ',' << info.timings.cfo_correction_ms << ',' << info.timings.rrc_filter_ms << ','
          << info.timings.synchronization_ms << ',' << info.timings.demodulation_ms << ','
          << info.timings.parsing_ms << ',' << info.timings.total_ms << '\n';
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

// ---------------------- Evaluate ------------------------
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

inline void evaluate(const char* name, bool use0, bool use1, const TestConfig& config,
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

    std::cout << "Decoding packets started" << std::endl;
    auto received_packets = rx_pipeline.decode_packets(in);
    auto rejected_packets = rx_pipeline.rejected();
    std::cout << "Finished decoding, starting comparison" << std::endl;

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
    info.timings.cfo_correction_ms = rx_stats.timings.cfo_correction_ms;
    info.timings.rrc_filter_ms = rx_stats.timings.rrc_filter_ms;
    info.timings.synchronization_ms = rx_stats.timings.synchronization_ms;
    info.timings.demodulation_ms = rx_stats.timings.demodulation_ms;
    info.timings.parsing_ms = rx_stats.timings.parsing_ms;
    info.timings.total_ms = rx_stats.timings.total_ms;

    std::string output_dir = config.record ? "tests/recordings/" + config.label : config.out_dir;
    const RunSummary s = write_results(output_dir, info, per_seq);

    if (config.mode == "siso" || (use0 && use1)) { // SISO or MRC
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

        std::cout << "\nTime for CFO correction:   " << info.timings.cfo_correction_ms << " ms"
                  << std::endl;
        std::cout << "Time for filter:             " << info.timings.rrc_filter_ms << " ms"
                  << std::endl;
        std::cout << "Time for synchronization:    " << info.timings.synchronization_ms << " ms"
                  << std::endl;
        std::cout << "Time for demodulation:       " << info.timings.demodulation_ms << " ms"
                  << std::endl;
        std::cout << "Time for parsing:            " << info.timings.parsing_ms << " ms"
                  << std::endl;
        std::cout << "Total time for decoding:     " << info.timings.total_ms << " ms" << std::endl;

        std::string extension = std::filesystem::path(config.input_file).extension().string();
        if (extension == ".jpg" || extension == ".jpeg") {
            auto rec = reconstruct_file(received_packets, transmitted_payloads);

            std::string filename = std::filesystem::path(config.input_file).stem().string();
            std::string output_path = output_dir + "/" + filename + "_reconstructed";

            save_jpeg(rec.data, output_path, output_dir); // raw, zero-filled (for comparison)

            const std::size_t lost_bytes = std::count(rec.lost.begin(), rec.lost.end(), uint8_t{1});
            std::cout << "Lost bytes: " << lost_bytes << " / " << rec.data.size() << "\n";

            try {
                auto damaged =
                    jpegres::find_damaged_intervals(flatten(transmitted_payloads), rec.lost);
                auto img = jpegres::decode_and_mark(rec.data, damaged);
                save_ppm(img, output_path + "_marked.ppm");
                std::cout << "Marked image saved to: " << output_path << "_marked.png\n";
            } catch (const std::exception& e) {
                std::cerr << "Could not decode image (header packets lost?): " << e.what() << "\n";
            }
        }
    }
};

enum class Node { TX, RX, TXRX };

inline void print_option(const std::string& option, const std::string& description) {
    std::cout << "  " << std::left << std::setw(25) << option << description << '\n';
}

inline void print_options(Node node) {
    switch (node) {
    case Node::TX:
        std::cout << "Usage: ./transmitter --tx-serial <TX_SERIAL> "
                     "--tx-channels <number> --input <number/filepath>\n";
        break;

    case Node::RX:
        std::cout
            << "Usage: ./receiver --rx-serial <RX_SERIAL> "
               "--rx-channels <number> --tx_channels <1 or 2> --label <text> --input <number>\n";
        break;

    case Node::TXRX:
        std::cout << "Usage normal run: ./txrx --tx-serial <TX_SERIAL> --tx_channels <1 or 2>"
                     "--rx-serial <RX_SERIAL> --rx_channels <1 or 2> --label <text> "
                     "--input <number/filepath>"
                  << std::endl;
        std::cout << "Usage recording run: ./txrx --tx-serial <TX_SERIAL> --tx_channels <1 or 2>"
                     "--rx-serial <RX_SERIAL> --rx_channels <1 or 2> --label <text> "
                     "--input <number/filepath> --record"
                  << std::endl;
        std::cout << "Usage replay run: ./txrx --replay <filepath> " << std::endl;
        break;
    }

    std::cout << "\nOptions:\n";

    switch (node) {
    case Node::TX:
        print_option("--tx-serial <SERIAL>", "Specify the serial number of the TX SDR");
        print_option(
            "--tx-channels <number>",
            "Number of TX channels, 1 channel = TX/RX A and 2 channels = TX/RX A & TX/RX B");
        print_option("--input <filepath or number of packets>",
                     "The input file to transmit or the number of packets sent from the synthetic "
                     "counter data)");
        break;

    case Node::RX:
        print_option("--rx-serial <SERIAL>", "Specify the serial number of the RX SDR");
        print_option("--rx-channels <number>",
                     "Number of RX channels, 1 channel = RX2 A and 2 channels = RX2 A & RX2 B");
        print_option("--tx-channels <number>",
                     "Number of TX channels (only for logging siso, simo, mimo)");
        print_option("--label <text>", "Experiment name stored in every CSV row");
        // print_option("--packets <number>", "Number of packets for synthetic input"); //TODO: RX
        // needs packets to know how long it should listen for
        break;

    case Node::TXRX:
        print_option("--loopback", "Run a loopback test without SDR hardware");
        print_option("--loopback-noise", "Run loopback test with added noise");
        print_option("--replay <filepath>",
                     "Run a replay test of recorded IQ-samples, without SDR hardware");
        print_option("--record", "Record raw IQ-samples");
        print_option("--payload-size", "Payload size of each packet (default: 32 bytes)");
        print_option("--tx-serial <SERIAL>", "Specify the serial number of the TX SDR");
        print_option("--rx-serial <SERIAL>", "Specify the serial number of the RX SDR");
        print_option(
            "--tx-channels <number>",
            "Number of TX channels, 1 channel = TX/RX A and 2 channels = TX/RX A & TX/RX B");
        print_option("--rx-channels <number>",
                     "Number of RX channels, 1 channel = RX2 A and 2 channels = RX2 A & RX2 B");
        print_option("--label <text>", "Experiment name stored in every CSV row");
        print_option("--input <filepath or number of packets>",
                     "The input file to transmit or the number of packets sent from the synthetic "
                     "counter data)");
        print_option("--output <filepath>", "CSV output directory (default: tests/outlog)");
        break;
    }

    std::cout << '\n';
}

inline bool parse_arguments(int argc, char** argv, TestConfig& options, Node node) {
    if (argc < 2) {
        std::cerr << "Error: arguments missing" << std::endl;
        print_options(node);
        return false;
    }

    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];

        if (arg == "--loopback") {
            options.mode = "loopback";
        } else if (arg == "--loopback-noise") {
            options.mode = "loopback-noise";
        } else if (arg == "--replay" && i + 1 < argc) {
            options.replay_file = argv[++i];
        } else if (arg == "--record") {
            options.record = true;
        } else if (arg == "--payload-size" && i + 1 < argc) {
            options.payload_size = static_cast<size_t>(std::stoull(argv[++i]));
        } else if (arg == "--tx-serial" && i + 1 < argc) {
            options.tx_serial = argv[++i];
        } else if (arg == "--tx-channels" && i + 1 < argc) {
            options.tx_channels = static_cast<size_t>(std::stoull(argv[++i]));
        } else if (arg == "--rx-serial" && i + 1 < argc) {
            options.rx_serial = argv[++i];
        } else if (arg == "--rx-channels" && i + 1 < argc) {
            options.rx_channels = static_cast<size_t>(std::stoull(argv[++i]));
        } else if (arg == "--label" && i + 1 < argc) {
            options.label = argv[++i];
        } else if (arg == "--input" && i + 1 < argc) {
            std::string input = argv[++i];
            bool is_number =
                !input.empty() && std::all_of(input.begin(), input.end(),
                                              [](unsigned char c) { return std::isdigit(c); });

            if (is_number) {
                options.num_packets = std::stoull(input);
                if (options.num_packets == 0) {
                    std::cerr << "Number of packets must be greater than zero\n";
                    return false;
                }

            } else {
                options.input_file = input;
                std::string extension = std::filesystem::path(input).extension().string();
                if (extension != ".txt" && extension != ".jpeg" && extension != ".jpg") {
                    std::cerr
                        << "Unsupported input file format, must be either .txt or .jpg/.jpeg\n";
                    return false;
                }
            }
        } else if (arg == "--output" && i + 1 < argc) {
            options.out_dir = argv[++i];
        } else {
            std::cerr << "Unknown or incomplete argument: " << arg << '\n';
            print_options(node);
            return false;
        }
    }

    if (options.mode != "loopback" && options.mode != "loopback-noise" &&
        options.replay_file.empty()) {
        if (options.tx_serial == options.rx_serial) {
            options.single_sdr = true;
        } else {
            options.single_sdr = false;
        }

        options.mode = options.tx_channels == 1 ? (options.rx_channels == 1 ? "siso" : "simo")
                                                : (options.rx_channels == 1 ? "miso" : "mimo");
    }

    options.run_id = std::to_string(std::time(nullptr));

    return true;
}

inline double capture_time(TestConfig& config) {
    const double per_packet_s = (config.LEAD_IN + config.TAIL) / double(SAMPLE_FREQ) +
                                (MIN_PACKET_SIZE + config.payload_size * 8) / double(SYMBOL_RATE) +
                                settings::TX_DELAY_MS / 1000.0;
    std::cout << "Capture time: " << config.num_packets * per_packet_s * 2.0 + 2.0 << " s"
              << std::endl;
    return config.num_packets * per_packet_s * 1.6 + 2.0; // margin
}