#include <cstdint>
#include <vector>

#include "mac/CRC.h"
#include "mac/validation.h"

uint32_t Validator::compute_checksum(const std::vector<uint8_t> &data)
{
    return CRC::Calculate(data.data(), data.size(), CRC::CRC_32());
}