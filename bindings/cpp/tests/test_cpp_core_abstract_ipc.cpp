#include "test_helpers.hpp"

#include <cerrno>
#include <cstring>

int main ()
{
#if defined(ZLINK_HAVE_IPC) && !defined(ZLINK_HAVE_WINDOWS)
    {
        zlink::context_t ctx;
        zlink::socket_t server (ctx, zlink::socket_type::dealer);
        zlink::socket_t client (ctx, zlink::socket_type::dealer);

        const char *endpoint = "ipc://@tmp-tester";
        assert (server.bind (endpoint) == 0);

        std::string last = bound_endpoint (server);
        assert (last.compare (0, std::strlen (endpoint), endpoint) == 0);

        assert (client.connect (endpoint) == 0);
        bounce (server, client);
    }

    {
        zlink::context_t ctx;
        zlink::socket_t server (ctx, zlink::socket_type::dealer);
        assert (server.bind ("ipc://@") == -1);
        assert (zlink_errno () == EINVAL);
    }
#endif
    return 0;
}
