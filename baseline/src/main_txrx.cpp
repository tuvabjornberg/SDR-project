#include "common/config.h"
#include "common/settings.h"
#include "mac/preamble.h"
#include "pipeline/rx_pipeline.h"
#include "pipeline/tx_pipeline.h"
#include "radio/receiver.h"
#include "radio/transmitter.h"

#include <chrono>
#include <cmath>
#include <complex>
#include <cstddef>
#include <iostream>
#include <string>
#include <thread>
#include <vector>

int main(int argc, char* argv[]) {
    std::cout << "========================================" << std::endl;
    std::cout << "Transmitter + Receiver" << std::endl;
    std::cout << "========================================" << std::endl;

    TxPipeline tx_pipeline;
    auto payload = tx_pipeline.file_reader(argc, argv);
    auto samples = tx_pipeline.generate_samples(payload);

    auto usrp = uhd::usrp::multi_usrp::make(TX_SERIAL);
    usrp->set_clock_source("internal");
    usrp->set_tx_subdev_spec(TX_SUBDEV);
    usrp->set_rx_subdev_spec(RX_SUBDEV);

    Transmitter tx(usrp);
    Receiver rx(usrp);

    std::vector<std::complex<float>> captured_samples;

    rx.start();

    auto start = std::chrono::steady_clock::now();
    std::thread rx_thread([&]() {
        while (std::chrono::steady_clock::now() - start <
               std::chrono::seconds(settings::RX_CAPTURE_SECONDS)) {

            auto chunk = rx.receive(settings::RX_CHUNK_SIZE);

            captured_samples.insert(captured_samples.end(), chunk.samples.begin(),
                                    chunk.samples.end());
        }
    });

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

    rx.stop();
    rx_thread.join();

    std::cout << "[RX] Receiving complete, captured samples: " << captured_samples.size()
              << std::endl;
    if (captured_samples.empty()) {
        std::cerr << "[RX] FAIL: No samples received\n";
        return 1;
    }

    RxPipeline rx_pipeline;

    auto parsed_packets = rx_pipeline.decode_packets(captured_samples);
    auto message = rx_pipeline.packet_to_msg(parsed_packets);

    std::cout << "[RX] Received payload size: " << message.size() << " bytes" << std::endl;
    std::cout << "[RX] Received: " << std::string(message.begin(), message.end()) << std::endl;

    if (message != payload) {
        std::cerr << "[RX] FAIL: Reassembled message mismatch" << std::endl;
        std::cerr << "[RX] Expected: " << std::string(payload.begin(), payload.end()) << std::endl;
        std::cerr << "[RX] Received: " << std::string(message.begin(), message.end()) << std::endl;
        return 1;
    }

    std::cout << "PASS: Complete message received successfully\n";

    return 0;
}
