#include "perf_dispatch.hpp"

int run_pattern_stream_callback (const std::string &transport, size_t size)
{
    return run_stream_variant (transport, size, true, "STREAM_CALLBACK",
                               "stream-callback");
}
