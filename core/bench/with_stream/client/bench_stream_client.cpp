#include "../../../perf/common/streamclient/perf_stream_arg_reader.hpp"
#include "../../../perf/common/streamclient/perf_stream_bench_client.hpp"
#include "../../../perf/common/streamclient/perf_stream_client_options.hpp"
#include "../../../perf/common/streamclient/perf_stream_common.hpp"

#include <cstdio>

namespace {

bool parse_with_stream_options (int argc, char **argv, client_options_t &opt)
{
    const arg_reader_t args (argc, argv);

    opt.transport = "tcp";
    opt.pattern = "STREAM";
    opt.host = args.get_string ("--host", opt.host.c_str ());
    opt.port = args.get_int ("--port", opt.port, 1);
    opt.ccu = args.get_int ("--ccu", opt.ccu, 1);
    opt.runs = args.get_int ("--runs", opt.runs, 1);
    opt.warmup = args.get_int ("--warmup", opt.warmup, 0);
    opt.duration = args.get_int ("--duration", opt.duration, 1);
    opt.io_threads = args.get_int ("--io-threads", opt.io_threads, 1);
    opt.drain_ms = 0;
    opt.print_perf_result = 0;
    opt.send_stop_token = 0;

    const std::string sizes_text = args.get_string ("--sizes", "");
    if (!sizes_text.empty ()) {
        if (!perf_stream_common::perf_stream_parse_size_list (sizes_text, opt.sizes)) {
            std::fprintf (stderr, "invalid --sizes: %s\n", sizes_text.c_str ());
            return false;
        }
    }

    if (opt.sizes.empty ())
        opt.sizes.push_back (64);
    return true;
}

} // namespace

int main (int argc, char **argv)
{
    if (argc <= 1) {
        std::printf ("bench_stream_client: no args -> skip\n");
        return 0;
    }

    client_options_t opt;
    if (!parse_with_stream_options (argc, argv, opt))
        return 2;

    bench_client_t app (opt);
    return app.run ();
}
