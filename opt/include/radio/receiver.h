#pragma once

#include <boost/format.hpp>
#include <boost/program_options.hpp>
#include <boost/thread.hpp>
#include <complex>
#include <cstddef>
#include <uhd/exception.hpp>
#include <uhd/types/tune_request.hpp>
#include <uhd/usrp/multi_usrp.hpp>
#include <uhd/utils/safe_main.hpp>
#include <uhd/utils/thread.hpp>
#include <vector>

struct RxChunk {
    double timestamp = 0.0;
    std::vector<std::vector<std::complex<float>>> samples;
};

class Receiver {
  public:
    Receiver(std::size_t num_rx_channels);
    Receiver(uhd::usrp::multi_usrp::sptr usrp, std::size_t num_rx_channels);
    Receiver(std::string serial, std::size_t num_rx_channels);

    RxChunk receive(std::size_t num_samples);

    void start();
    void stop();

  private:
    uhd::usrp::multi_usrp::sptr m_usrp;
    uhd::rx_streamer::sptr m_rx_streamer;
    size_t m_num_rx_channels;
};
