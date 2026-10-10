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

class Receiver {
  public:
    Receiver();

    std::vector<std::complex<float>> receive(std::size_t num_samples);

    void start();

  private:
    uhd::usrp::multi_usrp::sptr m_usrp;
    uhd::rx_streamer::sptr m_rx_streamer;
};
