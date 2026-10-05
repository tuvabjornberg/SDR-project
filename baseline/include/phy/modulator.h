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
