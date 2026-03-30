#include "test_helpers.hpp"

namespace {

void test_reconnect_ivl_against_pair_socket (const std::string &endpoint_,
                                             zlink::context_t &ctx_,
                                             zlink::pair_socket_t &server_,
                                             bool ipv6_ = false)
{
    zlink::pair_socket_t client (ctx_);
    if (ipv6_)
        assert (client.set_option (zlink::socket_option::ipv6, 1) == 0);

    const int interval = -1;
    assert (client.set_option (zlink::socket_option::reconnect_ivl, interval) == 0);
    assert (client.connect (endpoint_) == 0);

    bounce (server_, client);

    assert (server_.unbind (endpoint_.c_str ()) == 0);
    expect_bounce_fail (server_, client);

    assert (server_.bind (endpoint_) == 0);
    expect_bounce_fail (server_, client);

    assert (client.connect (endpoint_) == 0);
    bounce (server_, client);
}

void test_reconnect_ivl_tcp_ipv4 ()
{
    zlink::context_t ctx;
    zlink::pair_socket_t server (ctx);

    assert (server.bind ("tcp://127.0.0.1:*") == 0);
    const std::string endpoint = bound_endpoint (server);

    test_reconnect_ivl_against_pair_socket (endpoint, ctx, server);
}

void test_reconnect_ivl_tcp_ipv6 ()
{
    zlink::context_t ctx;
    zlink::pair_socket_t server (ctx);
    assert (server.set_option (zlink::socket_option::ipv6, 1) == 0);

    if (server.bind ("tcp://[::1]:*") != 0)
        return;
    const std::string endpoint = bound_endpoint (server);

    test_reconnect_ivl_against_pair_socket (endpoint, ctx, server, true);
}

} // namespace

int main ()
{
    test_reconnect_ivl_tcp_ipv4 ();
    test_reconnect_ivl_tcp_ipv6 ();
    return 0;
}
