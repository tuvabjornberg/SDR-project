#include <cmath>
#include <cstdint>
#include <string>
#include <uhd/types/tune_request.hpp>

// namespace config {

//---------------TX----------------
// Packet Builder
static constexpr std::size_t MAX_PAYLOAD_SIZE = 512;
static constexpr std::size_t PREAMBLE_SIZE = 32;
static constexpr std::size_t CHECKSUM_SIZE = 32;
static constexpr std::size_t HEADER_SIZE = 32;
static constexpr std::size_t MIN_PACKET_SIZE = PREAMBLE_SIZE + HEADER_SIZE + CHECKSUM_SIZE;

// Modulator
static constexpr uint32_t SAMPLE_FREQ = 2000000; // samples/sec
static constexpr uint32_t SYMBOL_RATE = 250000;  // symbols/sec
static_assert(SAMPLE_FREQ % SYMBOL_RATE == 0,
              "SAMPLE_FREQ must be an integer multiple of SYMBOL_RATE");
static constexpr uint32_t SAMPLES_PER_SYMBOL = SAMPLE_FREQ / SYMBOL_RATE;
constexpr std::size_t PREAMBLE_SAMPLES = (PREAMBLE_SIZE - 1) * SAMPLES_PER_SYMBOL + 1;

// Filter
static constexpr double ROLL_OFF_FACTOR = 0.35;
static constexpr int FILTER_SPAN = 8; // filter span in symbols
static constexpr double GAIN = 1.0;

// TX node
static const std::string TX_SERIAL = "serial=31993E7";
static const std::string TX_SUBDEV = "A:A";
static const std::string TX_ANT = "TX/RX";
static const std::string TX_REF = "internal";
static constexpr double TX_RATE = SAMPLE_FREQ;
static const uhd::tune_request_t TX_FREQ = 2450e6;
static constexpr double TX_GAIN = 60;
static constexpr double TX_BANDWIDTH = 1.5e6;

//---------------RX----------------

// RX node
static const std::string RX_SERIAL = "serial=31993F0";
static const std::string RX_SUBDEV = "A:A A:B";
static const std::string RX_ANT = "RX2";
static const std::string RX_REF = "internal";
static constexpr double RX_RATE = SAMPLE_FREQ;
static const uhd::tune_request_t RX_FREQ = 2450e6;
static constexpr double RX_GAIN = 40;
static constexpr double RX_BANDWIDTH = 1.5e6;

// } // namespace config