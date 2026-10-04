#pragma once

#include <cstdint>
#include <span>
#include <vector>

class Validator
{
public:
    static uint32_t compute_checksum(const std::vector<uint8_t> &data);

private:
};
