#include "test_helpers.hpp"

namespace {

void test_pair_tcp_regular ()
{
    zlink::context_t ctx;
    zlink::pair_socket_t server (ctx);
    zlink::pair_socket_t client (ctx);

    server.bind ("tcp://127.0.0.1:*");
    const std::string endpoint = bound_endpoint (server);
    client.connect (endpoint);

    bounce (server, client);
}

void test_pair_tcp_connect_by_name ()
{
    zlink::context_t ctx;
    zlink::pair_socket_t server (ctx);
    zlink::pair_socket_t client (ctx);

    server.bind ("tcp://127.0.0.1:*");
    const std::string endpoint = bound_endpoint (server);

    const std::size_t pos = endpoint.rfind (':');
    assert (pos != std::string::npos);
    const std::string connect_endpoint = "tcp://localhost" + endpoint.substr (pos);
    client.connect (connect_endpoint);

    bounce (server, client);
}

} // namespace

int main ()
{
    test_pair_tcp_regular ();
    test_pair_tcp_connect_by_name ();
    return 0;
}
