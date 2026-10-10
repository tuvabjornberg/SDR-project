#include "radio/transmitter.h"

#include "common/config.h"

#include <string>

Transmitter::Transmitter() {
    std::cout << boost::format("Creating the usrp device with: %s...") % TX_SERIAL << std::endl;
    uhd::usrp::multi_usrp::sptr usrp = uhd::usrp::multi_usrp::make(TX_SERIAL);
    m_usrp = usrp;

    std::cout << boost::format("Lock mboard clocks: %f") % TX_REF << std::endl;
    usrp->set_clock_source(TX_REF);

    // always select the subdevice first, the channel mapping affects the other settings
    std::cout << boost::format("subdev set to: %f") % TX_SUBDEV << std::endl;
    usrp->set_rx_subdev_spec(TX_SUBDEV);
    std::cout << boost::format("Using Device: %s") % usrp->get_pp_string() << std::endl;

    usrp->set_tx_rate(TX_RATE);
    usrp->set_tx_freq(TX_FREQ);
    usrp->set_tx_gain(TX_GAIN);
    usrp->set_tx_bandwidth(TX_BANDWIDTH);
    usrp->set_tx_antenna(TX_ANT);

    std::cout << "TX rate: " << usrp->get_tx_rate() << std::endl;
    std::cout << "TX freq: " << usrp->get_tx_freq() << std::endl;
    std::cout << "TX gain: " << usrp->get_tx_gain() << std::endl;

    uhd::stream_args_t stream_args("fc32", "sc16");
    m_tx_streamer = usrp->get_tx_stream(stream_args);
}

Transmitter::Transmitter(uhd::usrp::multi_usrp::sptr usrp) : m_usrp(std::move(usrp)) {
    m_usrp->set_tx_rate(TX_RATE);
    m_usrp->set_tx_freq(TX_FREQ);
    m_usrp->set_tx_gain(TX_GAIN);
    m_usrp->set_tx_bandwidth(TX_BANDWIDTH);
    m_usrp->set_tx_antenna(TX_ANT);

    std::cout << "TX rate: " << m_usrp->get_tx_rate() << '\n';
    std::cout << "TX freq: " << m_usrp->get_tx_freq() << '\n';
    std::cout << "TX gain: " << m_usrp->get_tx_gain() << '\n';
    std::cout << "TX antenna: " << m_usrp->get_tx_antenna() << '\n';

    uhd::stream_args_t stream_args("fc32", "sc16");
    m_tx_streamer = m_usrp->get_tx_stream(stream_args);
}

size_t Transmitter::send(std::vector<std::complex<float>> samples) {
    uhd::tx_metadata_t md;
    md.start_of_burst = true;
    md.end_of_burst = true;
    md.has_time_spec = false;

    return m_tx_streamer->send(samples.data(), samples.size(), md);
}