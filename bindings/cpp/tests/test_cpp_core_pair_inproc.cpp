#include "test_helpers.hpp"

#include <cstring>

namespace {

void test_roundtrip ()
{
    zlink::context_t ctx;
    zlink::pair_socket_t server (ctx);
    zlink::pair_socket_t client (ctx);

    const std::string endpoint = unique_inproc ("inproc://cpp-pair-", "inproc");
    assert (server.bind (endpoint) == 0);
    assert (client.connect (endpoint) == 0);

    bounce (server, client);
}

void test_send_multipart ()
{
    zlink::context_t ctx;
    zlink::pair_socket_t server (ctx);
    zlink::pair_socket_t client (ctx);

    const std::string endpoint =
      unique_inproc ("inproc://cpp-pair-mp-", "multipart");
    assert (server.bind (endpoint) == 0);
    assert (client.connect (endpoint) == 0);

    send_string_expect_success (server, "foo", zlink::send_flag::sndmore);
    send_string_expect_success (server, "foobar");

    recv_string_expect_success (client, "foo");
    assert (get_rcvmore (client) == 1);
    recv_string_expect_success (client, "foobar");
    assert (get_rcvmore (client) == 0);
}

} // namespace

int main ()
{
    test_roundtrip ();
    test_send_multipart ();
    return 0;
}
