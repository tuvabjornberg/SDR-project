#include <iostream>
#include <vector>
#include <complex>
#include <cmath>
#include <cstdlib>
#include <uhd/usrp/multi_usrp.hpp>
#include <uhd/utils/thread.hpp>

// AI generated
int main(int argc, char *argv[])
{
    std::cout << "[+] Searching for USRP B210..." << std::endl;

    // 1. Instantiate the USRP device (type=b200 catches B200/B210)
    uhd::usrp::multi_usrp::sptr usrp;
    try
    {
        usrp = uhd::usrp::multi_usrp::make("serial=31993E7");
    }
    catch (const std::exception &e)
    {
        std::cerr << "[-] Error initializing USRP: " << e.what() << std::endl;
        std::cerr << "    Check USB 3.0 connection or run 'uhd_find_devices'." << std::endl;
        return EXIT_FAILURE;
    }

    std::cout << "[+] Connected to: " << usrp->get_pp_string() << std::endl;

    // Select RX channel A
    usrp->set_rx_subdev_spec(uhd::usrp::subdev_spec_t("A:A"));

    const double target_freq = 2.45e9;
    const double sample_rate = 1e6;
    const double gain = 30.0;
    const double bandwidth = 1e6;

    // Configure RX
    usrp->set_rx_rate(sample_rate);
    usrp->set_rx_freq(uhd::tune_request_t(target_freq));
    usrp->set_rx_gain(gain);
    usrp->set_rx_bandwidth(bandwidth);
    usrp->set_rx_antenna("TX/RX");

    std::cout << "[+] Actual RX rate: " << usrp->get_rx_rate() / 1e6 << " MSps" << std::endl;
    std::cout << "[+] Actual RX frequency: " << usrp->get_rx_freq() / 1e6 << " MHz" << std::endl;
    std::cout << "[+] Actual RX gain: " << usrp->get_rx_gain() << " dB" << std::endl;
    std::cout << "[+] Actual RX bandwidth: " << usrp->get_rx_bandwidth() / 1e6 << " MHz" << std::endl;

    // Create RX streamer
    uhd::stream_args_t stream_args("fc32", "sc16");
    uhd::rx_streamer::sptr rx_stream = usrp->get_rx_stream(stream_args);

    // 4. Setup Buffer & Issue Stream Command
    constexpr size_t num_samples = 1024;
    std::vector<std::complex<float>> buff(num_samples);

    // Request exactly num_samples
    uhd::stream_cmd_t stream_cmd(uhd::stream_cmd_t::STREAM_MODE_NUM_SAMPS_AND_DONE);
    stream_cmd.num_samps = num_samples;
    stream_cmd.stream_now = true;
    rx_stream->issue_stream_cmd(stream_cmd);

    // Receive
    uhd::rx_metadata_t md;
    const size_t num_rx_samps = rx_stream->recv(buff.data(), buff.size(), md, 3.0);

    if (md.error_code != uhd::rx_metadata_t::ERROR_CODE_NONE)
    {
        std::cerr << "[-] RX error: " << md.strerror() << std::endl;
        return EXIT_FAILURE;
    }

    if (num_rx_samps == 0)
    {
        std::cerr << "[-] No samples received." << std::endl;
        return EXIT_FAILURE;
    }

    std::cout << "\n[+] SUCCESS!" << std::endl;
    std::cout << "[+] Received " << num_rx_samps << " samples." << std::endl;

    // Calculate normalized average power
    double total_power = 0.0;

    for (size_t i = 0; i < num_rx_samps; ++i)
    {
        total_power += std::norm(buff[i]);
    }

    const double avg_power = total_power / static_cast<double>(num_rx_samps);

    std::cout << "[+] Average normalized power: " << avg_power << std::endl;

    // Display samples
    const size_t samples_to_print = std::min<size_t>(5, num_rx_samps);

    std::cout << "[+] First " << samples_to_print << " IQ samples:" << std::endl;

    for (size_t i = 0; i < samples_to_print; ++i)
    {
        std::cout << "    [" << i << "] " << buff[i].real() << " + " << buff[i].imag() << "j" << std::endl;
    }

    return EXIT_SUCCESS;
}