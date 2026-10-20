#pragma once

#include <cstddef>

#define VERBOSE 0 // printing stuff

namespace settings {

constexpr int TX_REPETITIONS = 1;
constexpr int TX_DELAY_MS = 10;
constexpr float TX_MAX_AMPLITUDE = 0.7f;

constexpr std::size_t RX_CHUNK_SIZE = 4096;
constexpr int RX_CAPTURE_SECONDS = 5;

constexpr int PACKET_DELAY_MS = 100;

} // namespace settings