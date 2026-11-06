#include "phy/synchronizer.h"

#include "common/config.h"
#include "common/settings.h"
#include "mac/preamble.h"

#include <algorithm>
#include <limits>

double Synchronizer::preamble_score(std::size_t offset) const {
    double s = 0.0;

    for (std::size_t i = 0; i < PREAMBLE_SIZE; ++i) {
        const bool expected = ((BARKER_PREAMBLE >> (PREAMBLE_SIZE - 1 - i)) & 1U) != 0;
        s += m_buffer[offset + i * SAMPLES_PER_SYMBOL].real() * (expected ? 1.0 : -1.0);
    }
    return s;
}

// Mean magnitude at the 32 preamble sample points (rejects pure noise).
float Synchronizer::preamble_amp(std::size_t offset) const {
    float a = 0.f;
    for (std::size_t i = 0; i < PREAMBLE_SIZE; ++i)
        a += std::abs(m_buffer[offset + i * SAMPLES_PER_SYMBOL]);
    return a / PREAMBLE_SIZE;
}

PreambleHit Synchronizer::correlate(std::size_t off) const {
    constexpr std::size_t H = PREAMBLE_SIZE / 2;
    std::complex<double> c1{}, c2{};
    double energy = 0.0;

    for (std::size_t i = 0; i < PREAMBLE_SIZE; ++i) {
        const double s = ((BARKER_PREAMBLE >> (PREAMBLE_SIZE - 1 - i)) & 1U) ? 1.0 : -1.0;
        const std::complex<double> y = m_buffer[off + i * SAMPLES_PER_SYMBOL];
        (i < H ? c1 : c2) += y * s;
        energy += std::norm(y);
    }

    const auto c = c1 + c2;
    const double span_s = double(H * SAMPLES_PER_SYMBOL) / SAMPLE_FREQ;
    const double cfo = std::arg(c2 * std::conj(c1)) / (2.0 * M_PI * span_s); // +-7.8 kHz range
    // c's phase refers to the preamble centre, move it back to the start
    const double phase = std::arg(c) - 2.0 * M_PI * cfo * span_s;

    return {std::abs(c) / std::sqrt(PREAMBLE_SIZE * energy + 1e-20), phase, cfo};
}

std::vector<Detection> Synchronizer::process_old(const std::vector<std::complex<float>>& samples) {
    m_buffer.insert(m_buffer.end(), samples.begin(), samples.end());
    std::vector<Detection> detections;

    constexpr std::size_t MAX_ERRORS = 3;
    constexpr float MIN_AMP = 0.01f; // tune: above the noise floor, below packet level

    while (m_next_candidate + PREAMBLE_SAMPLES <= m_buffer.size()) {
        const std::size_t cand = m_next_candidate;

        if (std::min(preamble_errors(cand, false), preamble_errors(cand, true)) > MAX_ERRORS ||
            preamble_amp(cand) < MIN_AMP) {
            ++m_next_candidate;
            continue;
        }

        // The first hit may be a few samples early: pick the best alignment in the next symbol.
        std::size_t best = cand;
        double best_score = 0.0;
        for (std::size_t d = 0; d <= SAMPLES_PER_SYMBOL; ++d) {
            const std::size_t o = cand + d;
            if (o + PREAMBLE_SAMPLES > m_buffer.size())
                break;
            if (std::min(preamble_errors(o, false), preamble_errors(o, true)) > MAX_ERRORS)
                continue;
            const double s = preamble_score(o);
            if (std::abs(s) > std::abs(best_score)) {
                best_score = s;
                best = o;
            }
        }

        detections.push_back(Detection{best, 0, best_score < 0.0});
        m_next_candidate =
            best + MIN_PACKET_SIZE * SAMPLES_PER_SYMBOL; // skip past this packet's start
    }

#if VERBOSE
//         std::cout << "[Preamble]" << std::endl;
// std::cout << "\tBest preamble errors: " << best_errors << " / " << PREAMBLE_SIZE
//                   << std::endl;
//         std::cout << "\tBest sample offset: " << best_offset << std::endl;
//         std::cout << "\tBest sample phase: " << best_phase << std::endl;
//         std::cout << "\tInverted: " << (last_inverted ? "yes" : "no") << std::endl;
#endif

    if (m_next_candidate > 0) {
        const std::size_t drop = std::min(m_next_candidate, m_buffer.size());
        m_buffer.erase(m_buffer.begin(), m_buffer.begin() + drop);
        m_absolute_start += drop;
        m_next_candidate = 0;
    }

#if VERBOSE
    std::cout << "\tPreambles detected: " << detections.size() << std::endl;
#endif

    return detections;
}

std::vector<Detection> Synchronizer::process(const std::vector<std::complex<float>>& samples) {
    m_buffer.insert(m_buffer.end(), samples.begin(), samples.end());
    std::vector<Detection> detections;

    constexpr double THRESHOLD = 0.8; // above the ~0.61 sidelobe, below the main peak

    while (m_next_candidate + PREAMBLE_SAMPLES <= m_buffer.size()) {
        const std::size_t cand = m_next_candidate;

        if (correlate(cand).metric < THRESHOLD) {
            ++m_next_candidate;
            continue;
        }

        // First hit may be a few samples early: take the peak within the next symbol.
        std::size_t best = cand;
        PreambleHit best_hit = correlate(cand);
        for (std::size_t d = 1; d <= 2 * SAMPLES_PER_SYMBOL; ++d) { // 2 symbols, not 1
            const std::size_t o = cand + d;
            if (o + PREAMBLE_SAMPLES > m_buffer.size())
                break;
            const PreambleHit h = correlate(o);
            if (h.metric > best_hit.metric) {
                best_hit = h;
                best = o;
            }
        }

        detections.push_back(Detection{best, best_hit.phase, best_hit.cfo_hz, best_hit.metric});
        m_next_candidate = best + MIN_PACKET_SIZE * SAMPLES_PER_SYMBOL;
    }

    if (m_next_candidate > 0) {
        const std::size_t drop = std::min(m_next_candidate, m_buffer.size());
        m_buffer.erase(m_buffer.begin(), m_buffer.begin() + drop);
        m_absolute_start += drop;
        m_next_candidate = 0;
    }

    return detections;
}

void Synchronizer::reset() {
    m_buffer.clear();
    m_absolute_start = 0;
    m_next_candidate = 0;
    m_last_detection = std::numeric_limits<std::size_t>::max();
    // m_last_inverted = false;
}

std::size_t Synchronizer::preamble_errors(std::size_t offset, bool inverted) const {
    std::size_t errors = 0;

    for (std::size_t i = 0; i < PREAMBLE_SIZE; ++i) {
        const bool received = m_buffer[offset + i * SAMPLES_PER_SYMBOL].real() >= 0.0f;
        const bool expected = ((BARKER_PREAMBLE >> (PREAMBLE_SIZE - 1 - i)) & 1U) != 0;

        if (received != (expected != inverted)) {
            ++errors;
        }
    }

    return errors;
}
