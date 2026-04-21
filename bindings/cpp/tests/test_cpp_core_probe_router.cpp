#include "test_helpers.hpp"

#include <cstring>

namespace {

void test_probe_router_router ()
{
    zlink::context_t ctx;
    zlink::router_socket_t server (ctx);
    zlink::router_socket_t client (ctx);

    const std::string endpoint =
      endpoint_for (transport_case_t{"tcp", ""}, "probe-router-router");
    server.bind (endpoint);

    assert (client.set_option (zlink::socket_option::routing_id, "X", 1) == 0);
    const int probe = 1;
    assert (client.set_option (zlink::socket_option::probe_router, probe) == 0);
    client.connect (endpoint);

    recv_string_expect_success (server, "X");
    unsigned char buffer[255];
    std::memset (buffer, 0, sizeof (buffer));
    assert (recv_with_timeout (server, buffer, sizeof (buffer), 2000) == 0);

    send_string_expect_success (server, "X", zlink::send_flag::sndmore);
    send_string_expect_success (server, "Hello");

    zlink::message_t route;
    assert (recv_msg_with_timeout (client, route, 2000) >= 0);
    assert (route.size () == 16);
    recv_string_expect_success (client, "Hello");
}

void test_probe_router_dealer ()
{
    zlink::context_t ctx;
    zlink::router_socket_t server (ctx);
    zlink::dealer_socket_t client (ctx);

    const std::string endpoint =
      endpoint_for (transport_case_t{"tcp", ""}, "probe-router-dealer");
    server.bind (endpoint);

    assert (client.set_option (zlink::socket_option::routing_id, "X", 1) == 0);
    const int probe = 1;
    assert (client.set_option (zlink::socket_option::probe_router, probe) == 0);
    client.connect (endpoint);

    recv_string_expect_success (server, "X");
    unsigned char buffer[255];
    std::memset (buffer, 0, sizeof (buffer));
    assert (recv_with_timeout (server, buffer, sizeof (buffer), 2000) >= 0);

    send_string_expect_success (server, "X", zlink::send_flag::sndmore);
    send_string_expect_success (server, "Hello");
    recv_string_expect_success (client, "Hello");
}

} // namespace

int main ()
{
    test_probe_router_router ();
    test_probe_router_dealer ();
    return 0;
}
