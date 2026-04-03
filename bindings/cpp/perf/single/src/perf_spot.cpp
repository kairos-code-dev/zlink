// SPOT benchmark: one-way publisher->subscriber callback path.
// Topology: publisher spot(bind) -> subscriber spot(connect)

#include "../common/perf_single_common.hpp"
#include "../common/perf_single_runner.hpp"

#include <cstdlib>
#include <iostream>
#include <sstream>
#include <vector>

#if defined(ZLINK_HAVE_WINDOWS)
#include <process.h>
#endif

#if !defined(ZLINK_HAVE_WINDOWS)
#include <unistd.h>
#endif

namespace {

const char *const k_topic = "bench";

unsigned current_process_id ()
{
#if defined(ZLINK_HAVE_WINDOWS)
    return static_cast<unsigned> (_getpid ());
#else
    return static_cast<unsigned> (getpid ());
#endif
}

std::string make_spot_endpoint (const std::string &transport_)
{
    static unsigned counter = 0;
    const unsigned port = 34000u + ((current_process_id () % 1000u) * 20u)
                          + (++counter);
    return perf::single::make_fixed_endpoint (transport_, static_cast<int> (port));
}

bool wait_for_monitor_readable (void *monitor_handle_, int timeout_ms_)
{
    zlink_pollitem_t item;
    item.socket = monitor_handle_;
    item.fd = 0;
    item.events = ZLINK_POLLIN;
    item.revents = 0;

    const int rc = zlink_poll (&item, 1, timeout_ms_);
    return rc > 0 && (item.revents & ZLINK_POLLIN) != 0;
}

bool perf_debug_enabled ()
{
    return std::getenv ("PERF_DEBUG") != NULL;
}

bool send_spot_payload (void *userdata_, const void *data_, size_t size_)
{
    zlink::service::spot_t *spot = static_cast<zlink::service::spot_t *> (userdata_);
    if (!spot)
        return false;

    zlink_msg_t part;
    if (zlink_msg_init_size (&part, size_) != 0)
        return false;
    if (size_ > 0 && data_)
        std::memcpy (zlink_msg_data (&part), data_, size_);

    const int rc = zlink_publish (
      spot->handle (), k_topic, &part, 1, static_cast<zlink_send_flags_t> (0));
    if (rc != 0) {
        const int err = errno;
        (void) zlink_msg_close (&part);
        errno = err;
        return false;
    }
    return true;
}

bool wait_for_service_monitor_event_endpoint (
  zlink::service_monitor_handle_t &monitor_,
  uint32_t event_type_,
  const std::string &endpoint_,
  int timeout_ms_)
{
    const std::chrono::steady_clock::time_point deadline =
      std::chrono::steady_clock::now () + std::chrono::milliseconds (timeout_ms_);

    while (std::chrono::steady_clock::now () < deadline) {
        const std::chrono::steady_clock::duration remaining =
          deadline - std::chrono::steady_clock::now ();
        const int remaining_ms = static_cast<int> (
          std::chrono::duration_cast<std::chrono::milliseconds> (remaining)
            .count ());
        if (remaining_ms <= 0)
            break;

        if (!wait_for_monitor_readable (monitor_.handle (), remaining_ms))
            continue;

        const zlink::maybe_t<zlink_service_monitor_event_t> event =
          monitor_.try_recv();
        if (!event)
            continue;
        if (event->event_type != event_type_)
            continue;
        if ((event->detail_flags & ZLINK_SERVICE_EVENT_DETAIL_ENDPOINT) == 0)
            continue;
        if (endpoint_ != event->endpoint)
            continue;
        return true;
    }

    return false;
}

} // namespace

void run_pattern_spot (const std::string &transport,
                       size_t msg_size,
                       const std::string &lib_name)
{
    if (transport != "tcp" && transport != "tls" && transport != "ws"
        && transport != "wss") {
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

    zlink::service::spot_node_t pub_node (ctx.ctx ());
    zlink::service::spot_node_t sub_node (ctx.ctx ());
    zlink::service::spot_t pub_spot (pub_node);
    zlink::service::spot_t sub_spot (sub_node);
    if (!pub_node.valid () || !sub_node.valid () || !pub_spot.valid ()
        || !sub_spot.valid ()) {
        if (perf_debug_enabled ())
            std::cerr << "spot: invalid service topology" << std::endl;
        perf::single::print_fail_result (lib_name, "SPOT", transport, msg_size);
        return;
    }

    if (transport == "tls" || transport == "wss") {
        std::string cert;
        std::string key;
        std::string ca;
        if (!perf::single::try_resolve_perf_tls_paths (cert, key, ca)
            || pub_node.set_tls_server (cert, key, false) != 0
            || sub_node.set_tls_client (ca, std::string ("localhost"), false)
                 != 0) {
            if (perf_debug_enabled ())
                std::cerr << "spot: tls setup failed" << std::endl;
            perf::single::print_fail_result (lib_name, "SPOT", transport, msg_size);
            return;
        }
    }

    (void) pub_spot.set (zlink::socket_options::sndhwm,
                         perf::single::resolve_single_socket_hwm (true));
    (void) sub_spot.set (zlink::socket_options::rcvhwm,
                         perf::single::resolve_single_socket_hwm (false));
    (void) pub_spot.set (zlink::socket_options::sndtimeo,
                         perf::single::resolve_single_send_timeout_ms ());
    (void) sub_spot.set (zlink::socket_options::rcvtimeo,
                         perf::single::resolve_single_recv_timeout_ms ());

    const std::string endpoint = make_spot_endpoint (transport);
    if (pub_node.bind (endpoint) != 0 || sub_node.connect_peer (endpoint) != 0
        || sub_spot.set_subscription (k_topic) != 0) {
        if (perf_debug_enabled ())
            std::cerr << "spot: ready gate failed" << std::endl;
        perf::single::print_fail_result (lib_name, "SPOT", transport, msg_size);
        return;
    }
    perf::single::settle ();

    zlink::socket_t pub_socket = zlink::socket_t::wrap (pub_spot.handle ());
    zlink::socket_t sub_socket = zlink::socket_t::wrap (sub_spot.handle ());
    perf::single::queue_probe_t queue_probe (&pub_socket, &sub_socket);
    perf::single::subscribe_callback_receiver_t receiver_cb;
    if (!receiver_cb.attach_spot (sub_spot, &queue_probe)) {
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
                                                     &pub_spot,
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
                                                     &pub_spot,
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
