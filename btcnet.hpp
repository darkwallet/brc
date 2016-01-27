#ifndef BRC_BTCNET_HPP
#define BRC_BTCNET_HPP

#include <functional>
#include <bitcoin/bitcoin.hpp>
#include "util.hpp"
#include "define.hpp"

class broadcaster
{
public:
    typedef std::function<void (const std::error_code&)> start_handler;

    broadcaster();

    void start(size_t threads,
        size_t broadcast_hosts, start_handler handle_start);
    void stop();

    bool broadcast(const bc::data_chunk& raw_tx);

    size_t total_connections() const;

private:
    std::ofstream outfile_, errfile_;
    bc::threadpool pool_;
    bc::network::hosts hosts_;
    bc::network::handshake handshake_;
    bc::network::peer network_;
    bc::network::protocol broadcast_p2p_;
};

#endif

