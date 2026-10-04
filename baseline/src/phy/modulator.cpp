#include <cstdint>
#include <vector>
#include <complex>

#include "phy/modulator.h"

// Complex because of B210
std::vector<std::complex<float>> Modulator::bpsk_modulate(const std::vector<uint8_t> &bits, std::size_t samples_per_symbol)
{
    std::vector<std::complex<float>> samples;

    samples.reserve(bits.size() * samples_per_symbol);

    for (uint8_t bit : bits)
    {
        const float symbol = bit ? 1.0f : -1.0f;

        for (std::size_t i = 0; i < samples_per_symbol; ++i)
        {
            samples.emplace_back(symbol, 0.0f);
        }
    }

    return samples;
}
