#include "test_helpers.hpp"

namespace {

void test_bind_after_connect_tcp ()
{
    zlink::context_t ctx;
    zlink::dealer_socket_t server (ctx);
    zlink::dealer_socket_t client (ctx);

    const std::string endpoint =
      endpoint_for (transport_case_t{"tcp", ""}, "bind-after-connect");

    client.connect (endpoint);

    send_string_expect_success (client, "foobar");
    send_string_expect_success (client, "baz");
    send_string_expect_success (client, "buzz");

    server.bind (endpoint);

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
