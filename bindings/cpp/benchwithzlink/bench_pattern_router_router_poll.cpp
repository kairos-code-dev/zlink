#include <string>

#include "pattern_dispatch.hpp"

int run_pattern_router_router_poll(const std::string &transport, size_t size)
{
    return run_router_router(transport, size, true);
}
