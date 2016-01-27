#include <future>
#include <boost/filesystem.hpp>
#include <signal.h>
#include <bitcoin/bitcoin.hpp>
#include <czmq++/czmqpp.hpp>
#include "btcnet.hpp"

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

void keep_pushing_count(broadcaster& brc)
{
    // Create ZMQ socket.
    czmqpp::context ctx;
    BITCOIN_ASSERT(ctx.self());
    czmqpp::authenticator auth(ctx);
#ifdef ONLY_LOCALHOST_CONNECTIONS
    auth.allow("127.0.0.1");
#endif
    czmqpp::socket socket(ctx, ZMQ_PUB);
    BITCOIN_ASSERT(socket.self());
    int bind_rc = socket.bind(
        listen_transport(publish_connections_count_port));
    BITCOIN_ASSERT(bind_rc != -1);
    while (true)
    {
        czmqpp::message msg;
        const auto data = bc::to_little_endian(brc.total_connections());
        msg.append(bc::to_data_chunk(data));
        // Send it.
        bool rc = msg.send(socket);
        BITCOIN_ASSERT(rc);
        std::this_thread::sleep_for(std::chrono::milliseconds(500));
    }
}

void send_error_message(const bc::hash_digest& tx_hash, const std::string& err);

int main(int argc, char** argv)
{
    // Used for casting the char** string to std::string.
    auto is_help = [](const std::string arg)
    {
        return arg == "-h" || arg == "--help";
    };
    if (argc > 1 && is_help(argv[1]))
    {
        log_info() << "Usage: brc [ZMQ_TRANSPORT] [CLIENT_CERTS_DIR]";
        log_info() << "Example: brc tcp://*:8989";
        log_info() << "An empty CLIENT_CERTS_DIR will accept all certificates.";
        log_info() << "See ZMQ manual for more info.";
        return 0;
    }
    if (argc > 3)
    {
        log_fatal() << "brc: Too many arguments.";
        return -1;
    }
    std::string zmq_transport = listen_transport(push_transaction_port);
    std::string client_certs_dir;
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
    brc.start(4, target_connections, broadcaster_started);
    std::error_code ec = ec_promise.get_future().get();
    if (ec)
    {
        log_fatal() << "Problem starting broadcaster: " << ec.message();
        brc.stop();
        return -1;
    }
    log_info() << "Node started.";
    // TODO: BUG handler doesn't get called.
    signal(SIGINT, interrupt_handler);
    // ZMQ stuff
    czmqpp::context ctx;
    BITCOIN_ASSERT(ctx.self());
    // Start the authenticator and tell it do authenticate clients
    // via the certificates stored in the .curve directory.
    czmqpp::authenticator auth(ctx);
    auth.set_verbose(true);
#ifdef ONLY_LOCALHOST_CONNECTIONS
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
    czmqpp::socket server(ctx, ZMQ_REP);
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
        brc.stop();
        return -1;
    }
    czmqpp::poller poller(server);
    BITCOIN_ASSERT(poller.self());
    // Thread which keeps publishing the connection count.
    std::thread thread([&brc]() { keep_pushing_count(brc); });
    while (!stopped)
    {
        czmqpp::socket sock = poller.wait(poll_sleep_interval);
        if (!sock.self())
            continue;
        // Receive message.
        czmqpp::message msg;
        msg.receive(sock);
        BITCOIN_ASSERT(msg.parts().size() == 1);
        const bc::data_chunk& raw_tx = msg.parts()[0];
        bool success = brc.broadcast(raw_tx);
        if (!success)
            log_warning() << "Invalid Tx received: "
                << bc::encode_base16(raw_tx);
        // Respond back.
        czmqpp::message response_msg;
        // If successful respond with 0x01, otherwise return 0x00
        constexpr uint8_t response_true = 0x01;
        constexpr uint8_t response_false = 0x00;
        czmqpp::data_chunk response{{
            success ? response_true : response_false}};
        response_msg.append(response);
        bool rc = response_msg.send(sock);
        BITCOIN_ASSERT(rc);
    }
    thread.detach();
    brc.stop();
    return 0;
}

