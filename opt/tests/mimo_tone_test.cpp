// Clauded
// 2x2 hardware sanity test.
// Sends a 100 kHz tone from TX0 only, then from TX1 only, and measures what each RX channel sees.
// Prints a 2x2 channel power matrix, SNR, tone frequency (-> CFO) and the RX0/RX1 relative phase.

#include "common/config.h"
#include "radio/receiver.h"
#include "radio/transmitter.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <complex>
#include <cstddef>
#include <iomanip>
#include <iostream>
#include <thread>
#include <vector>

namespace {

using Cvec = std::vector<std::complex<float>>;
using Cd = std::complex<double>;

constexpr double TONE_HZ = 100e3;
constexpr std::size_t TONE_SAMPLES = 400000; // 0.2 s at 2 Msps
constexpr float TONE_AMP = 0.5f;
constexpr double CAPTURE_SECONDS = 1.0;
constexpr std::size_t WIN = 2048;

struct Range {
    std::size_t begin = 0, end = 0;
    double peak_window_power = 0.0;
    bool ok = false;
};

struct Stats {
    double power_dbfs = -200.0;
    double snr_db = 0.0;
    double tone_hz = 0.0;
    double peak = 0.0;
};

std::vector<double> window_powers(const Cvec& x) {
    std::vector<double> p;
    for (std::size_t i = 0; i + WIN <= x.size(); i += WIN) {
        double s = 0.0;
        for (std::size_t k = 0; k < WIN; ++k)
            s += std::norm(x[i + k]);
        p.push_back(s / WIN);
    }
    return p;
}

double noise_floor(const Cvec& x) {
    auto p = window_powers(x);
    if (p.empty())
        return 1e-20;
    std::sort(p.begin(), p.end());
    return std::max(p[p.size() / 10], 1e-20); // 10th percentile of window power
}

// Samples where the tone is on (windows above half the peak power, one window trimmed at each end).
Range find_active(const Cvec& x) {
    Range r;
    const auto p = window_powers(x);
    if (p.size() < 8)
        return r;
    r.peak_window_power = *std::max_element(p.begin(), p.end());
    std::size_t first = p.size(), last = 0;
    for (std::size_t i = 0; i < p.size(); ++i) {
        if (p[i] >= 0.5 * r.peak_window_power) {
            first = std::min(first, i);
            last = std::max(last, i + 1);
        }
    }
    if (last < first + 4)
        return r;
    r.begin = (first + 1) * WIN;
    r.end = (last - 1) * WIN;
    r.ok = true;
    return r;
}

Stats measure(const Cvec& x, const Range& r, double noise) {
    Stats s;
    double p = 0.0;
    Cd acc{};
    for (std::size_t n = r.begin; n < r.end; ++n) {
        p += std::norm(x[n]);
        s.peak = std::max(s.peak, static_cast<double>(std::abs(x[n])));
        if (n + 1 < r.end)
            acc += Cd(x[n + 1]) * std::conj(Cd(x[n]));
    }
    p /= static_cast<double>(r.end - r.begin);
    s.power_dbfs = 10.0 * std::log10(std::max(p, 1e-20));
    s.snr_db = 10.0 * std::log10(std::max(p - noise, 1e-20) / noise);
    s.tone_hz = std::arg(acc) * SAMPLE_FREQ / (2.0 * M_PI);
    return s;
}

double rel_phase_deg(const Cvec& a, const Cvec& b, std::size_t from, std::size_t to) {
    Cd acc{};
    for (std::size_t n = from; n < to; ++n)
        acc += Cd(b[n]) * std::conj(Cd(a[n]));
    return std::arg(acc) * 180.0 / M_PI;
}

double wrap_deg(double d) {
    while (d > 180.0)
        d -= 360.0;
    while (d < -180.0)
        d += 360.0;
    return d;
}

std::array<Cvec, 2> run_case(Transmitter& tx, Receiver& rx, bool use_tx0, bool use_tx1) {
    std::array<Cvec, 2> cap;
    for (auto& c : cap)
        c.reserve(static_cast<std::size_t>(CAPTURE_SECONDS * SAMPLE_FREQ * 1.2));

    rx.start();
    const auto t0 = std::chrono::steady_clock::now();
    std::thread rx_thread([&]() {
        while (std::chrono::steady_clock::now() - t0 <
               std::chrono::duration<double>(CAPTURE_SECONDS)) {
            auto chunk = rx.receive(4096);
            for (std::size_t ch = 0; ch < 2; ++ch)
                cap[ch].insert(cap[ch].end(), chunk.samples[ch].begin(), chunk.samples[ch].end());
        }
    });

    std::this_thread::sleep_for(std::chrono::milliseconds(300)); // let RX settle

    Cvec tone(TONE_SAMPLES), zeros(TONE_SAMPLES);
    for (std::size_t n = 0; n < TONE_SAMPLES; ++n) {
        const double ph = 2.0 * M_PI * TONE_HZ * static_cast<double>(n) / SAMPLE_FREQ;
        tone[n] = TONE_AMP * std::complex<float>(std::cos(ph), std::sin(ph));
    }

    const std::size_t sent = tx.send(use_tx0 ? tone : zeros, use_tx1 ? tone : zeros);
    if (sent != TONE_SAMPLES)
        std::cerr << "[WARNING] TX sent " << sent << " of " << TONE_SAMPLES << " samples\n";

    rx_thread.join();
    rx.stop();
    std::this_thread::sleep_for(std::chrono::milliseconds(200));
    return cap;
}

} // namespace

int main() {
    // When running with 1 SDR
    auto usrp = uhd::usrp::multi_usrp::make(TX_SERIAL);

    usrp->set_clock_source("internal");
    usrp->set_tx_subdev_spec(TX_SUBDEV);
    usrp->set_rx_subdev_spec(RX_SUBDEV);

    Transmitter tx(usrp);
    Receiver rx(usrp);

    Stats m[2][2]; // m[rx][tx]

    for (std::size_t t = 0; t < 2; ++t) {
        std::cout << "\n=== Tone from TX" << t << " only ===\n";
        auto cap = run_case(tx, rx, t == 0, t == 1);

        const double noise0 = noise_floor(cap[0]);
        const double noise1 = noise_floor(cap[1]);
        const Range r0 = find_active(cap[0]);
        const Range r1 = find_active(cap[1]);
        const Range& r = (r0.peak_window_power >= r1.peak_window_power) ? r0 : r1;

        if (!r.ok || cap[0].size() < r.end || cap[1].size() < r.end) {
            std::cout << "No usable tone captured (got " << cap[0].size() << " / " << cap[1].size()
                      << " samples). Check antennas and gains.\n";
            continue;
        }

        m[0][t] = measure(cap[0], r, noise0);
        m[1][t] = measure(cap[1], r, noise1);

        for (std::size_t c = 0; c < 2; ++c) {
            std::cout << std::fixed << std::setprecision(1) << "RX" << c << ": power "
                      << m[c][t].power_dbfs << " dBFS, SNR " << m[c][t].snr_db << " dB, tone "
                      << m[c][t].tone_hz << " Hz (CFO " << m[c][t].tone_hz - TONE_HZ
                      << " Hz), peak " << std::setprecision(2) << m[c][t].peak << '\n';
            if (m[c][t].peak > 0.8)
                std::cout << "   [WARNING] RX" << c << " close to saturation, lower TX/RX gain\n";
        }

        // RX1 relative to RX0, first half vs second half of the tone
        const std::size_t mid = r.begin + (r.end - r.begin) / 2;
        const double ph1 = rel_phase_deg(cap[0], cap[1], r.begin, mid);
        const double ph2 = rel_phase_deg(cap[0], cap[1], mid, r.end);
        std::cout << std::fixed << std::setprecision(1) << "RX1 vs RX0: gain "
                  << m[1][t].power_dbfs - m[0][t].power_dbfs << " dB, phase " << ph1
                  << " deg, drift over tone " << wrap_deg(ph2 - ph1) << " deg\n";
    }

    std::cout << "\n=== Channel power matrix (dBFS), rows RX, columns TX ===\n";
    std::cout << std::fixed << std::setprecision(1) << "          TX0      TX1\n";
    for (std::size_t c = 0; c < 2; ++c)
        std::cout << "RX" << c << "   " << std::setw(8) << m[c][0].power_dbfs << " " << std::setw(8)
                  << m[c][1].power_dbfs << '\n';

    return 0;
}