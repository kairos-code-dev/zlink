// GATEWAY benchmark: one-way gateway->receiver service message flow.
// Topology: gateway(service client) -> receiver(service provider router)

#include "../common/perf_single_common.hpp"

#include <algorithm>
#include <cerrno>
#include <chrono>
#include <thread>
#include <vector>

#if defined(_WIN32)
#include <process.h>
#else
#include <unistd.h>
#endif

namespace {

int bench_pid ()
{
#if defined(_WIN32)
    return _getpid ();
#else
    return getpid ();
#endif
}

bool configure_receiver_tls (zlink::service::receiver_t &receiver,
                             const std::string &transport)
{
    if (transport != "tls" && transport != "wss")
        return true;

    std::string cert;
    std::string key;
    std::string ca;
    if (!perf::single::try_resolve_perf_tls_paths (cert, key, ca))
        return false;

    return receiver.set_tls_server (cert, key) == 0;
}

bool configure_gateway_tls (zlink::service::gateway_t &gateway,
                            const std::string &transport)
{
    if (transport != "tls" && transport != "wss")
        return true;

    std::string cert;
    std::string key;
    std::string ca;
    if (!perf::single::try_resolve_perf_tls_paths (cert, key, ca))
        return false;

    return gateway.set_tls_client (ca, "localhost", 0) == 0;
}

std::string bind_receiver_endpoint (zlink::service::receiver_t &receiver,
                                    const std::string &transport,
                                    int base_port)
{
    for (int i = 0; i < 64; ++i) {
        const std::string endpoint =
          perf::single::make_fixed_endpoint (transport, base_port + i);
        if (receiver.bind (endpoint) == 0)
            return endpoint;
    }
    return std::string ();
}

bool wait_discovery_ready (zlink::service::discovery_t &discovery,
                           const std::string &service,
                           int timeout_ms)
{
    const auto deadline = std::chrono::steady_clock::now ()
                          + std::chrono::milliseconds (timeout_ms);
    while (std::chrono::steady_clock::now () < deadline) {
        if (discovery.service_available (service) > 0)
            return true;
        std::this_thread::sleep_for (std::chrono::milliseconds (2));
    }
    return false;
}

bool wait_gateway_ready (zlink::service::gateway_t &gateway,
                         const std::string &service,
                         int timeout_ms)
{
    const auto deadline = std::chrono::steady_clock::now ()
                          + std::chrono::milliseconds (timeout_ms);
    while (std::chrono::steady_clock::now () < deadline) {
        if (gateway.connection_count (service) > 0)
            return true;
        std::this_thread::sleep_for (std::chrono::milliseconds (2));
    }
    return false;
}

int recv_provider_header (zlink::socket_t &provider_router,
                          size_t payload_size,
                          zlink::recv_flag flags,
                          perf_single_metric::header_t *header_out,
                          bool *header_ok_out)
{
    if (header_ok_out)
        *header_ok_out = false;

    zlink::message_t routing_id;
    const int id_rc = provider_router.recv (routing_id, flags);
    if (id_rc < 0) {
        const int err = errno;
        if (err == EAGAIN || err == EINTR)
            return 0;
        return -1;
    }

    if (!routing_id.more ())
        return -1;

    zlink::message_t payload;
    if (provider_router.recv (payload, zlink::recv_flag::none) < 0)
        return -1;

    if (payload.more () || payload.size () != payload_size)
        return -1;

    bool header_ok = false;
    if (header_out) {
        header_ok = perf_single_metric::decode_payload_header (
          payload.data (), payload.size (), header_out);
    }

    if (header_ok_out)
        *header_ok_out = header_ok;
    return 1;
}

bool run_phase (zlink::service::gateway_t &gateway,
                const std::string &service,
                zlink::socket_t &provider_router,
                std::vector<char> &payload,
                size_t msg_size,
                uint32_t run_id,
                uint64_t &seq,
                perf_single_metric::phase_t phase,
                int warmup_count,
                int duration_s,
                perf::single::queue_probe_t *queue_probe,
                unsigned long long *received_out,
                perf::single::latency_stats_t *latency_out)
{
    if (!received_out)
        return false;

    const bool active = phase == perf_single_metric::phase_active;
    const size_t payload_size = payload.size ();

    perf::single::latency_stats_builder_t latency_builder (
      perf::single::resolve_single_latency_sample_cap ());
    unsigned long long received = 0;

    auto do_one = [&] () -> bool {
        const uint64_t sent_ts = perf_single_metric::now_us ();
        if (!perf_single_metric::stamp_payload (payload.data (),
                                                payload_size,
                                                run_id,
                                                phase,
                                                msg_size,
                                                seq++,
                                                sent_ts)
            || gateway.send (service,
                             payload.data (),
                             payload_size,
                             zlink::send_flag::none)
                 != 0) {
            return false;
        }
        if (queue_probe)
            queue_probe->sample_send_if_due ();

        perf_single_metric::header_t header;
        bool header_ok = false;
        const int recv_rc = recv_provider_header (
          provider_router,
          payload_size,
          zlink::recv_flag::none,
          &header,
          &header_ok);
        if (recv_rc <= 0)
            return false;

        if (queue_probe)
            queue_probe->sample_recv_if_due ();

        if (header_ok && perf_single_metric::is_expected (
                           header, run_id, phase, msg_size)) {
            ++received;
            if (active) {
                const uint64_t now = perf_single_metric::now_us ();
                const double latency_us =
                  now >= header.sent_ts_us
                    ? static_cast<double> (now - header.sent_ts_us)
                    : 0.0;
                latency_builder.add (latency_us);
            }
        }

        return true;
    };

    if (active) {
        const auto deadline =
          std::chrono::steady_clock::now ()
          + std::chrono::seconds (duration_s > 0 ? duration_s : 1);
        while (std::chrono::steady_clock::now () < deadline) {
            if (!do_one ())
                return false;
        }
    } else {
        for (int i = 0; i < warmup_count; ++i) {
            if (!do_one ())
                return false;
        }
    }

    if (queue_probe) {
        queue_probe->force_sample_send ();
        queue_probe->force_sample_recv ();
    }

    if (active) {
        if (received == 0 || latency_builder.count () == 0 || !latency_out)
            return false;
        *latency_out = latency_builder.snapshot ();
    }

    *received_out = received;
    return true;
}

} // namespace

void run_pattern_gateway (const std::string &transport,
                          size_t msg_size,
                          const std::string &lib_name)
{
    if (transport == "inproc" || transport == "ipc") {
        std::cout << "UNSUPPORTED,GATEWAY," << transport << std::endl;
        return;
    }
    if (!perf::single::transport_available (transport)) {
        std::cout << "UNSUPPORTED,GATEWAY," << transport << std::endl;
        return;
    }

    perf::single::ctx_guard_t ctx;
    if (!ctx.valid ()) {
        perf::single::print_fail_result (lib_name, "GATEWAY", transport, msg_size);
        return;
    }

    const std::string suffix =
      lib_name + "_gw_" + std::to_string (perf_single_metric::now_us ());
    const std::string reg_pub = "inproc://cpp_gw_pub_" + suffix;
    const std::string reg_router = "inproc://cpp_gw_router_" + suffix;
    const std::string service_name = "svc";

    zlink::service::registry_t registry (ctx.ctx ());
    if (!registry.valid () || registry.set_endpoints (reg_pub, reg_router) != 0
        || registry.start () != 0) {
        perf::single::print_fail_result (lib_name, "GATEWAY", transport, msg_size);
        return;
    }

    zlink::service::discovery_t discovery (ctx.ctx (),
                                           zlink::service_type::gateway);
    if (!discovery.valid () || discovery.connect_registry (reg_pub) != 0
        || discovery.subscribe (service_name) != 0) {
        perf::single::print_fail_result (lib_name, "GATEWAY", transport, msg_size);
        return;
    }

    zlink::service::gateway_t gateway (ctx.ctx (), discovery);
    zlink::service::receiver_t receiver (ctx.ctx ());
    if (!gateway.valid () || !receiver.valid ()) {
        perf::single::print_fail_result (lib_name, "GATEWAY", transport, msg_size);
        return;
    }

    const int sndhwm = perf::single::resolve_single_socket_hwm (true);
    const int rcvhwm = perf::single::resolve_single_socket_hwm (false);
    const int send_timeout = perf::single::resolve_single_send_timeout_ms ();
    const int recv_timeout = perf::single::resolve_single_recv_timeout_ms ();

    (void) gateway.set_sockopt (zlink::socket_options::sndhwm, sndhwm);
    (void) gateway.set_sockopt (zlink::socket_options::rcvhwm, rcvhwm);
    (void) gateway.set_sockopt (zlink::socket_options::sndtimeo, send_timeout);
    (void) gateway.set_sockopt (zlink::socket_options::rcvtimeo, recv_timeout);

    (void) receiver.set_sockopt (
      zlink::receiver_socket_role::router,
      zlink::socket_options::sndhwm,
      sndhwm);
    (void) receiver.set_sockopt (
      zlink::receiver_socket_role::router,
      zlink::socket_options::rcvhwm,
      rcvhwm);
    (void) receiver.set_sockopt (
      zlink::receiver_socket_role::router,
      zlink::socket_options::sndtimeo,
      send_timeout);
    (void) receiver.set_sockopt (
      zlink::receiver_socket_role::router,
      zlink::socket_options::rcvtimeo,
      recv_timeout);

    if (!configure_receiver_tls (receiver, transport)
        || !configure_gateway_tls (gateway, transport)) {
        perf::single::print_fail_result (lib_name, "GATEWAY", transport, msg_size);
        return;
    }

    const int base_port = 37000 + (bench_pid () % 1000) * 8;
    const std::string provider_endpoint =
      bind_receiver_endpoint (receiver, transport, base_port);
    if (provider_endpoint.empty ()
        || receiver.connect_registry (reg_router) != 0
        || receiver.register_service (service_name, provider_endpoint, 1) != 0) {
        perf::single::print_fail_result (lib_name, "GATEWAY", transport, msg_size);
        return;
    }

    if (!wait_discovery_ready (discovery, service_name, 3000)
        || !wait_gateway_ready (gateway, service_name, 3000)) {
        perf::single::print_fail_result (lib_name, "GATEWAY", transport, msg_size);
        return;
    }

    zlink::socket_t gateway_router =
      zlink::socket_t::wrap (gateway.router_handle ());
    zlink::socket_t provider_router =
      zlink::socket_t::wrap (receiver.router_handle ());

    perf::single::queue_probe_t queue_probe (&gateway_router, &provider_router);

    const size_t payload_size =
      std::max<size_t> (msg_size, perf_single_metric::header_size ());
    std::vector<char> payload (payload_size, 'a');

    const uint32_t run_id = static_cast<uint32_t> (perf_single_metric::now_us ());
    uint64_t seq = 1;

    const int warmup_count =
      perf::single::resolve_bench_count ("PERF_WARMUP_COUNT", 200);
    unsigned long long warmup_received = 0;
    if (!run_phase (gateway,
                    service_name,
                    provider_router,
                    payload,
                    msg_size,
                    run_id,
                    seq,
                    perf_single_metric::phase_warmup,
                    warmup_count,
                    0,
                    NULL,
                    &warmup_received,
                    NULL)) {
        perf::single::print_fail_result (
          lib_name, "GATEWAY", transport, msg_size, &queue_probe);
        return;
    }

    const int duration_s = std::max (1, perf::single::resolve_single_duration_seconds ());
    unsigned long long received = 0;
    perf::single::latency_stats_t latency;
    if (!run_phase (gateway,
                    service_name,
                    provider_router,
                    payload,
                    msg_size,
                    run_id,
                    seq,
                    perf_single_metric::phase_active,
                    0,
                    duration_s,
                    &queue_probe,
                    &received,
                    &latency)) {
        perf::single::print_fail_result (
          lib_name, "GATEWAY", transport, msg_size, &queue_probe);
        return;
    }

    const double throughput =
      static_cast<double> (received) / static_cast<double> (duration_s);
    perf::single::print_result (lib_name,
                                "GATEWAY",
                                transport,
                                msg_size,
                                throughput,
                                latency.mean_us,
                                latency.p95_us,
                                latency.p99_us,
                                queue_probe.snapshot ());
}
