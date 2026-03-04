#include "test_helpers.hpp"

namespace {

void test_ws_pair_message ()
{
    if (!zlink::has ("ws"))
        return;

    zlink::context_t ctx;
    zlink::socket_t server (ctx, zlink::socket_type::pair);
    zlink::socket_t client (ctx, zlink::socket_type::pair);

    const int zero = 0;
    assert (server.set (zlink::socket_option::linger, zero) == 0);
    assert (client.set (zlink::socket_option::linger, zero) == 0);

    const std::string endpoint = endpoint_for (transport_case_t{"ws", ""},
                                               "zmp-ws");
    assert (server.bind (endpoint) == 0);
    assert (client.connect (endpoint) == 0);

    send_string_expect_success (client, "ws-zmp");
    recv_string_expect_success (server, "ws-zmp");
}

} // namespace

int main ()
{
    test_ws_pair_message ();
    return 0;
}
