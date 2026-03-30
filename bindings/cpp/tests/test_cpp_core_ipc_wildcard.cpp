#include "test_helpers.hpp"

int main ()
{
#if defined(ZLINK_HAVE_IPC)
    zlink::context_t ctx;
    zlink::pair_socket_t server (ctx);
    zlink::pair_socket_t client (ctx);

    assert (server.bind ("ipc://*") == 0);
    std::string endpoint = bound_endpoint (server);
    assert (!endpoint.empty ());
    assert (client.connect (endpoint) == 0);

    bounce (server, client);
#endif
    return 0;
}
