#define main zlink_cpp_binding_multi_stream_len32be_core_main
#include "../../../../../core/perf/multi/src/perf_stream_len32be_server.cpp"
#undef main

int main (int argc, char **argv)
{
    if (argc < 2)
        return 1;

    char current_name[] = "current";
    char *forward_argv[] = {argv[0], current_name, argv[1]};
    return zlink_cpp_binding_multi_stream_len32be_core_main (3, forward_argv);
}
