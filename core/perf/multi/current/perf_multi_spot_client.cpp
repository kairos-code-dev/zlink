#include "../common/perf_multi_entry.hpp"
#include "../../../bench/with_zmq/multi/common/bench_multi_client.hpp"

int main (int argc, char **argv)
{
    set_perf_multi_pattern_env ("MULTI_SPOT");
    const multi_pattern_config_t cfg = multi_pattern_config_for_name ("MULTI_SPOT");
    return run_multi_client_main (argc, argv, cfg);
}
