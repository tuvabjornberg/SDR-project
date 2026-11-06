#include "phy/modulator.h"

#include <complex>
#include <cstdint>
#include <stdexcept>
#include <vector>

// Complex because of B210
// std::vector<std::complex<float>> Modulator::bpsk_modulate(const std::vector<uint8_t>& bits,
//                                                           std::size_t samples_per_symbol) {
//     std::vector<std::complex<float>> samples;

//     samples.reserve(bits.size() * samples_per_symbol);

//     for (uint8_t bit : bits) {
//         const float symbol = bit ? 1.0f : -1.0f;

//         for (std::size_t i = 0; i < samples_per_symbol; ++i) {
//             samples.emplace_back(symbol, 0.0f);
//         }
//     }

//     return samples;
// }
// std::vector<std::complex<float>> Modulator::bpsk_modulate(const std::vector<uint8_t>& bits,
//                                                           std::size_t samples_per_symbol) {
//     std::vector<std::complex<float>> samples(bits.size() * samples_per_symbol,
//                                              std::complex<float>{0.0f, 0.0f});

//     for (std::size_t i = 0; i < bits.size(); ++i) {
//         samples[i * samples_per_symbol] = {bits[i] ? 1.0f : -1.0f, 0.0f};
//     }

//     return samples;
// }

std::vector<std::complex<float>> Modulator::bpsk_modulate(const std::vector<uint8_t>& bits,
                                                          std::size_t samples_per_symbol) {
    std::vector<std::complex<float>> samples(bits.size() * samples_per_symbol,
                                             std::complex<float>{0.0f, 0.0f});

    for (std::size_t i = 0; i < bits.size(); ++i) {
        const float symbol = bits[i] ? 1.0f : -1.0f;

        for (std::size_t j = 0; j < samples_per_symbol; ++j) {
            samples[i * samples_per_symbol + j] = {symbol, 0.0f};
        }
    }
    return samples;
}

std::vector<uint8_t> Demodulator::bpsk_demodulate(const std::vector<std::complex<float>>& samples,
                                                  std::size_t samples_per_symbol,
                                                  std::size_t sampling_offset) const {
    if (samples_per_symbol == 0) {
        throw std::invalid_argument("samples_per_symbol must be greater than zero");
    }

    if (sampling_offset >= samples_per_symbol) {
        throw std::invalid_argument("sampling_offset must be less than samples_per_symbol");
    }

    std::vector<uint8_t> bits;

    for (std::size_t i = sampling_offset; i < samples.size(); i += samples_per_symbol) {

        bits.push_back(samples[i].real() >= 0.0f ? 1 : 0);
    }

    return bits;
}
