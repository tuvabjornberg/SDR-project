#include "phy/synchronizer.h"

#include "common/config.h"
#include "mac/preamble.h"

bool Synchronizer::matches_preamble(std::size_t offset, bool inverted) const {
    constexpr std::size_t PREAMBLE_BITS = 32;

    for (std::size_t i = 0; i < PREAMBLE_BITS; ++i) {
        const std::size_t sample_index = offset + i * SAMPLES_PER_SYMBOL;

        const bool received_bit = m_buffer[sample_index].real() >= 0.0f;

        const bool expected_bit = ((BARKER_PREAMBLE >> (31 - i)) & 1U) != 0;

        if (received_bit != (expected_bit != inverted))
            return false;
    }

    return true;
}

std::vector<std::size_t> Synchronizer::process(const std::vector<std::complex<float>>& samples) {
    constexpr std::size_t PREAMBLE_BITS = 32;
    constexpr std::size_t PREAMBLE_SAMPLES = (PREAMBLE_BITS - 1) * SAMPLES_PER_SYMBOL + 1;

    m_buffer.insert(m_buffer.end(), samples.begin(), samples.end());

    std::vector<std::size_t> detections;

    while (m_next_candidate + PREAMBLE_SAMPLES <= m_buffer.size()) {
        const std::size_t offset = m_next_candidate;

        if (matches_preamble(offset, false) || matches_preamble(offset, true)) {
            detections.push_back(m_absolute_start + offset);
        }

        ++m_next_candidate;
    }

    if (m_next_candidate > 0) {
        m_buffer.erase(m_buffer.begin(), m_buffer.begin() + m_next_candidate);

        m_absolute_start += m_next_candidate;
        m_next_candidate = 0;
    }

    return detections;
}