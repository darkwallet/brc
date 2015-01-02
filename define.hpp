#ifndef BRC_DEFINE_HPP
#define BRC_DEFINE_HPP

const size_t push_transaction_port = 9109;
const size_t publish_connections_count_port = 9112;
const size_t publish_summary_port = 9110;

// How many connections broadcaster should try to maintain.
const size_t target_connections = 40;

#define LOG_BRC "broadcaster"

#endif

