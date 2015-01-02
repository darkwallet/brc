#include "util.hpp"

#include <boost/lexical_cast.hpp>

const std::string listen_transport(const size_t port)
{
    std::string transport = "tcp://*:";
    transport += boost::lexical_cast<std::string>(port);
    return transport;
}

