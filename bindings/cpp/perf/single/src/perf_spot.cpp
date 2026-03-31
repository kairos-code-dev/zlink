// SPOT benchmark: unified self-delivery spot callback path.

#include "../common/perf_single_common.hpp"
#include "../common/perf_single_runner.hpp"

#include <cstdlib>
#include <iostream>
#include <vector>

namespace {

const char *const k_topic = "bench";

bool perf_debug_enabled ()
{
    return std::getenv ("PERF_DEBUG") != NULL;
}

bool send_spot_payload (void *userdata_, const void *data_, size_t size_)
{
    zlink::service::spot_t *spot = static_cast<zlink::service::spot_t *> (userdata_);
    if (!spot)
        return false;

    zlink::message_t msg = zlink::message_t::from_bytes (data_, size_);
    return msg.valid () && spot->publish (k_topic, msg, zlink::send_flag::none) == 0;
}

} // namespace

void run_pattern_spot (const std::string &transport,
                       size_t msg_size,
                       const std::string &lib_name)
{
    if (transport != "tcp") {
        std::cout << "UNSUPPORTED,SPOT," << transport << std::endl;
        return;
    }

    perf::single::ctx_guard_t ctx;
    if (!ctx.valid ()) {
        if (perf_debug_enabled ())
            std::cerr << "spot: invalid context" << std::endl;
        perf::single::print_fail_result (lib_name, "SPOT", transport, msg_size);
        return;
    }

    zlink::service::spot_node_t node (ctx.ctx ());
    zlink::service::spot_t spot (node);
    if (!spot.valid ()) {
        if (perf_debug_enabled ())
            std::cerr << "spot: invalid service handle" << std::endl;
        perf::single::print_fail_result (lib_name, "SPOT", transport, msg_size);
        return;
    }

    zlink::service_monitor_handle_t sub_monitor (
      spot,
      zlink::service_monitor_event::spot_filter_applied
        | zlink::service_monitor_event::error);
    if (!sub_monitor.valid ()) {
        if (perf_debug_enabled ())
            std::cerr << "spot: invalid sub monitor" << std::endl;
        perf::single::print_fail_result (lib_name, "SPOT", transport, msg_size);
        return;
    }

    (void) spot.set (zlink::socket_options::sndhwm,
                     perf::single::resolve_single_socket_hwm (true));
    (void) spot.set (zlink::socket_options::rcvhwm,
                     perf::single::resolve_single_socket_hwm (false));
    (void) spot.set (zlink::socket_options::sndtimeo,
                     perf::single::resolve_single_send_timeout_ms ());
    (void) spot.set (zlink::socket_options::rcvtimeo,
                     perf::single::resolve_single_recv_timeout_ms ());

    if (spot.subscribe (k_topic) != 0
        || !perf::single::wait_service_monitor_event (
          sub_monitor,
          static_cast<uint32_t> (
            zlink::service_monitor_event::spot_filter_applied),
          -1,
          10000)) {
        if (perf_debug_enabled ())
            std::cerr << "spot: ready gate failed" << std::endl;
        perf::single::print_fail_result (lib_name, "SPOT", transport, msg_size);
        return;
    }

    zlink::socket_t spot_socket = zlink::socket_t::wrap (spot.handle ());
    perf::single::queue_probe_t queue_probe (&spot_socket, &spot_socket);
    perf::single::subscribe_callback_receiver_t receiver_cb;
    if (!receiver_cb.attach_spot (spot, &queue_probe)) {
        if (perf_debug_enabled ())
            std::cerr << "spot: attach callback failed" << std::endl;
        perf::single::print_fail_result (
          lib_name, "SPOT", transport, msg_size, &queue_probe);
        return;
    }

    const size_t payload_size =
      std::max<size_t> (msg_size, perf_single_metric::header_size ());
    std::vector<char> payload (payload_size, 's');

    const uint32_t run_id = static_cast<uint32_t> (perf_single_metric::now_us ());
    uint64_t seq = 1;

    int warmup_default = 200;
    if (msg_size >= 65536)
        warmup_default = 20;
    const int warmup_count =
      perf::single::resolve_bench_count ("PERF_WARMUP_COUNT", warmup_default);

    unsigned long long warmup_received = 0;
    if (!perf::single::run_subscribe_callback_phase (receiver_cb,
                                                     &send_spot_payload,
                                                     &spot,
                                                     payload,
                                                     msg_size,
                                                     run_id,
                                                     seq,
                                                     perf_single_metric::phase_warmup,
                                                     warmup_count,
                                                     0,
                                                     perf::single::resolve_single_recv_timeout_ms (),
                                                     k_topic,
                                                     &warmup_received,
                                                     NULL)) {
        if (perf_debug_enabled ())
            std::cerr << "spot: warmup phase failed" << std::endl;
        perf::single::print_fail_result (
          lib_name, "SPOT", transport, msg_size, &queue_probe);
        return;
    }

    const int duration_s =
      std::max (1, perf::single::resolve_single_duration_seconds ());
    unsigned long long received = 0;
    perf::single::latency_stats_t latency;
    if (!perf::single::run_subscribe_callback_phase (receiver_cb,
                                                     &send_spot_payload,
                                                     &spot,
                                                     payload,
                                                     msg_size,
                                                     run_id,
                                                     seq,
                                                     perf_single_metric::phase_active,
                                                     0,
                                                     duration_s,
                                                     perf::single::resolve_single_recv_timeout_ms (),
                                                     k_topic,
                                                     &received,
                                                     &latency)) {
        if (perf_debug_enabled ())
            std::cerr << "spot: active phase failed" << std::endl;
        perf::single::print_fail_result (
          lib_name, "SPOT", transport, msg_size, &queue_probe);
        return;
    }

    const double throughput =
      static_cast<double> (received) / static_cast<double> (duration_s);
    perf::single::print_result (lib_name,
                                "SPOT",
                                transport,
                                msg_size,
                                throughput,
                                latency.mean_us,
                                latency.p95_us,
                                latency.p99_us,
                                perf::single::sample_queue_stats (&queue_probe));
}

int main (int argc, char **argv)
{
    return perf::single::run_standard_bench_main (argc, argv, run_pattern_spot);
}
