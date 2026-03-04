#include "test_helpers.hpp"

int main ()
{
    zlink::context_t ctx;
    zlink::socket_t server (ctx, zlink::socket_type::pair);
    zlink::socket_t client (ctx, zlink::socket_type::pair);

    int tos = 0x28;
    size_t tos_size = sizeof (tos);
    assert (server.set (zlink::socket_option::tos, &tos, tos_size) == 0);

    const std::string endpoint =
      endpoint_for (transport_case_t{"tcp", ""}, "diffserv");
    assert (server.bind (endpoint) == 0);

    int out_tos = 0;
    assert (zlink_getsockopt (server.handle (), ZLINK_TOS, &out_tos, &tos_size) == 0);
    assert (out_tos == tos);

    tos = 0x58;
    tos_size = sizeof (tos);
    assert (client.set (zlink::socket_option::tos, &tos, tos_size) == 0);
    assert (client.connect (endpoint) == 0);

    out_tos = 0;
    assert (zlink_getsockopt (client.handle (), ZLINK_TOS, &out_tos, &tos_size) == 0);
    assert (out_tos == tos);

    bounce (server, client);
    return 0;
}
