#include "test_helpers.hpp"

#include <cerrno>

namespace {

void test_zmp_metadata_enabled ()
{
    zlink::context_t ctx;
    zlink::pair_socket_t server (ctx);
    zlink::pair_socket_t client (ctx);

    const int enabled = 1;
    assert (client.set_option (zlink::socket_option::zmp_metadata, enabled) == 0);

    const std::string endpoint = endpoint_for (transport_case_t{"tcp", ""},
                                               "zmp-metadata-on");
    assert (server.bind (endpoint) == 0);
    assert (client.connect (endpoint) == 0);

    send_string_expect_success (client, "A");

    zlink::message_t msg;
    assert (recv_msg_with_timeout (server, msg, 2000) == 1);

    const char *sock_type = msg.get_property ("Socket-Type");
    assert (sock_type != NULL);
    assert (std::string (sock_type) == "PAIR");
}

void test_zmp_metadata_disabled ()
{
    zlink::context_t ctx;
    zlink::pair_socket_t server (ctx);
    zlink::pair_socket_t client (ctx);

    const int disabled = 0;
    assert (client.set_option (zlink::socket_option::zmp_metadata, disabled) == 0);

    const std::string endpoint = endpoint_for (transport_case_t{"tcp", ""},
                                               "zmp-metadata-off");
    assert (server.bind (endpoint) == 0);
    assert (client.connect (endpoint) == 0);

    send_string_expect_success (client, "B");

    zlink::message_t msg;
    assert (recv_msg_with_timeout (server, msg, 2000) == 1);

    errno = 0;
    const char *sock_type = msg.get_property ("Socket-Type");
    assert (sock_type == NULL);
    assert (zlink_errno () == EINVAL);
}

} // namespace

int main ()
{
    test_zmp_metadata_enabled ();
    test_zmp_metadata_disabled ();
    return 0;
}
