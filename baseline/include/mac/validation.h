#pragma once

#include <cstdint>
#include <vector>

class Validator {
  public:
    static uint32_t compute_checksum(const std::vector<uint8_t>& data);

  private:
};
