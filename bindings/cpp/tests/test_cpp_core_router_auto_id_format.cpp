#include "test_helpers.hpp"

#include <cstring>

int main ()
{
    zlink::context_t ctx;
    zlink::socket_t server (ctx, zlink::socket_type::router);
    zlink::socket_t client (ctx, zlink::socket_type::dealer);

    const int zero = 0;
    assert (server.set (zlink::socket_option::linger, zero) == 0);
    assert (client.set (zlink::socket_option::linger, zero) == 0);

    const std::string endpoint =
      endpoint_for (transport_case_t{"tcp", ""}, "router-auto-id-format");
    assert (server.bind (endpoint) == 0);
    assert (client.connect (endpoint) == 0);

    const char payload[] = "hello";
    assert (client.send (payload, sizeof (payload) - 1) == 5);

    zlink::message_t route_from_client;
    zlink::message_t body;
    assert (recv_msg_with_timeout (server, route_from_client, 2000) >= 0);
    assert (recv_msg_with_timeout (server, body, 2000) >= 0);

    assert (route_from_client.size () == 16);
    assert (body.size () == sizeof (payload) - 1);
    assert (std::memcmp (body.data (), payload, sizeof (payload) - 1) == 0);

    return 0;
}
