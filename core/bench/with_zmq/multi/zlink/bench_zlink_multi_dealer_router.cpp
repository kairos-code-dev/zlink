#include "../common/bench_multi_client.hpp"
#include "../common/bench_multi_server.hpp"
#include <cstring>

int main (int argc, char **argv)
{
    const multi_pattern_config_t cfg = multi_pattern_config_for_name ("MULTI_DEALER_ROUTER");
    for (int i = 4; i < argc; ++i) {
        if (std::strcmp (argv[i], "--server-only") == 0)
            return run_multi_server_main (argc, argv, cfg);
    }
    return run_multi_client_main (argc, argv, cfg);
}
