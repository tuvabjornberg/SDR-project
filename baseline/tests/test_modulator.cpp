#include <cassert>
#include <cmath>
#include <cstdint>
#include <iostream>
#include <vector>

#include "phy/modulator.h"

int main()
{
    constexpr std::size_t SAMPLES_PER_SYMBOL = 4;

    const std::vector<uint8_t> bits = {1, 0, 1};

    Modulator modulator;
    const auto samples = modulator.bpsk_modulate(bits, SAMPLES_PER_SYMBOL);

    // 3 symbols × 4 samples/symbol
    assert(samples.size() == 12);

    for (std::size_t i = 0; i < samples.size(); ++i)
    {
        const std::size_t symbol_index = i / SAMPLES_PER_SYMBOL;

        const float expected = bits[symbol_index] ? 1.0f : -1.0f;

        assert(std::abs(samples[i].real() - expected) < 1e-6f);

        assert(std::abs(samples[i].imag()) < 1e-6f);
    }

    std::cout << "BPSK modulator test PASSED\n";

    return 0;
}
