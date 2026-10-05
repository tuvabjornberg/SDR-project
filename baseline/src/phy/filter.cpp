#include "phy/filter.h"

#include <cmath>

#define GR_M_PI 3.14159265358979323846 /* pi */

// https://github.com/gnuradio/gnuradio/blob/master/gr-filter/lib/firdes.cc#L641
std::vector<float> RRCFilter::root_raised_cosine(double gain,
                                                 double sampling_freq,
                                                 double symbol_rate,
                                                 double alpha, int ntaps) {
  ntaps |= 1; // ensure that ntaps is odd

  double spb = sampling_freq / symbol_rate; // samples per bit/symbol
  std::vector<float> taps(ntaps);
  double scale = 0;
  for (int i = 0; i < ntaps; i++) {
    double x1, x2, x3, num, den;
    double xindx = i - ntaps / 2;
    x1 = GR_M_PI * xindx / spb;
    x2 = 4 * alpha * xindx / spb;
    x3 = x2 * x2 - 1;

    if (fabs(x3) >= 0.000001) { // Avoid Rounding errors...
      if (i != ntaps / 2)
        num = cos((1 + alpha) * x1) +
              sin((1 - alpha) * x1) / (4 * alpha * xindx / spb);
      else
        num = cos((1 + alpha) * x1) + (1 - alpha) * GR_M_PI / (4 * alpha);
      den = x3 * GR_M_PI;
    } else {
      if (alpha == 1) {
        taps[i] = -1;
        scale += taps[i];
        continue;
      }
      x3 = (1 - alpha) * x1;
      x2 = (1 + alpha) * x1;
      num = (sin(x2) * (1 + alpha) * GR_M_PI -
             cos(x3) * ((1 - alpha) * GR_M_PI * spb) / (4 * alpha * xindx) +
             sin(x3) * spb * spb / (4 * alpha * xindx * xindx));
      den = -32 * GR_M_PI * alpha * alpha * xindx / spb;
    }
    taps[i] = 4 * alpha * num / den;
    scale += taps[i];
  }

  for (int i = 0; i < ntaps; i++)
    taps[i] = taps[i] * gain / scale;

  return taps;
}

std::vector<std::complex<float>>
RRCFilter::filter(const std::vector<std::complex<float>> &input,
                  const std::vector<float> &taps) {
  std::vector<std::complex<float>> output(input.size());

  for (std::size_t n = 0; n < input.size(); ++n) {
    std::complex<float> sum = 0.0f;

    for (std::size_t k = 0; k < taps.size(); ++k) {
      if (n >= k)
        sum += input[n - k] * taps[k];
    }

    output[n] = sum;
  }

  return output;
}