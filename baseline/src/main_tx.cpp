#include "common/config.h"
#include "common/settings.h"
#include "pipeline/tx_pipeline.h"
#include "radio/transmitter.h"

#include <chrono>
#include <cmath>
#include <cstddef>
#include <iostream>
#include <thread>
#include <vector>

int main(int argc, char* argv[]) {
    std::cout << "========================================" << std::endl;
    std::cout << "Transmitter" << std::endl;
    std::cout << "========================================" << std::endl;

    TxPipeline pipeline;
    auto payload = pipeline.file_reader(argc, argv);
    auto samples = pipeline.generate_samples(payload);

    Transmitter tx;
    size_t num_sent;
    std::cout << "[TX] Sending " << samples.size() << " samples..." << std::endl;

    for (int i = 0; i < settings::TX_REPETITIONS; i++) {
        num_sent = tx.send(samples);
        if (num_sent != samples.size()) {
            std::cerr << "[ERROR] Sample count mismatch: expected " << samples.size() << ", sent "
                      << num_sent << '\n';
            return 1;
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(settings::TX_DELAY_MS));
    }

    std::cout << "[OK] Transmission complete\n\n";

    return 0;
}
