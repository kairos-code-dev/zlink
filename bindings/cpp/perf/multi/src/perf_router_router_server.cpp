// ROUTER-ROUTER multi server benchmark: routed echo responder.
// Topology: client ROUTER(connect, N) <-> server ROUTER(bind, routing_id=SERVER)
// Measurement role: echo payload to sender routing id and emit queue metrics.

#include "../common/perf_common.hpp"
#include "../common/perf_entry.hpp"

#include <cerrno>
#include <cstring>
#include <vector>

namespace {

bool perf_debug_enabled ()
{
    return std::getenv ("PERF_DEBUG") != NULL;
}

zlink::routing_id_t routing_id_from_ascii (const char *value_)
{
    return zlink::routing_id_t::from_bytes (
      reinterpret_cast<const uint8_t *> (value_), std::strlen (value_));
}

void debug_log (const std::string &message_)
{
    if (!perf_debug_enabled ())
        return;
    std::cerr << "router_router server: " << message_ << std::endl;
}

bool take_router_payload (std::vector<zlink::message_t> &parts,
                          zlink::message_t &payload)
{
    if (parts.empty ()) {
        payload = zlink::message_t (0);
        return payload.valid ();
    }

    if (parts.size () == 1) {
        payload = std::move (parts[0]);
        return true;
    }

    if (parts.size () == 2 && parts[0].size () == 0) {
        payload = std::move (parts[1]);
        return true;
    }

    errno = EPROTO;
    return false;
}

} // namespace

bool perf_router_router_server (const std::string &lib_name,
                                const std::string &transport,
                                size_t msg_size)
{
    perf::multi::set_perf_pattern_env ("ROUTER_ROUTER");

    if (!perf::multi::is_supported_transport (transport)) {
        std::cout << "UNSUPPORTED," << lib_name << ",MULTI_ROUTER_ROUTER,"
                  << transport
                  << std::endl;
        return true;
    }

    const perf::multi::multi_bench_settings_t settings =
      perf::multi::resolve_multi_bench_settings ();

    perf::multi::ctx_guard_t ctx;
    zlink::router_socket_t server (ctx.ctx ());

    if (perf_debug_enabled ()) {
        int type = 0;
        if (perf::multi::get_common_socket_option (
              server, zlink::compat::options::socket_options::type, type)
            == 0)
            debug_log ("socket type=" + std::to_string (type));
        else
            debug_log ("socket type read failed errno=" + std::to_string (errno));
    }

    server.set_routing_id (routing_id_from_ascii ("SERVER"));
    perf::multi::apply_benchmark_socket_options (
      server, settings, transport);
    if (!perf::multi::apply_benchmark_auto_hwm_msg_unit_typed (server, msg_size))
        return false;
    if (!perf::multi::setup_tls_server (server, transport))
        return false;

    const std::string endpoint = perf::multi::bind_and_resolve_endpoint (
      server, transport, "cpp_multi_router_router", settings.server_bind_port);
    if (endpoint.empty ())
        return false;

    const bench_multi_cpu_sample_t resource_probe_start =
      perf::multi::start_resource_probe ();
    perf::multi::print_ready (endpoint);

    bool stop_requested = false;
    bool failed = false;
    while (!stop_requested) {
        std::optional<zlink::received_t> maybe_received;
        try {
            maybe_received = server.recv ();
        }
        catch (const zlink::recv_error_t &err) {
            if (err.internal_errno () == EINTR)
                continue;
            debug_log ("receive failed errno="
                       + std::to_string (err.internal_errno ()));
            failed = true;
            break;
        }
        if (!maybe_received.has_value ())
            continue;

        zlink::message_t payload;
        if (maybe_received->is_single_part ())
            payload = std::move (maybe_received->first_part ());
        else if (!take_router_payload (maybe_received->parts (), payload)) {
            const int err = errno;
            if (err == EPROTO)
                continue;
            failed = true;
            break;
        }

        if (perf::multi::is_stop_token (payload.data (), payload.size ())) {
            stop_requested = true;
            break;
        }
        if (payload.size () == 0)
            continue;

        const std::optional<zlink::routing_id_t> &client_rid =
          maybe_received->routing_id ();
        if (!client_rid.has_value ()) {
            debug_log ("missing client routing id");
            failed = true;
            break;
        }

        try {
            if (!server.send (*client_rid, payload)) {
                debug_log ("send blocked");
                failed = true;
                break;
            }
        }
        catch (const zlink::submit_error_t &err) {
            debug_log ("send failed errno="
                       + std::to_string (err.internal_errno ()));
            failed = true;
            break;
        }
    }

    const bench_multi_resource_metrics_t resource_metrics =
      perf::multi::finish_resource_probe (resource_probe_start);
    perf::multi::print_server_resource_metrics (
      lib_name,
      "MULTI_ROUTER_ROUTER",
      transport,
      msg_size,
      resource_metrics);
    perf::multi::print_server_queue_metrics (
      lib_name,
      "MULTI_ROUTER_ROUTER",
      transport,
      msg_size,
      perf::multi::server_queue_stats_t ());
    return !failed;
}

int main (int argc, char **argv)
{
    if (argc < 4) {
        std::cerr << "usage: <lib_name> <transport> <size>" << std::endl;
        return 1;
    }

    const std::string lib_name = argv[1];
    const std::string transport = argv[2];
    const size_t size = static_cast<size_t> (std::strtoull (argv[3], NULL, 10));
    if (size == 0)
        return 1;

    return perf_router_router_server (lib_name, transport, size) ? 0 : 1;
}
