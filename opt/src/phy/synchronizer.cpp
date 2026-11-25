#include "phy/synchronizer.h"

#include "mac/preamble.h"

#include <algorithm>
#include <array>
#include <cmath>

namespace {
constexpr std::array<double, PREAMBLE_SIZE> PREAMBLE_SIGNS = [] {
    std::array<double, PREAMBLE_SIZE> signs{};

    for (std::size_t i = 0; i < PREAMBLE_SIZE; ++i) {
        signs[i] = ((BARKER_PREAMBLE >> (PREAMBLE_SIZE - 1 - i)) & 1U) ? 1.0 : -1.0;
    }

    return signs;
}();
} // namespace

float Synchronizer::calculate_energy(std::size_t off) {
    constexpr std::size_t LAST_TAP = (PREAMBLE_SIZE - 1) * SAMPLES_PER_SYMBOL;

    const std::size_t phase = off % SAMPLES_PER_SYMBOL;
    auto& state = m_energy_states[phase];

    if (!state.initialized || state.offset + SAMPLES_PER_SYMBOL != off) {
        state.energy = 0.0f;

        for (std::size_t i = 0; i < PREAMBLE_SIZE; ++i) {
            for (std::size_t ch = 0; ch < m_buffer.size(); ++ch) {
                state.energy += std::norm(m_buffer[ch][off + i * SAMPLES_PER_SYMBOL]);
            }
        }

        state.offset = off;
        state.initialized = true;

        return state.energy;
    }

    // Remove the oldest samples and add the newest samples.
    for (std::size_t ch = 0; ch < m_buffer.size(); ++ch) {
        const auto& old_sample = m_buffer[ch][off - SAMPLES_PER_SYMBOL];

        const auto& new_sample = m_buffer[ch][off + LAST_TAP];

        state.energy -= std::norm(old_sample);
        state.energy += std::norm(new_sample);
    }

    state.offset = off;
    return state.energy;
}

PreambleHit Synchronizer::correlate(std::size_t off) {
    constexpr std::size_t H = PREAMBLE_SIZE / 2;
    const std::size_t num_channels = m_buffer.size();

    std::array<std::complex<float>, MAX_RX_CHANNELS> c1{};
    std::array<std::complex<float>, MAX_RX_CHANNELS> c2{};

    // float energy = 0.0f;

    for (std::size_t i = 0; i < PREAMBLE_SIZE; ++i) {
        const std::size_t half = (i < H) ? 0 : 1;

        for (std::size_t ch = 0; ch < num_channels; ++ch) {
            const std::complex<float> y = m_buffer[ch][off + i * SAMPLES_PER_SYMBOL];

            if (PREAMBLE_SIGNS[i] > 0.0)
                (half == 0 ? c1[ch] : c2[ch]) += y;
            else
                (half == 0 ? c1[ch] : c2[ch]) -= y;

            // energy += std::norm(y);
        }
    }
    const float energy = calculate_energy(off);

    const double span_s = double(H * SAMPLES_PER_SYMBOL) / SAMPLE_FREQ;

    std::complex<float> cross{};
    for (std::size_t ch = 0; ch < num_channels; ++ch) {
        cross += c2[ch] * std::conj(c1[ch]); // channel phase cancels, CFO adds up
    }
    const double cfo = std::arg(cross) / (2.0 * M_PI * span_s); // +-7.8 kHz range
    const float phase = -2.0f * static_cast<float>(M_PI) * cfo * span_s;
    const std::complex<float> back{std::cos(phase), std::sin(phase)};

    PreambleHit h{};

    double csum = 0.0;
    for (std::size_t ch = 0; ch < num_channels; ++ch) {
        const std::complex<float> c = c1[ch] + c2[ch];
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
        for (auto& state : m_energy_states) {
            state.initialized = false;
        }
        m_absolute_start += drop;
        m_next_candidate = 0;
    }

    return detections;
}

void Synchronizer::reset() {
    for (auto& b : m_buffer)
        b.clear();
    for (auto& state : m_energy_states)
        state = {};
    m_absolute_start = 0;
    m_next_candidate = 0;
}