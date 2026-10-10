#pragma once

#include <complex>
#include <cstddef>
#include <vector>

class Synchronizer {
  public:
    std::vector<std::size_t> process(const std::vector<std::complex<float>>& samples);

  private:
    std::vector<std::complex<float>> m_buffer;
    std::size_t m_absolute_start = 0;
    std::size_t m_next_candidate = 0;

    bool matches_preamble(std::size_t offset, bool inverted) const;
};