#include "../common/perf_multi_entry.hpp"
#include "../common/perf_common.hpp"
#include "../common/perf_common_multi.hpp"
#include "../common/perf_multi_client_helpers.hpp"
#include "../../../bench/with_zmq/multi/common/bench_multi_resource.hpp"

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <string>
#include <vector>

namespace {

static const char *k_pattern = "MULTI_PUBSUB";
static const int k_client_socket_type = ZLINK_SUB;
static const uint32_t k_metric_run_id = 1U;

using perf_multi_client::close_client_monitors;
using perf_multi_client::close_client_sockets;
using perf_multi_client::is_supported_transport;
using perf_multi_client::parse_endpoint_arg;
using perf_multi_client::print_client_result_lines;
using perf_multi_client::resolve_case_msg_sizes;
using perf_multi_client::wait_all_client_connect_ready;

inline bool create_client_sockets (
  ctx_guard_t &ctx,
  const std::string &transport,
  const std::string &endpoint,
  const multi_bench_settings_t &settings,
  std::vector<void *> *sockets_out,
  std::vector<connect_monitor_t> *monitors_out)
{
    return perf_multi_client::create_client_sockets (
      ctx,
      transport,
      endpoint,
      settings,
      k_client_socket_type,
      sockets_out,
      monitors_out);
}

inline bool run_single_size_case (const std::vector<void *> &sockets,
                                  const multi_bench_settings_t &base_settings,
                                  size_t scratch_capacity,
                                  const std::string &lib_name,
                                  const std::string &transport,
                                  size_t msg_size)
{
    double throughput = 0.0;
    bench_latency_stats_t latency;
    bench_multi_resource_metrics_t metrics;
    if (!perf_multi_client::run_one_way_duration (
          sockets,
          base_settings,
          msg_size,
          k_metric_run_id,
          scratch_capacity,
          &throughput,
          &latency,
          &metrics)) {
        return false;
    }

    print_client_result_lines (
      k_pattern,
      lib_name,
      transport,
      msg_size,
      throughput,
      latency,
      metrics);

    return true;
}

inline int run_client_benchmark (const std::string &lib_name,
                                 const std::string &transport,
                                 const std::string &endpoint,
                                 size_t fallback_size)
{
    set_perf_multi_pattern_env (k_pattern);

    if (!is_supported_transport (transport)) {
        std::cout << "UNSUPPORTED," << lib_name << "," << k_pattern << ","
                  << transport << std::endl;
        return 0;
    }

    if (!transport_available (transport)) {
        std::cerr << "transport unavailable: " << transport << std::endl;
        return 1;
    }

    const multi_bench_settings_t base_settings = resolve_multi_bench_settings ();
    const std::vector<size_t> msg_sizes = resolve_case_msg_sizes (fallback_size);

    ctx_guard_t ctx;
    if (!ctx.valid ())
        return 1;

    std::vector<void *> sockets;
    std::vector<connect_monitor_t> monitors;
    if (!create_client_sockets (
          ctx,
          transport,
          endpoint,
          base_settings,
          &sockets,
          &monitors)) {
        close_client_monitors (&monitors);
        close_client_sockets (&sockets);
        return 1;
    }

    if (!wait_all_client_connect_ready (
          monitors,
          base_settings.connect_ready_timeout_ms)) {
        close_client_monitors (&monitors);
        close_client_sockets (&sockets);
        return 1;
    }
    close_client_monitors (&monitors);

    const size_t scratch_capacity = static_cast<size_t> (64);

    for (size_t si = 0; si < msg_sizes.size (); ++si) {
        const size_t msg_size = msg_sizes[si];
        if (!run_single_size_case (
              sockets,
              base_settings,
              scratch_capacity,
              lib_name,
              transport,
              msg_size)) {
            close_client_sockets (&sockets);
            return 1;
        }

    }
    close_client_sockets (&sockets);
    return 0;
}

} // namespace

int main (int argc, char **argv)
{
    if (argc < 4)
        return 1;

    const std::string lib_name = argv[1];
    const std::string transport = argv[2];
    const size_t fallback_size =
      static_cast<size_t> (std::strtoull (argv[3], NULL, 10));

    std::string endpoint;
    if (!parse_endpoint_arg (argc, argv, &endpoint)) {
        std::cerr << "missing --endpoint" << std::endl;
        return 1;
    }

    return run_client_benchmark (lib_name, transport, endpoint, fallback_size);
}
