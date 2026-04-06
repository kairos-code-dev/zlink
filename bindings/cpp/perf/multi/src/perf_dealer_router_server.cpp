// DEALER-ROUTER multi server benchmark: echo responder.
// Topology: client DEALER(connect, N) <-> server ROUTER(bind, 1)
// Measurement role: receive request payload and echo same payload back.

#include "../common/perf_common.hpp"
#include "../common/perf_entry.hpp"

#include <cerrno>

namespace {

} // namespace

bool perf_dealer_router_server (const std::string &transport, size_t msg_size)
{
    perf::multi::set_perf_pattern_env ("DEALER_ROUTER");

    if (!perf::multi::is_supported_transport (transport)) {
        std::cout << "UNSUPPORTED,MULTI_DEALER_ROUTER," << transport << std::endl;
        return true;
    }

    const perf::multi::multi_bench_settings_t settings =
      perf::multi::resolve_multi_bench_settings ();

    perf::multi::ctx_guard_t ctx;
    perf::multi::socket_guard_t server (ctx, zlink::socket_type::router);
    if (!server.valid ())
        return false;

    perf::multi::apply_benchmark_socket_options (
      server.sock (), settings, transport);
    if (!perf::multi::setup_tls_server (server.sock (), transport))
        return false;

    const std::string endpoint = perf::multi::bind_and_resolve_endpoint (
      server.sock (), transport, "cpp_multi_dealer_router", settings.server_bind_port);
    if (endpoint.empty ())
        return false;

    const bench_multi_cpu_sample_t resource_probe_start =
      perf::multi::start_resource_probe ();
    perf::multi::print_ready (endpoint);

    bool stop_requested = false;
    bool failed = false;
    while (!stop_requested) {
        zlink::routing_id_t source_rid;
        zlink::message_t part;
        const int recv_rc = server.sock ().recv (source_rid, part);
        if (recv_rc < 0) {
            const int err = errno;
            if (err == EINTR || err == EAGAIN)
                continue;
            failed = true;
            break;
        }

        if (perf::multi::is_stop_token (part.data (), part.size ())) {
            stop_requested = true;
            break;
        }

        if (!part.valid ()) {
            zlink::message_t empty_part (0);
            if (!empty_part.valid ()) {
                failed = true;
                break;
            }

            const int send_rc = server.sock ().send (source_rid, empty_part);
            if (send_rc < 0) {
                const int err = errno;
                if (err == EINTR || err == EAGAIN)
                    continue;
                failed = true;
                break;
            }
            continue;
        }

        const int send_rc = server.sock ().send (source_rid, part);
        if (send_rc >= 0)
            continue;

        const int err = errno;
        if (err == EINTR || err == EAGAIN)
            continue;
        failed = true;
        break;
    }

    const bench_multi_resource_metrics_t resource_metrics =
      perf::multi::finish_resource_probe (resource_probe_start);
    perf::multi::print_server_resource_metrics (
      "current",
      "MULTI_DEALER_ROUTER",
      transport,
      msg_size,
      resource_metrics);
    perf::multi::print_server_queue_metrics (
      "current",
      "MULTI_DEALER_ROUTER",
      transport,
      msg_size,
      perf::multi::server_queue_stats_t ());
    return !failed;
}

int main (int argc, char **argv)
{
    if (argc < 3) {
        std::cerr << "usage: <transport> <size>" << std::endl;
        return 1;
    }

    const std::string transport = argv[1];
    const size_t size = static_cast<size_t> (std::strtoull (argv[2], NULL, 10));
    if (size == 0)
        return 1;

    return perf_dealer_router_server (transport, size) ? 0 : 1;
}
