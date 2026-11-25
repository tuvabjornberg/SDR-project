#pragma once

#include "mac/packet.h"
#include "phy/synchronizer.h"

#include <chrono>
#include <complex>
#include <cstdint>
#include <vector>

struct PreambleDiagnostic {
    std::size_t offset;
    std::size_t errors;
};

struct PipelineTimings {
    double cfo_correction_ms = 0.0;
    double rrc_filter_ms = 0.0;
    double synchronization_ms = 0.0;
    double demodulation_ms = 0.0;
    double parsing_ms = 0.0;
    double total_ms = 0.0;
};

struct RxStats {
    std::size_t preambles_detected = 0;
    std::size_t packets_decoded = 0;
    std::size_t parse_failures = 0;
    std::size_t invalid_offsets = 0;
    PipelineTimings timings;
};

class RxPipeline {
  public:
    std::vector<PacketStruct>
    decode_packets(std::vector<std::vector<std::complex<float>>>& captured_samples);

    std::vector<uint8_t> packet_to_msg(std::vector<PacketStruct> received_packets);

    const std::vector<PacketStruct>& rejected() const {
        return m_rejected;
    }

    const RxStats& stats() const {
        return m_stats;
    }

  private:
    std::vector<PacketStruct> demod_parse(std::vector<std::vector<std::complex<float>>>& filtered,
                                          const std::vector<Detection>& detections);

    std::vector<PacketStruct> m_rejected; // header OK, checksum failed

    RxStats m_stats;
};