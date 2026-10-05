#pragma once

#include <complex>
#include <vector>

class RRCFilter {
  public:
    std::vector<float> root_raised_cosine(double gain, double sampling_freq, double symbol_rate,
                                          double alpha, int ntaps);

    std::vector<std::complex<float>> filter(const std::vector<std::complex<float>>& input,
                                            const std::vector<float>& taps);

  private:
    std::vector<float> taps;
    int samples_per_symbol_;
};