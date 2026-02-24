#include "../common/perf_multi_entry.hpp"
#include "../../../bench/with_zmq/multi/common/bench_multi_server.hpp"

int main (int argc, char **argv)
{
    set_perf_multi_pattern_env ("MULTI_PUBSUB");
    const multi_pattern_config_t cfg = multi_pattern_config_for_name ("MULTI_PUBSUB");
    return run_multi_server_main (argc, argv, cfg);
}
