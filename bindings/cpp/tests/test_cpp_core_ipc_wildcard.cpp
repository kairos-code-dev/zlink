#include "test_helpers.hpp"

int main ()
{
#if defined(ZLINK_HAVE_IPC)
    zlink::context_t ctx;
    zlink::pair_socket_t server (ctx);
    zlink::pair_socket_t client (ctx);

    server.bind ("ipc://*");
    std::string endpoint = bound_endpoint (server);
    assert (!endpoint.empty ());
    client.connect (endpoint);

    bounce (server, client);
#endif
    return 0;
}
