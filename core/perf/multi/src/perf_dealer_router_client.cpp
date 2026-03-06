#include "../common/perf_entry.hpp"
#include "../common/perf_client_helpers.hpp"

#include <cstdlib>
#include <iostream>
#include <string>

namespace {

static const char *k_pattern = "DEALER_ROUTER";
static const int k_client_socket_type = ZLINK_DEALER;
static const bool k_client_router_send = false;
static const char *k_server_routing_id = "SERVER";

} // namespace

int main (int argc, char **argv)
{
    if (argc < 4)
        return 1;

    const std::string lib_name = argv[1];
    const std::string transport = argv[2];
    const size_t fallback_size =
      static_cast<size_t> (std::strtoull (argv[3], NULL, 10));

    std::string endpoint;
    if (!perf_client::parse_endpoint_arg (argc, argv, &endpoint)) {
        std::cerr << "missing --endpoint" << std::endl;
        return 1;
    }

    return perf_client::run_echo_client_benchmark (
      k_pattern,
      k_client_socket_type,
      k_client_router_send,
      k_server_routing_id,
      lib_name,
      transport,
      endpoint,
      fallback_size);
}
