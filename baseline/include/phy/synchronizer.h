#pragma once

#include <complex>
#include <cstddef>
#include <vector>

struct Detection {
    std::size_t offset;
    std::size_t phase;
    bool inverted;
    // std::complex<float> rot;
};

class Synchronizer {
  public:
    std::vector<Detection> process(const std::vector<std::complex<float>>& samples);
    void reset();
    // bool last_detection_inverted() const {
    //     return m_last_inverted;
    // }

    // std::size_t m_best_phase;

  private:
    std::vector<std::complex<float>> m_buffer;
    std::size_t m_absolute_start = 0;
    std::size_t m_next_candidate = 0;
    std::size_t m_last_detection = static_cast<std::size_t>(-1);
    // std::vector<std::size_t> m_detections;

    bool matches_preamble(std::size_t offset, bool inverted, std::size_t& errors) const;

    std::size_t preamble_errors(std::size_t offset, bool inverted) const;
    double preamble_score(std::size_t offset) const;
    float preamble_amp(std::size_t offset) const;

    // bool m_last_inverted = false;
};
