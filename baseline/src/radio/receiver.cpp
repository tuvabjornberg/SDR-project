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

Receiver::Receiver(uhd::usrp::multi_usrp::sptr usrp) : m_usrp(std::move(usrp)) {

    const size_t rx_chan = 1;

    m_usrp->set_rx_rate(RX_RATE, rx_chan);
    m_usrp->set_rx_freq(RX_FREQ, rx_chan);
    m_usrp->set_rx_gain(RX_GAIN, rx_chan);
    m_usrp->set_rx_bandwidth(RX_BANDWIDTH, rx_chan);
    m_usrp->set_rx_antenna(RX_ANT, rx_chan);
    // m_usrp->set_rx_dc_offset(false, rx_chan);
    // m_usrp->set_rx_iq_balance(false, rx_chan);

    std::cout << "RX rate: " << m_usrp->get_rx_rate(rx_chan) << '\n';
    std::cout << "RX freq: " << m_usrp->get_rx_freq(rx_chan) << '\n';
    std::cout << "RX gain: " << m_usrp->get_rx_gain(rx_chan) << '\n';
    std::cout << "RX antenna: " << m_usrp->get_rx_antenna(rx_chan) << '\n';

    uhd::stream_args_t stream_args("fc32", "sc16");
    stream_args.channels = {rx_chan};
    m_rx_streamer = m_usrp->get_rx_stream(stream_args);
}

RxChunk Receiver::receive(std::size_t num_samples) {
    RxChunk chunk;
    chunk.samples.resize(num_samples);

    uhd::rx_metadata_t md;

    const double timeout = 1.0;

    const std::size_t num_received =
        m_rx_streamer->recv(chunk.samples.data(), chunk.samples.size(), md, 1.0);

    switch (md.error_code) {
    case uhd::rx_metadata_t::ERROR_CODE_NONE:
        break;

    case uhd::rx_metadata_t::ERROR_CODE_OVERFLOW:
        std::cerr << "[WARNING] RX overflow: samples were lost\n";
        return {};

    case uhd::rx_metadata_t::ERROR_CODE_TIMEOUT:
        std::cerr << "[WARNING] RX timeout\n";
        return {};

    default:
        throw std::runtime_error("RX error: " + md.strerror());
    }

    chunk.samples.resize(num_received);
    chunk.timestamp = md.time_spec.get_real_secs();
    return chunk;
}

void Receiver::start() {
    uhd::stream_cmd_t stream_cmd(uhd::stream_cmd_t::STREAM_MODE_START_CONTINUOUS);

    stream_cmd.stream_now = true;

    m_rx_streamer->issue_stream_cmd(stream_cmd);
}

void Receiver::stop() {
    uhd::stream_cmd_t stream_cmd(uhd::stream_cmd_t::STREAM_MODE_STOP_CONTINUOUS);

    m_rx_streamer->issue_stream_cmd(stream_cmd);
}
