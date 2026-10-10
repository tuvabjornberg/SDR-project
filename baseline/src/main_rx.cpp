#include "common/config.h"
#include "phy/filter.h"
#include "phy/synchronizer.h"
#include "radio/receiver.h"

#include <cmath>
#include <iostream>
#include <vector>

int main() {
    Receiver rx;
    RRCFilter rrc;
    Synchronizer synchronizer;

    const auto taps = rrc.root_raised_cosine(GAIN, SAMPLE_FREQ, SYMBOL_RATE, ROLL_OFF_FACTOR,
                                             FILTER_SPAN * SAMPLES_PER_SYMBOL + 1);
    // For 65 taps keep the previous 64 input samples.
    const std::size_t history_size = taps.size() - 1;
    std::vector<std::complex<float>> history(history_size, {0.0f, 0.0f});

    rx.start();

    while (true) {
        auto samples = rx.receive(4096); // TODO Look into chunck size

        if (samples.empty()) {
            continue;
        }
        std::cout << "Received " << samples.size() << " samples\n";

        // Join previous samples so filtering continuous
        std::vector<std::complex<float>> input;
        input.reserve(history.size() + samples.size());
        input.insert(input.end(), history.begin(), history.end());
        input.insert(input.end(), samples.begin(), samples.end());

        auto filtered = rrc.filter(input, taps);

        // Keep only output corresponding to the new chunk
        std::vector<std::complex<float>> filtered_samples(filtered.begin() + history_size,
                                                          filtered.end());
        std::cout << "Filtered " << filtered_samples.size() << " samples\n";

        // Update raw history for the next chunk
        if (input.size() >= history_size) {
            history.assign(input.end() - history_size, input.end());
        }

        auto detections = synchronizer.process(filtered_samples);

        for (const auto offset : detections) {
            std::cout << "Preamble detected at filtered sample offset " << offset << '\n';
        }
    }

    return 0;
}
