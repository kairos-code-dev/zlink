#include "../common/bench_multi_client.hpp"

int main (int argc, char **argv)
{
    const multi_pattern_config_t cfg = multi_pattern_config_for_name ("MULTI_DEALER_DEALER");
    return run_multi_client_main (argc, argv, cfg);
}
