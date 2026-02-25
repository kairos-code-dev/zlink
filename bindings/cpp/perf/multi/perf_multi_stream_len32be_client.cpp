#include "perf_multi_common.hpp"

int perf_multi_stream_len32be_client (const std::string &transport,
                                size_t size,
                                const std::string &endpoint)
{
    return perf_multi::run_generic_client ("MULTI_STREAM_LEN32BE", transport, size, endpoint);
}
