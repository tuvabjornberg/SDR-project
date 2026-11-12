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
    usrp->set_tx_subdev_spec(TX_SUBDEV);
    std::cout << boost::format("Using Device: %s") % usrp->get_pp_string() << std::endl;

    for (std::size_t ch = 0; ch < NUM_TX_CHANNELS; ++ch) {
        usrp->set_tx_rate(TX_RATE, ch);
        usrp->set_tx_freq(TX_FREQ, ch);
        usrp->set_tx_gain(TX_GAIN, ch);
        usrp->set_tx_bandwidth(TX_BANDWIDTH, ch);
        usrp->set_tx_antenna(TX_ANT, ch);

        std::cout << "TX channel " << ch << std::endl;
        std::cout << "TX rate: " << usrp->get_tx_rate() << std::endl;
        std::cout << "TX freq: " << usrp->get_tx_freq() << std::endl;
        std::cout << "TX gain: " << usrp->get_tx_gain() << std::endl;
        std::cout << "TX antenna: " << usrp->get_tx_antenna() << std::endl;
    }

    uhd::stream_args_t stream_args("fc32", "sc16");
    stream_args.channels = {0, 1};
    m_tx_streamer = usrp->get_tx_stream(stream_args);
}

Transmitter::Transmitter(uhd::usrp::multi_usrp::sptr usrp) : m_usrp(std::move(usrp)) {
    m_usrp->set_tx_subdev_spec(TX_SUBDEV);

    for (std::size_t ch = 0; ch < NUM_TX_CHANNELS; ++ch) {
        m_usrp->set_tx_rate(TX_RATE, ch);
        m_usrp->set_tx_freq(TX_FREQ, ch);
        m_usrp->set_tx_gain(TX_GAIN, ch);
        m_usrp->set_tx_bandwidth(TX_BANDWIDTH, ch);
        m_usrp->set_tx_antenna(TX_ANT, ch);

        std::cout << "TX channel " << ch << std::endl;
        std::cout << "TX rate: " << m_usrp->get_tx_rate() << std::endl;
        std::cout << "TX freq: " << m_usrp->get_tx_freq() << std::endl;
        std::cout << "TX gain: " << m_usrp->get_tx_gain() << std::endl;
        std::cout << "TX antenna: " << m_usrp->get_tx_antenna() << std::endl;
    }

    uhd::stream_args_t stream_args("fc32", "sc16");
    stream_args.channels = {0, 1};
    m_tx_streamer = m_usrp->get_tx_stream(stream_args);
}

// size_t Transmitter::send(const std::vector<std::complex<float>>& samples_tx0,
//                          const std::vector<std::complex<float>>& samples_tx1) {
//     if (samples_tx0.size() != samples_tx1.size()) {
//         throw std::invalid_argument("MIMO TX channels must have equal sample counts");
//     }

//     uhd::tx_metadata_t md;
//     md.start_of_burst = true;
//     md.end_of_burst = true;
//     md.has_time_spec = false;

//     std::vector<const void*> buffs = {samples_tx0.data(), samples_tx1.data()};

//     return m_tx_streamer->send(buffs, samples_tx0.size(), md, 1.0);
// }

std::size_t Transmitter::send(const std::vector<std::complex<float>>& samples_tx0,
                              const std::vector<std::complex<float>>& samples_tx1) {

    if (samples_tx0.size() != samples_tx1.size()) {
        throw std::invalid_argument("MIMO TX channels must have equal sample counts");
    }

    const std::size_t total = samples_tx0.size();

    if (total == 0)
        return 0;

    std::size_t sent = 0;

    while (sent < total) {

        uhd::tx_metadata_t md;
        md.start_of_burst = (sent == 0);
        md.end_of_burst = false;
        // md.has_time_spec = false;

        md.has_time_spec = (sent == 0);
        if (sent == 0)
            md.time_spec = m_usrp->get_time_now() + uhd::time_spec_t(0.05);

        std::vector<const void*> buffs = {samples_tx0.data() + sent, samples_tx1.data() + sent};

        const std::size_t n = m_tx_streamer->send(buffs, total - sent, md, 1.0);

        if (n == 0) {
            throw std::runtime_error("TX returned zero samples");
        }

        sent += n;
    }

    // Explicitly terminate the burst.
    uhd::tx_metadata_t end_md;
    end_md.start_of_burst = false;
    end_md.end_of_burst = true;
    end_md.has_time_spec = false;

    std::vector<const void*> buffs = {samples_tx0.data() + total, samples_tx1.data() + total};

    m_tx_streamer->send(buffs, 0, end_md, 1.0);

    return sent;
}
