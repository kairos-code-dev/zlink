#include "test_helpers.hpp"

#include <cerrno>
#include <cstring>

int main ()
{
#if defined(ZLINK_HAVE_IPC) && !defined(ZLINK_HAVE_WINDOWS)
    {
        zlink::context_t ctx;
        zlink::dealer_socket_t server (ctx);
        zlink::dealer_socket_t client (ctx);

        const char *endpoint = "ipc://@tmp-tester";
        server.bind (endpoint);

        std::string last = bound_endpoint (server);
        assert (last.compare (0, std::strlen (endpoint), endpoint) == 0);

        client.connect (endpoint);
        bounce (server, client);
    }

    {
        zlink::context_t ctx;
        zlink::dealer_socket_t server (ctx);
        try {
            server.bind ("ipc://@");
            assert (false);
        } catch (const zlink::bind_error_t &) {
        }
        assert (zlink_errno () == EINVAL);
    }
#endif
    return 0;
}
