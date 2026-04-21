#include "test_helpers.hpp"

namespace {

void test_ws_pair_message ()
{
    if (!zlink::has ("ws"))
        return;

    zlink::context_t ctx;
    zlink::pair_socket_t server (ctx);
    zlink::pair_socket_t client (ctx);

    const int zero = 0;
    assert (server.set_option (zlink::socket_option::linger, zero) == 0);
    assert (client.set_option (zlink::socket_option::linger, zero) == 0);

    const std::string endpoint = endpoint_for (transport_case_t{"ws", ""},
                                               "zmp-ws");
    server.bind (endpoint);
    client.connect (endpoint);

    send_string_expect_success (client, "ws-zmp");
    recv_string_expect_success (server, "ws-zmp");
}

} // namespace

int main ()
{
    test_ws_pair_message ();
    return 0;
}
