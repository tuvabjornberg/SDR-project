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

class Transmitter {
  public:
    Transmitter();
    Transmitter(uhd::usrp::multi_usrp::sptr usrp);

    size_t send(std::vector<std::complex<float>> samples);

  private:
    uhd::usrp::multi_usrp::sptr m_usrp;
    uhd::tx_streamer::sptr m_tx_streamer;
};
