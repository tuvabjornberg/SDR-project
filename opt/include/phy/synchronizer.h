#pragma once

#include <array>
#include <complex>
#include <cstddef>
#include <vector>


struct PreambleHit {
    double metric;                         // 0..1
    double cfo_hz;                         // common to both RX channels (one LO)
    std::array<std::complex<double>, 2> g; // per-channel complex gain, phase at preamble start
};

struct Detection {
    std::size_t offset;
    double cfo_hz;
    std::array<std::complex<double>, 2> g;
    double metric;
};

class Synchronizer {
  public:
    using Channels = std::array<std::vector<std::complex<float>>, 2>;

    std::vector<Detection> process(const Channels& samples);

    void reset();
    // bool last_detection_inverted() const {
    //     return m_last_inverted;
    // }

    // std::size_t m_best_phase;

  private:
    // std::vector<std::complex<float>> m_buffer;
    Channels m_buffer;
    std::size_t m_absolute_start = 0;
    std::size_t m_next_candidate = 0;
    std::size_t m_last_detection = static_cast<std::size_t>(-1);
    // std::vector<std::size_t> m_detections;

    bool matches_preamble(std::size_t offset, bool inverted, std::size_t& errors) const;

    std::size_t preamble_errors(std::size_t offset, bool inverted) const;
    double preamble_score(std::size_t offset) const;
    float preamble_amp(std::size_t offset) const;

    PreambleHit correlate(std::size_t off) const;

    // bool m_last_inverted = false;
};
