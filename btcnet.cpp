#include "btcnet.hpp"

#include <czmq++/czmq.hpp>

#define LOG_BRC "broadcaster"

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

void warning(std::ofstream& file, bc::log_level level,
    const std::string& domain, const std::string& body)
{
    if (domain == LOG_BRC)
        log_to_both(std::cerr, file, level, domain, body);
    else
        log_to_file(file, level, domain, body);
}
void error(std::ofstream& file, bc::log_level level,
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
        std::bind(warning, std::ref(errfile_), _1, _2, _3));
    bc::log_error().set_output_function(
        std::bind(error, std::ref(errfile_), _1, _2, _3));
    bc::log_fatal().set_output_function(
        std::bind(error, std::ref(errfile_), _1, _2, _3));
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

void send_error_message(const bc::hash_digest& tx_hash, const std::string& err)
{
    bc::log_warning(LOG_BRC) << "Rejected: " << err;
    // Create ZMQ socket.
    static czmqpp::context ctx;
    static czmqpp::socket socket(ctx, ZMQ_PUB);
    static bool is_initialized = false;
    if (!is_initialized)
    {
        bc::log_debug(LOG_BRC) << "Initializing errors socket.";
        BITCOIN_ASSERT(ctx.self());
        BITCOIN_ASSERT(socket.self());
        int bind_rc = socket.bind("tcp://*:9110");
        BITCOIN_ASSERT(bind_rc != -1);
        is_initialized = true;
        sleep(1);
    }
    // Create a message.
    czmqpp::message msg;
    // tx_hash
    czmqpp::data_chunk data_hash(tx_hash.begin(), tx_hash.end());
    msg.append(data_hash);
    // Error message
    czmqpp::data_chunk data_err(err.begin(), err.end());
    msg.append(data_err);
    // Send it.
    bool rc = msg.send(socket);
    BITCOIN_ASSERT(rc);
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
        bc::log_warning(LOG_BRC) << "Bad stream.";
        std::error_code ec = bc::error::bad_stream;
        send_error_message(bc::null_hash, ec.message());
        return false;
    }
    bc::hash_digest tx_hash = bc::hash_transaction(tx);
    bc::log_info(LOG_BRC) << "Sending: " << tx_hash;
    // We can ignore the send since we have connections to monitor
    // from the network and resend if neccessary anyway.
    auto ignore_send = [tx_hash](const std::error_code& ec, size_t)
    {
        if (ec)
            send_error_message(tx_hash, ec.message());
    };
    broadcast_p2p_.broadcast(tx, ignore_send);
    return true;
}

size_t broadcaster::total_connections() const
{
    return broadcast_p2p_.total_connections();
}

