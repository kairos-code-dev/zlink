#include "perf_multi_common.hpp"
#include "perf_multi_dispatch.hpp"

#include <cstdlib>
#include <cstring>
#include <string>

int main (int argc, char **argv)
{
    if (argc < 2)
        return 1;

    if (argc >= 5 && std::strcmp (argv[1], "--multi-server") == 0) {
        const std::string pattern = perf_multi::to_upper (argv[2]);
        const std::string transport = perf_multi::to_lower (argv[3]);
        const size_t size = static_cast<size_t> (std::strtoull (argv[4], NULL, 10));
        perf_multi_server_fn_t server_fn = resolve_multi_server_fn (pattern);
        if (!server_fn)
            return 2;
        return server_fn (transport, size > 0 ? size : 64);
    }

    if (argc >= 7 && std::strcmp (argv[1], "--multi-client") == 0) {
        const std::string pattern = perf_multi::to_upper (argv[2]);
        const std::string transport = perf_multi::to_lower (argv[3]);
        const size_t size = static_cast<size_t> (std::strtoull (argv[4], NULL, 10));
        std::string endpoint;
        for (int i = 5; i + 1 < argc; ++i) {
            if (std::strcmp (argv[i], "--endpoint") == 0) {
                endpoint = argv[i + 1];
                break;
            }
        }
        if (endpoint.empty ())
            return 1;

        perf_multi_client_fn_t client_fn = resolve_multi_client_fn (pattern);
        if (!client_fn)
            return 2;
        return client_fn (transport, size > 0 ? size : 64, endpoint);
    }

    return 1;
}
