#include "perf_multi_common.hpp"

int perf_multi_router_router_client (const std::string &transport,
                                size_t size,
                                const std::string &endpoint)
{
    return perf_multi::run_generic_client ("MULTI_ROUTER_ROUTER", transport, size, endpoint);
}
