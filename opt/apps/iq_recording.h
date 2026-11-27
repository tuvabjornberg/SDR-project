
#pragma once

#include <algorithm>
#include <complex>
#include <cstdint>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

namespace iq_recording {

inline void save_channel(const std::vector<std::complex<float>>& samples,
                         const std::string& filename) {
    std::ofstream file(filename, std::ios::binary);

    if (!file)
        throw std::runtime_error("Cannot create IQ file: " + filename);

    // Magic identifier and version
    const char magic[8] = {'S', 'D', 'R', 'I', 'Q', '0', '1', '\0'};
    file.write(magic, sizeof(magic));

    const uint64_t count = samples.size();
    file.write(reinterpret_cast<const char*>(&count), sizeof(count));

    for (const auto& sample : samples) {
        const float real = sample.real();
        const float imag = sample.imag();

        file.write(reinterpret_cast<const char*>(&real), sizeof(real));
        file.write(reinterpret_cast<const char*>(&imag), sizeof(imag));
    }

    if (!file)
        throw std::runtime_error("Error writing IQ file: " + filename);
}

inline uint64_t file_size_limit() {
    return 500000000ULL; // Maximum 500 million samples
}

inline std::vector<std::complex<float>> load_channel(const std::string& filename) {
    std::ifstream file(filename, std::ios::binary);

    if (!file)
        throw std::runtime_error("Cannot open IQ file: " + filename);

    char magic[8];
    file.read(magic, sizeof(magic));

    const char expected[8] = {'S', 'D', 'R', 'I', 'Q', '0', '1', '\0'};

    if (!file || !std::equal(magic, magic + 8, expected))
        throw std::runtime_error("Invalid IQ file: " + filename);

    uint64_t count = 0;
    file.read(reinterpret_cast<char*>(&count), sizeof(count));

    if (!file || count > file_size_limit())
        throw std::runtime_error("Invalid IQ sample count");

    std::vector<std::complex<float>> samples;
    samples.reserve(static_cast<std::size_t>(count));

    for (uint64_t i = 0; i < count; ++i) {
        float real, imag;

        file.read(reinterpret_cast<char*>(&real), sizeof(real));
        file.read(reinterpret_cast<char*>(&imag), sizeof(imag));

        if (!file)
            throw std::runtime_error("Truncated IQ file: " + filename);

        samples.emplace_back(real, imag);
    }

    return samples;
}

inline void save_payloads(const std::vector<std::vector<uint8_t>>& payloads,
                          const std::string& filename) {
    std::ofstream file(filename, std::ios::binary);

    if (!file)
        throw std::runtime_error("Cannot create payload file");

    uint64_t count = payloads.size();
    file.write(reinterpret_cast<const char*>(&count), sizeof(count));

    for (const auto& payload : payloads) {
        uint64_t size = payload.size();

        file.write(reinterpret_cast<const char*>(&size), sizeof(size));
        file.write(reinterpret_cast<const char*>(payload.data()), size);
    }

    if (!file)
        throw std::runtime_error("Error writing payload file");
}

inline std::vector<std::vector<uint8_t>> load_transmitted_payloads(const std::string& filename) {
    std::ifstream file(filename, std::ios::binary);

    if (!file)
        throw std::runtime_error("Cannot open payload file");

    uint64_t count = 0;
    file.read(reinterpret_cast<char*>(&count), sizeof(count));

    if (!file || count > 1000000)
        throw std::runtime_error("Invalid payload count");

    std::vector<std::vector<uint8_t>> payloads;

    for (uint64_t i = 0; i < count; ++i) {
        uint64_t size = 0;

        file.read(reinterpret_cast<char*>(&size), sizeof(size));

        if (!file || size > 100000000)
            throw std::runtime_error("Invalid payload size");

        std::vector<uint8_t> payload(size);

        file.read(reinterpret_cast<char*>(payload.data()), size);

        if (!file)
            throw std::runtime_error("Truncated payload file");

        payloads.push_back(std::move(payload));
    }

    return payloads;
}

} // namespace iq_recording
