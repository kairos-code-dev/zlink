#include "perf_multi_common.hpp"

int perf_multi_router_router_server (const std::string &transport, size_t size)
{
    return perf_multi::run_generic_server ("MULTI_ROUTER_ROUTER", transport, size, true);
}
