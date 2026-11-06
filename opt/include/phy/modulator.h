#pragma once

#include <complex>
#include <cstddef>
#include <cstdint>
#include <vector>

class Modulator {
  public:
    std::vector<std::complex<float>> bpsk_modulate(const std::vector<uint8_t>& bits,
                                                   std::size_t samples_per_symbol);

  private:
};

class Demodulator {
  public:
    std::vector<uint8_t> bpsk_demodulate(const std::vector<std::complex<float>>& samples,
                                         std::size_t samples_per_symbol,
                                         std::size_t sampling_offset = 0) const;
};
