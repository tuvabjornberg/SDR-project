#pragma once

#include "mac/packet.h"

#include <complex>
#include <cstdint>
#include <vector>

class TxPipeline {
  public:
    std::vector<std::vector<uint8_t>> file_reader(const std::string& filename,
                                                  std::size_t payload_size);
    std::vector<std::complex<float>> generate_samples(const std::vector<uint8_t>& payload);

  private:
    void normalize(std::vector<std::complex<float>>& samples);
    Packet m_builder{0};
};