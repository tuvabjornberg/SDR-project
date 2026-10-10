#include "radio/receiver.h"

#include "common/config.h"

#include <stdexcept>
#include <string>

Receiver::Receiver() {
    std::cout << boost::format("Creating the usrp device with: %s...") % RX_SERIAL << std::endl;
    uhd::usrp::multi_usrp::sptr usrp = uhd::usrp::multi_usrp::make(RX_SERIAL);
    m_usrp = usrp;

    std::cout << boost::format("Lock mboard clocks: %f") % RX_REF << std::endl;
    usrp->set_clock_source(RX_REF);

    // always select the subdevice first, the channel mapping affects the other settings
    std::cout << boost::format("subdev set to: %f") % RX_SUBDEV << std::endl;
    usrp->set_rx_subdev_spec(RX_SUBDEV);
    std::cout << boost::format("Using Device: %s") % usrp->get_pp_string() << std::endl;

    usrp->set_rx_rate(RX_RATE);
    usrp->set_rx_freq(RX_FREQ);
    usrp->set_rx_gain(RX_GAIN);
    usrp->set_rx_bandwidth(RX_BANDWIDTH);
    usrp->set_rx_antenna(RX_ANT);

    std::cout << "RX rate: " << usrp->get_rx_rate() << std::endl;
    std::cout << "RX freq: " << usrp->get_rx_freq() << std::endl;
    std::cout << "RX gain: " << usrp->get_rx_gain() << std::endl;

    uhd::stream_args_t stream_args("fc32", "sc16");
    m_rx_streamer = usrp->get_rx_stream(stream_args);
}

std::vector<std::complex<float>> Receiver::receive(std::size_t num_samples) {
    std::vector<std::complex<float>> samples(num_samples);
    uhd::rx_metadata_t md;

    std::size_t num_received = m_rx_streamer->recv(samples.data(), samples.size(), md);

    if (md.error_code != uhd::rx_metadata_t::ERROR_CODE_NONE) {
        throw std::runtime_error("RX error: " + md.strerror());
    }

    samples.resize(num_received);

    return samples;
}

void Receiver::start() {
    uhd::stream_cmd_t stream_cmd(uhd::stream_cmd_t::STREAM_MODE_START_CONTINUOUS);

    stream_cmd.stream_now = true;

    m_rx_streamer->issue_stream_cmd(stream_cmd);
}
