#include "test_helpers.hpp"

int main ()
{
#if defined(ZLINK_HAVE_IPC)
    zlink::context_t ctx;
    zlink::socket_t server (ctx, zlink::socket_type::pair);
    zlink::socket_t client (ctx, zlink::socket_type::pair);

    assert (server.bind ("ipc://*") == 0);
    std::string endpoint = bound_endpoint (server);
    assert (!endpoint.empty ());
    assert (client.connect (endpoint) == 0);

    bounce (server, client);
#endif
    return 0;
}
