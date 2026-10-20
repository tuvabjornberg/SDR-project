#include "common/config.h"
#include "common/settings.h"
#include "mac/preamble.h"
#include "pipeline/rx_pipeline.h"
#include "radio/receiver.h"

#include <chrono>
#include <cmath>
#include <complex>
#include <iostream>
#include <string>
#include <vector>

int main() {
    std::cout << "========================================" << std::endl;
    std::cout << "Receiver" << std::endl;
    std::cout << "========================================" << std::endl;
    Receiver rx;

    rx.start();

    std::vector<std::complex<float>> captured_samples;

    auto start = std::chrono::steady_clock::now();

    while (std::chrono::steady_clock::now() - start <
           std::chrono::seconds(settings::RX_CAPTURE_SECONDS)) {

        auto chunk = rx.receive(settings::RX_CHUNK_SIZE);

        captured_samples.insert(captured_samples.end(), chunk.samples.begin(), chunk.samples.end());
    }

    rx.stop();

    std::cout << "[RX] Receiving complete, captured samples: " << captured_samples.size()
              << std::endl;
    if (captured_samples.empty()) {
        std::cerr << "[RX] FAIL: No samples received\n";
        return 1;
    }

    RxPipeline pipeline;

    auto parsed_packets = pipeline.decode_packets(captured_samples);
    auto message = pipeline.packet_to_msg(parsed_packets);

    std::cout << "[RX] Received payload size: " << message.size() << " bytes" << std::endl;
    std::cout << "[RX] Received: " << std::string(message.begin(), message.end()) << std::endl;

    return 0;
}
