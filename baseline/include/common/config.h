#include <cmath>
#include <cstdint>

//---------------TX----------------
// Packet Builder
static constexpr std::size_t MAX_PAYLOAD_SIZE = 512;

// Modulator
static constexpr uint32_t SAMPLE_FREQ = 2000000; // samples/sec
static constexpr uint32_t SYMBOL_RATE = 250000;  // symbols/sec
static_assert(SAMPLE_FREQ % SYMBOL_RATE == 0,
              "SAMPLE_FREQ must be an integer multiple of SYMBOL_RATE");
static constexpr uint32_t SAMPLES_PER_SYMBOL = SAMPLE_FREQ / SYMBOL_RATE;

// Filter
static constexpr double ROLL_OFF_FACTOR = 0.35;
static constexpr int FILTER_SPAN = 8; // filter span in symbols
static constexpr double GAIN = 1.0;

//---------------RX----------------
