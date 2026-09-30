#include <iostream>
#include <vector>
#include <complex>
#include <cmath>
#include <uhd/usrp/multi_usrp.hpp>
#include <uhd/utils/thread.hpp>

int main(int argc, char *argv[])
{
    std::cout << "[+] Searching for USRP B210..." << std::endl;

    // 1. Instantiate the USRP device (type=b200 catches B200/B210)
    uhd::usrp::multi_usrp::sptr usrp;
    try
    {
        usrp = uhd::usrp::multi_usrp::make("type=b200");
    }
    catch (const std::exception &e)
    {
        std::cerr << "[-] Error initializing USRP: " << e.what() << std::endl;
        std::cerr << "    Check USB 3.0 connection or run 'uhd_find_devices'." << std::endl;
        return EXIT_FAILURE;
    }

    std::cout << "[+] Connected to: " << usrp->get_pp_string() << std::endl;

    // 2. Configure RF Parameters
    double target_freq = 2.45e9; // 2.45 GHz (ISM band)
    double sample_rate = 1e6;    // 1 MSps (1 MHz bandwidth)
    double gain = 30.0;          // 30 dB RX Gain

    std::cout << "[+] Setting Sample Rate to " << sample_rate / 1e6 << " MSps..." << std::endl;
    usrp->set_rx_rate(sample_rate);

    std::cout << "[+] Tuning Center Frequency to " << target_freq / 1e6 << " MHz..." << std::endl;
    usrp->set_rx_freq(uhd::tune_request_t(target_freq));

    std::cout << "[+] Setting RX Gain to " << gain << " dB..." << std::endl;
    usrp->set_rx_gain(gain);

    // 3. Create RX Streamer (Complex Float CPU -> 16-bit Wire Format)
    uhd::stream_args_t stream_args("fc32", "sc16");
    uhd::rx_streamer::sptr rx_stream = usrp->get_rx_stream(stream_args);

    // 4. Setup Buffer & Issue Stream Command
    const size_t num_samples = 1024;
    std::vector<std::complex<float>> buff(num_samples);

    uhd::stream_cmd_t stream_cmd(uhd::stream_cmd_t::STREAM_MODE_NUM_SAMPS_AND_DONE);
    stream_cmd.num_samps = num_samples;
    stream_cmd.stream_now = true;
    rx_stream->issue_stream_cmd(stream_cmd);

    // 5. Receive IQ Samples
    uhd::rx_metadata_t md;
    size_t num_rx_samps = rx_stream->recv(&buff[0], num_samples, md, 3.0 /* timeout in sec */);

    // 6. Handle Output & Calculate Received Signal Power
    if (md.error_code == uhd::rx_metadata_t::ERROR_CODE_NONE)
    {
        std::cout << "\n[+] SUCCESS! Received " << num_rx_samps << " samples." << std::endl;

        // Calculate average magnitude square (power)
        double total_power = 0.0;
        for (const auto &sample : buff)
        {
            total_power += std::norm(sample); // |I + jQ|^2
        }
        double avg_power = total_power / num_rx_samps;

        std::cout << "[+] Average Signal Power: " << avg_power << " (Linear)" << std::endl;
        std::cout << "[+] First 5 Raw IQ Samples (I, Q):" << std::endl;
        for (size_t i = 0; i < 5; ++i)
        {
            std::cout << "    Sample [" << i << "]: "
                      << buff[i].real() << " + " << buff[i].imag() << "j" << std::endl;
        }
    }
    else
    {
        std::cerr << "[-] Error receiving samples: " << md.strerror() << std::endl;
        return EXIT_FAILURE;
    }

    return EXIT_SUCCESS;
}