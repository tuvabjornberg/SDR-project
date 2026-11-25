#pragma once

#include "common/config.h"

#include <array>
#include <complex>
#include <cstddef>
#include <vector>

constexpr std::size_t MAX_RX_CHANNELS = 2;

struct PreambleHit {
    double metric; // 0..1
    double cfo_hz; // common to both RX channels (one LO)
    std::array<std::complex<float>, MAX_RX_CHANNELS>
        g{}; // per-channel complex gain, phase at preamble start
};

struct Detection {
    std::size_t offset;
    double cfo_hz;
    std::array<std::complex<float>, MAX_RX_CHANNELS> g{};
    double metric;
};

struct EnergyState {
    float energy = 0.0f;
    std::size_t offset = 0;
    bool initialized = false;
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
    std::array<EnergyState, SAMPLES_PER_SYMBOL> m_energy_states{};

    PreambleHit correlate(std::size_t off);
    float calculate_energy(std::size_t off);
};
