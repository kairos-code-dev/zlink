#include "../common/bench_multi_server.hpp"

int main (int argc, char **argv)
{
    const multi_pattern_config_t cfg = multi_pattern_config_for_name ("MULTI_STREAM_CALLBACK");
    return run_multi_server_main (argc, argv, cfg);
}
