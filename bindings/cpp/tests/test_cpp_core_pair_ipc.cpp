#include "test_helpers.hpp"

#include <cerrno>
#include <string>

int main ()
{
#if defined(ZLINK_HAVE_IPC)
    {
        zlink::context_t ctx;
        zlink::pair_socket_t server (ctx);
        zlink::pair_socket_t client (ctx);

        assert (server.bind ("ipc://*") == 0);
        const std::string endpoint = bound_endpoint (server);
        assert (!endpoint.empty ());
        assert (client.connect (endpoint) == 0);
        bounce (server, client);
    }

    {
        zlink::context_t ctx;
        zlink::pair_socket_t server (ctx);
        std::string endpoint_too_long = "ipc://";
        endpoint_too_long.append (108, 'a');
        assert (server.bind (endpoint_too_long) == -1);
        assert (zlink_errno () == ENAMETOOLONG);
    }
#endif
    return 0;
}
