#include "test_helpers.hpp"

namespace {

void test_bind_after_connect_tcp ()
{
    zlink::context_t ctx;
    zlink::dealer_socket_t server (ctx);
    zlink::dealer_socket_t client (ctx);

    const std::string endpoint =
      endpoint_for (transport_case_t{"tcp", ""}, "bind-after-connect");

    assert (client.connect (endpoint) == 0);

    send_string_expect_success (client, "foobar");
    send_string_expect_success (client, "baz");
    send_string_expect_success (client, "buzz");

    assert (server.bind (endpoint) == 0);

    recv_string_expect_success (server, "foobar");
    recv_string_expect_success (server, "baz");
    recv_string_expect_success (server, "buzz");
}

} // namespace

int main ()
{
    test_bind_after_connect_tcp ();
    return 0;
}
