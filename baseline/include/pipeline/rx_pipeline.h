#pragma once

#include "mac/packet.h"
#include "phy/synchronizer.h"

#include <complex>
#include <cstdint>
#include <vector>

struct PreambleDiagnostic {
    std::size_t offset;
    std::size_t errors;
};

class RxPipeline {
  public:
    std::vector<PacketStruct> decode_packets(const std::vector<std::complex<float>>& samples);

    std::vector<uint8_t> packet_to_msg(std::vector<PacketStruct> received_packets);

    const std::vector<PacketStruct>& rejected() const {
        return m_rejected;
    }

  private:
    std::vector<PacketStruct> demod_parse(const std::vector<std::complex<float>>& samples,
                                          const std::vector<Detection>& detections);

    std::vector<PacketStruct> m_rejected;           // header OK, checksum failed
};