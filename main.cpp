#include <future>
#include <boost/filesystem.hpp>
#include <signal.h>
#include <bitcoin/bitcoin.hpp>
#include <czmq++/czmq.hpp>
#include "btcnet.hpp"

//#define CRYPTO_ENABLED
#define ONLY_LOCALHOST_CONNECTIONS

using bc::log_info;
using bc::log_warning;
using bc::log_fatal;

constexpr const char* server_cert_filename = "brc_server_cert";
// Milliseconds
constexpr long poll_sleep_interval = 1000;

bool stopped = false;
void interrupt_handler(int)
{
    log_info() << "Stopping... Please wait.";
    stopped = true;
}

void create_cert_if_not_exists(const std::string& filename)
{
    if (boost::filesystem::exists(filename))
        return;
    czmqpp::certificate cert = czmqpp::new_cert();
    cert.save(filename);
    log_info() << "Created new certificate file named " << filename;
    log_info() << "Public key: " << cert.public_text();
}

int main(int argc, char** argv)
{
    if (argc > 1 && (argv[1] == "-h" || argv[1] == "--help"))
    {
        log_info() << "Usage: brc [ZMQ_TRANSPORT] [CLIENT_CERTS_DIR]";
        log_info() << "Example: brc tcp://*:8989";
        log_info() << "An empty CLIENT_CERTS_DIR will accept all certificates.";
        log_info() << "See ZMQ manual for more info.";
        return 0;
    }
    if (argc > 3)
    {
        log_fatal() << "brc: Wrong number of arguments.";
        return -1;
    }
    std::string zmq_transport = "tcp://*:9109", client_certs_dir;
    if (argc == 2)
        zmq_transport = argv[1];
    if (argc == 3)
        client_certs_dir = argv[2];
    // libbitcoin stuff
    broadcaster brc;
    std::promise<std::error_code> ec_promise;
    auto broadcaster_started =
        [&ec_promise](const std::error_code& ec)
        {
            ec_promise.set_value(ec);
        };
    brc.start(4, 40, broadcaster_started);
    std::error_code ec = ec_promise.get_future().get();
    if (ec)
    {
        log_fatal() << "Problem starting broadcaster: " << ec.message();
        return -1;
    }
    log_info() << "Node started.";
    // TODO: BUG handler doesn't get called.
    signal(SIGINT, interrupt_handler);
    // ZMQ stuff
    czmqpp::context ctx;
    assert(ctx.self());
    // Start the authenticator and tell it do authenticate clients
    // via the certificates stored in the .curve directory.
    czmqpp::authenticator auth(ctx);
    auth.set_verbose(true);
#ifdef ONLY_LOCALHOST_CONNECTIONS
    // Comment below line to allow connections outside of localhost.
    // i.e if you deploy it on another machine.
    auth.allow("127.0.0.1");
#endif
#ifdef CRYPTO_ENABLED
    if (client_certs_dir.empty())
        auth.configure_curve("*", CURVE_ALLOW_ANY);
    else
        auth.configure_curve("*", client_certs_dir);
    create_cert_if_not_exists(server_cert_filename);
    czmqpp::certificate server_cert =
        czmqpp::load_cert(server_cert_filename);
#endif
    // Create socket.
    czmqpp::socket server(ctx, ZMQ_PULL);
    BITCOIN_ASSERT(server.self());
    server.set_linger(0);
#ifdef CRYPTO_ENABLED
    server_cert.apply(server);
    server.set_curve_server(1);
#else
    server.set_zap_domain("global");
#endif
    int rc = server.bind(zmq_transport);
    if (rc == -1)
    {
        log_fatal() << "Error initializing ZMQ socket: "
            << zmq_strerror(zmq_errno());
        return -1;
    }
    czmqpp::poller poller(server);
    BITCOIN_ASSERT(poller.self());
    while (!stopped)
    {
        czmqpp::socket sock = poller.wait(poll_sleep_interval);
        if (!sock.self())
            continue;
        czmqpp::message msg;
        msg.receive(sock);
        BITCOIN_ASSERT(msg.parts().size() == 1);
        const bc::data_chunk& raw_tx = msg.parts()[0];
        if (!brc.broadcast(raw_tx))
            log_warning() << "Invalid Tx received: " << raw_tx;
    }
    brc.stop();
    return 0;
}

