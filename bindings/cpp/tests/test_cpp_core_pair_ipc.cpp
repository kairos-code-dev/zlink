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

        server.bind ("ipc://*");
        const std::string endpoint = bound_endpoint (server);
        assert (!endpoint.empty ());
        client.connect (endpoint);
        bounce (server, client);
    }

    {
        zlink::context_t ctx;
        zlink::pair_socket_t server (ctx);
        std::string endpoint_too_long = "ipc://";
        endpoint_too_long.append (108, 'a');
        try {
            server.bind (endpoint_too_long);
            assert (false);
        } catch (const zlink::bind_error_t &) {
        }
        assert (zlink_errno () == ENAMETOOLONG);
    }
#endif
    return 0;
}
