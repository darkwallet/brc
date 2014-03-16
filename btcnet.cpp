#include "btcnet.hpp"

using std::placeholders::_1;
using std::placeholders::_2;
using std::placeholders::_3;

void log_to_file(std::ofstream& file,
    bc::log_level level, const std::string& domain, const std::string& body)
{
    if (body.empty())
        return;
    file << level_repr(level);
    if (!domain.empty())
        file << " [" << domain << "]";
    file << ": " << body << std::endl;
}
void log_to_both(std::ostream& device, std::ofstream& file,
    bc::log_level level, const std::string& domain, const std::string& body)
{
    if (body.empty())
        return;
    std::ostringstream output;
    output << level_repr(level);
    if (!domain.empty())
        output << " [" << domain << "]";
    output << ": " << body;
    device << output.str() << std::endl;
    file << output.str() << std::endl;
}

void output_file(std::ofstream& file, bc::log_level level,
    const std::string& domain, const std::string& body)
{
    log_to_file(file, level, domain, body);
}
void output_both(std::ofstream& file, bc::log_level level,
    const std::string& domain, const std::string& body)
{
    log_to_both(std::cout, file, level, domain, body);
}

void error_file(std::ofstream& file, bc::log_level level,
    const std::string& domain, const std::string& body)
{
    log_to_file(file, level, domain, body);
}
void error_both(std::ofstream& file, bc::log_level level,
    const std::string& domain, const std::string& body)
{
    log_to_both(std::cerr, file, level, domain, body);
}

broadcaster::broadcaster()
  : hosts_(pool_), handshake_(pool_), network_(pool_),
    broadcast_p2p_(
        pool_, hosts_, handshake_, network_)
{
}

void broadcaster::start(size_t threads,
    size_t broadcast_hosts, start_handler handle_start)
{
    outfile_.open("debug.log");
    errfile_.open("error.log");
    // Suppress noisy output.
    bc::log_debug().set_output_function(
        std::bind(output_file, std::ref(outfile_), _1, _2, _3));
    bc::log_info().set_output_function(
        std::bind(output_both, std::ref(outfile_), _1, _2, _3));
    bc::log_warning().set_output_function(
        std::bind(error_file, std::ref(errfile_), _1, _2, _3));
    bc::log_error().set_output_function(
        std::bind(error_both, std::ref(errfile_), _1, _2, _3));
    bc::log_fatal().set_output_function(
        std::bind(error_both, std::ref(errfile_), _1, _2, _3));
    // Begin initialization.
    pool_.spawn(threads);
    // Set connection counts.
    broadcast_p2p_.set_max_outbound(broadcast_hosts);
    // Start connecting to p2p network for broadcasting txs.
    broadcast_p2p_.start(handle_start);
}
void broadcaster::stop()
{
    pool_.stop();
    pool_.join();
}

bool broadcaster::broadcast(const bc::data_chunk& raw_tx)
{
    bc::transaction_type tx;
    try
    {
        satoshi_load(raw_tx.begin(), raw_tx.end(), tx);
    }
    catch (bc::end_of_stream)
    {
        return false;
    }
    bc::log_info() << "Sending: " << bc::hash_transaction(tx);
    // We can ignore the send since we have connections to monitor
    // from the network and resend if neccessary anyway.
    auto ignore_send = [](const std::error_code&, size_t) {};
    broadcast_p2p_.broadcast(tx, ignore_send);
    return true;
}

