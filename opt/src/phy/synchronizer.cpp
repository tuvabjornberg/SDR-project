#include "phy/synchronizer.h"

#include "common/config.h"
#include "mac/preamble.h"

#include <algorithm>
#include <cmath>

PreambleHit Synchronizer::correlate(std::size_t off) const {
    using Cd = std::complex<double>;
    constexpr std::size_t H = PREAMBLE_SIZE / 2;
    const std::size_t num_channels = m_buffer.size();
    std::vector<Cd> c1(num_channels, Cd{});
    std::vector<Cd> c2(num_channels, Cd{});
    double energy = 0.0;

    for (std::size_t i = 0; i < PREAMBLE_SIZE; ++i) {
        const double s = ((BARKER_PREAMBLE >> (PREAMBLE_SIZE - 1 - i)) & 1U) ? 1.0 : -1.0;
        for (std::size_t ch = 0; ch < num_channels; ++ch) {
            const Cd y = m_buffer[ch][off + i * SAMPLES_PER_SYMBOL];
            (i < H ? c1[ch] : c2[ch]) += y * s;
            energy += std::norm(y);
        }
    }

    const double span_s = double(H * SAMPLES_PER_SYMBOL) / SAMPLE_FREQ;

    Cd cross{};
    for (std::size_t ch = 0; ch < num_channels; ++ch) {
        cross += c2[ch] * std::conj(c1[ch]); // channel phase cancels, CFO adds up
    }
    const double cfo = std::arg(cross) / (2.0 * M_PI * span_s);  // +-7.8 kHz range
    const Cd back = std::polar(1.0, -2.0 * M_PI * cfo * span_s); // centre -> start

    PreambleHit h{};
    h.g.resize(num_channels);
    double csum = 0.0;
    for (std::size_t ch = 0; ch < num_channels; ++ch) {
        const Cd c = c1[ch] + c2[ch];
        csum += std::norm(c);
        h.g[ch] = c * back;
    }
    h.metric = std::sqrt(csum) / std::sqrt(PREAMBLE_SIZE * energy + 1e-20);
    h.cfo_hz = cfo;
    return h;
}

std::vector<Detection>
Synchronizer::process(const std::vector<std::vector<std::complex<float>>>& samples) {
    if (samples.size() != m_buffer.size()) {
        throw std::invalid_argument("Synchronizer RX channel count mismatch");
    }

    if (samples.empty()) {
        return {};
    }

    for (std::size_t ch = 0; ch < samples.size(); ++ch) {
        m_buffer[ch].insert(m_buffer[ch].end(), samples[ch].begin(), samples[ch].end());
    }

    std::size_t n = m_buffer[0].size();
    for (const auto& channel : m_buffer) {
        n = std::min(n, channel.size());
    }

    std::vector<Detection> detections;

    constexpr double THRESHOLD = 0.8; // above the ~0.61 sidelobe, below the main peak

    while (m_next_candidate + PREAMBLE_SAMPLES <= n) {
        const std::size_t cand = m_next_candidate;

        PreambleHit best_hit = correlate(cand);
        if (best_hit.metric < THRESHOLD) {
            ++m_next_candidate;
            continue;
        }

        // First hit may be a few samples early: take the peak within the next 2 symbols.
        std::size_t best = cand;
        for (std::size_t d = 1; d <= 2 * SAMPLES_PER_SYMBOL; ++d) {
            const std::size_t o = cand + d;
            if (o + PREAMBLE_SAMPLES > n)
                break;
            const PreambleHit h = correlate(o);
            if (h.metric > best_hit.metric) {
                best_hit = h;
                best = o;
            }
        }

        detections.push_back(Detection{best, best_hit.cfo_hz, best_hit.g, best_hit.metric});
        m_next_candidate = best + MIN_PACKET_SIZE * SAMPLES_PER_SYMBOL;
    }

    if (m_next_candidate > 0) {
        const std::size_t drop = std::min(m_next_candidate, n);
        for (auto& channel : m_buffer) {
            channel.erase(channel.begin(), channel.begin() + drop);
        }
        m_absolute_start += drop;
        m_next_candidate = 0;
    }

    return detections;
}

void Synchronizer::reset() {
    for (auto& b : m_buffer)
        b.clear();
    m_absolute_start = 0;
    m_next_candidate = 0;
}