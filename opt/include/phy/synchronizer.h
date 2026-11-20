#pragma once

#include <complex>
#include <cstddef>
#include <vector>

struct PreambleHit {
    double metric;                       // 0..1
    double cfo_hz;                       // common to both RX channels (one LO)
    std::vector<std::complex<double>> g; // per-channel complex gain, phase at preamble start
};

struct Detection {
    std::size_t offset;
    double cfo_hz;
    std::vector<std::complex<double>> g;
    double metric;
};

class Synchronizer {
  public:
    Synchronizer(std::size_t num_rx_channels) : m_buffer(num_rx_channels) {};

    std::vector<Detection> process(const std::vector<std::vector<std::complex<float>>>& samples);

    void reset();

  private:
    std::vector<std::vector<std::complex<float>>> m_buffer;
    std::size_t m_absolute_start = 0;
    std::size_t m_next_candidate = 0;

    PreambleHit correlate(std::size_t off) const;
};
