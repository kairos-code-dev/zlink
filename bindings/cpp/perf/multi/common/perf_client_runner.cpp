#include "perf_client_helpers.hpp"

#include <cstdlib>
#include <iostream>
#include <string>

#ifndef RUN_MULTI_CLIENT_FN
#error "RUN_MULTI_CLIENT_FN must be defined"
#endif

void RUN_MULTI_CLIENT_FN (const std::string &transport,
                          size_t size,
                          const std::string &endpoint);

int main (int argc, char **argv)
{
    if (argc < 3) {
        std::cerr << "usage: <transport> <size> --endpoint <endpoint>" << std::endl;
        return 1;
    }

    const std::string transport = argv[1];
    const size_t size = static_cast<size_t> (std::strtoull (argv[2], NULL, 10));
    if (size == 0)
        return 1;

    const std::string endpoint = perf::multi::parse_endpoint_arg (argc, argv);
    if (endpoint.empty ()) {
        std::cerr << "missing --endpoint" << std::endl;
        return 1;
    }

    RUN_MULTI_CLIENT_FN (transport, size, endpoint);
    return 0;
}
