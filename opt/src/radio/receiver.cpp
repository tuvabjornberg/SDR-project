#include "radio/receiver.h"

#include "common/config.h"

#include <iostream>
#include <stdexcept>
#include <string>

Receiver::Receiver(std::size_t num_rx_channels) : m_num_rx_channels(num_rx_channels) {
    uhd::usrp::multi_usrp::sptr usrp = uhd::usrp::multi_usrp::make(RX_SERIAL);
    m_usrp = usrp;

    std::cout << boost::format("Lock mboard clocks: %f") % RX_REF << std::endl;
    usrp->set_clock_source(RX_REF);

    // always select the subdevice first, the channel mapping affects the other settings
    std::cout << boost::format("subdev set to: %f") % RX_SUBDEV << std::endl;
    usrp->set_rx_subdev_spec(RX_SUBDEV);
    std::cout << boost::format("Using Device: %s") % usrp->get_pp_string() << std::endl;

    for (std::size_t ch = 0; ch < m_num_rx_channels; ++ch) {
        usrp->set_rx_rate(RX_RATE, ch);
        usrp->set_rx_freq(RX_FREQ, ch);
        usrp->set_rx_gain(RX_GAIN, ch);
        usrp->set_rx_bandwidth(RX_BANDWIDTH, ch);
        usrp->set_rx_antenna(RX_ANT, ch);

        std::cout << "RX channel " << ch << std::endl;
        std::cout << "RX rate: " << usrp->get_rx_rate() << std::endl;
        std::cout << "RX freq: " << usrp->get_rx_freq() << std::endl;
        std::cout << "RX gain: " << usrp->get_rx_gain() << std::endl;
    }

    uhd::stream_args_t stream_args("fc32", "sc16");

    std::vector<std::size_t> channels(m_num_rx_channels);
    for (std::size_t i = 0; i < m_num_rx_channels; ++i) {
        channels[i] = i;
    }
    stream_args.channels = channels;

    m_rx_streamer = m_usrp->get_rx_stream(stream_args);
}

Receiver::Receiver(uhd::usrp::multi_usrp::sptr usrp, std::size_t num_rx_channels)
    : m_usrp(std::move(usrp)), m_num_rx_channels(num_rx_channels) {
    m_usrp->set_rx_subdev_spec(RX_SUBDEV);

    for (std::size_t ch = 0; ch < m_num_rx_channels; ++ch) {
        m_usrp->set_rx_rate(RX_RATE, ch);
        m_usrp->set_rx_freq(RX_FREQ, ch);
        m_usrp->set_rx_gain(RX_GAIN, ch);
        m_usrp->set_rx_bandwidth(RX_BANDWIDTH, ch);
        m_usrp->set_rx_antenna(RX_ANT, ch);

        std::cout << "RX channel " << ch << std::endl;
        std::cout << "RX rate: " << m_usrp->get_rx_rate() << std::endl;
        std::cout << "RX freq: " << m_usrp->get_rx_freq() << std::endl;
        std::cout << "RX gain: " << m_usrp->get_rx_gain() << std::endl;
    }

    uhd::stream_args_t stream_args("fc32", "sc16");

    std::vector<std::size_t> channels(m_num_rx_channels);
    for (std::size_t i = 0; i < m_num_rx_channels; ++i) {
        channels[i] = i;
    }
    stream_args.channels = channels;

    m_rx_streamer = m_usrp->get_rx_stream(stream_args);
}

Receiver::Receiver(std::string serial, std::size_t num_rx_channels)
    : m_num_rx_channels(num_rx_channels) {
    uhd::usrp::multi_usrp::sptr usrp = uhd::usrp::multi_usrp::make("serial=" + serial);
    m_usrp = usrp;

    std::cout << boost::format("Lock mboard clocks: %f") % RX_REF << std::endl;
    usrp->set_clock_source(RX_REF);

    // always select the subdevice first, the channel mapping affects the other settings
    std::cout << boost::format("subdev set to: %f") % RX_SUBDEV << std::endl;
    usrp->set_rx_subdev_spec(RX_SUBDEV);
    std::cout << boost::format("Using Device: %s") % usrp->get_pp_string() << std::endl;

    for (std::size_t ch = 0; ch < m_num_rx_channels; ++ch) {
        usrp->set_rx_rate(RX_RATE, ch);
        usrp->set_rx_freq(RX_FREQ, ch);
        usrp->set_rx_gain(RX_GAIN, ch);
        usrp->set_rx_bandwidth(RX_BANDWIDTH, ch);
        usrp->set_rx_antenna(RX_ANT, ch);

        std::cout << "RX serial:    " << serial << std::endl;
        std::cout << "RX channel:   " << ch << std::endl;
        std::cout << "RX rate:      " << usrp->get_rx_rate() << std::endl;
        std::cout << "RX freq:      " << usrp->get_rx_freq() << std::endl;
        std::cout << "RX gain:      " << usrp->get_rx_gain() << std::endl;
    }

    uhd::stream_args_t stream_args("fc32", "sc16");

    std::vector<std::size_t> channels(m_num_rx_channels);
    for (std::size_t i = 0; i < m_num_rx_channels; ++i) {
        channels[i] = i;
    }
    stream_args.channels = channels;

    m_rx_streamer = m_usrp->get_rx_stream(stream_args);
}

RxChunk Receiver::receive(std::size_t num_samples) {
    RxChunk chunk;
    chunk.samples.resize(m_num_rx_channels);
    for (auto& channel : chunk.samples) {
        channel.resize(num_samples);
    }

    uhd::rx_metadata_t md;
    const double timeout = 1.0;

    std::vector<void*> buffs;
    buffs.reserve(m_num_rx_channels);
    for (auto& channel : chunk.samples) {
        buffs.push_back(channel.data());
    }

    const std::size_t num_received = m_rx_streamer->recv(buffs, num_samples, md, timeout);

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

    for (auto& channel : chunk.samples) {
        channel.resize(num_received);
    }

    chunk.timestamp = md.time_spec.get_real_secs();
    return chunk;
}

void Receiver::start() {
    uhd::stream_cmd_t stream_cmd(uhd::stream_cmd_t::STREAM_MODE_START_CONTINUOUS);

    stream_cmd.stream_now = false;
    stream_cmd.time_spec = m_usrp->get_time_now() + uhd::time_spec_t(0.1);

    m_rx_streamer->issue_stream_cmd(stream_cmd);
}

void Receiver::stop() {
    uhd::stream_cmd_t stream_cmd(uhd::stream_cmd_t::STREAM_MODE_STOP_CONTINUOUS);

    m_rx_streamer->issue_stream_cmd(stream_cmd);
}
