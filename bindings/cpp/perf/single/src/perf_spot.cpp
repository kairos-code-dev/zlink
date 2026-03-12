// SPOT benchmark: one-way spot_node(pub)->spot_node(sub) flow.
// Topology: publisher(spot_node bind) -> subscriber(spot_node connect_peer_pub)

#include "../common/perf_single_common.hpp"
#include "../common/perf_single_runner.hpp"

#include <algorithm>
#include <atomic>
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

bool configure_spot_server_tls (zlink::service::spot_node_t &node,
                                const std::string &transport)
{
    if (transport != "tls" && transport != "wss")
        return true;

    std::string cert;
    std::string key;
    std::string ca;
    if (!perf::single::try_resolve_perf_tls_paths (cert, key, ca))
        return false;

    return node.set_tls_server (cert, key) == 0;
}

bool configure_spot_client_tls (zlink::service::spot_node_t &node,
                                const std::string &transport)
{
    if (transport != "tls" && transport != "wss")
        return true;

    std::string cert;
    std::string key;
    std::string ca;
    if (!perf::single::try_resolve_perf_tls_paths (cert, key, ca))
        return false;

    return node.set_tls_client (ca, "localhost", 0) == 0;
}

bool wait_sub_peer_ready (zlink::service::spot_node_t &node,
                          int timeout_ms)
{
    const auto deadline = std::chrono::steady_clock::now ()
                          + std::chrono::milliseconds (std::max (timeout_ms, 1000));
    while (std::chrono::steady_clock::now () < deadline) {
        size_t peer_count = 0;
        if (node.sub_peers (NULL, &peer_count) == 0 && peer_count > 0)
            return true;
        std::this_thread::sleep_for (std::chrono::milliseconds (1));
    }

    size_t peer_count = 0;
    return node.sub_peers (NULL, &peer_count) == 0 && peer_count > 0;
}

std::string bind_spot_endpoint (zlink::service::spot_node_t &node,
                                const std::string &transport,
                                int base_port)
{
    for (int i = 0; i < 64; ++i) {
        const std::string endpoint =
          perf::single::make_fixed_endpoint (transport, base_port + i);
        if (node.bind (endpoint) == 0)
            return endpoint;
    }
    return std::string ();
}

int recv_spot_payload_header (zlink::service::spot_t &subscriber,
                              std::string &topic,
                              std::vector<char> &payload_buffer,
                              size_t payload_size,
                              zlink::recv_flag flags,
                              perf_single_metric::header_t *header_out,
                              bool *header_ok_out)
{
    if (header_ok_out)
        *header_ok_out = false;

    size_t received_size = 0;
    const int rc = subscriber.recv (
      topic, payload_buffer.data (), payload_buffer.size (), &received_size, flags);
    if (rc != 0) {
        const int err = errno;
        if (err == EAGAIN || err == EINTR)
            return 0;
        return -1;
    }
    if (received_size != payload_size) {
        return -1;
    }
    if (topic != "bench")
        return 1;

    bool header_ok = false;
    if (header_out) {
        header_ok = perf_single_metric::decode_payload_header (
          payload_buffer.data (), received_size, header_out);
    }

    if (header_ok_out)
        *header_ok_out = header_ok;
    return 1;
}

bool send_spot_payload (zlink::service::spot_t &publisher,
                        const char *topic,
                        const void *payload,
                        size_t payload_size)
{
    if (!topic)
        return false;
    return publisher.publish (topic, payload, payload_size, zlink::send_flag::none)
           == 0;
}

bool drain_spot_queue (zlink::service::spot_t &subscriber,
                       std::string &topic,
                       std::vector<char> &payload_buffer,
                       size_t payload_size,
                       uint32_t run_id,
                       perf_single_metric::phase_t phase,
                       size_t msg_size,
                       bool active,
                       unsigned long long *received,
                       perf::single::latency_stats_builder_t *latency_builder)
{
    for (;;) {
        perf_single_metric::header_t header;
        bool header_ok = false;
        const int recv_rc = recv_spot_payload_header (subscriber,
                                                      topic,
                                                      payload_buffer,
                                                      payload_size,
                                                      zlink::recv_flag::dontwait,
                                                      &header,
                                                      &header_ok);
        if (recv_rc == 0)
            return true;
        if (recv_rc < 0)
            return false;
        if (!header_ok
            || !perf_single_metric::is_expected (header, run_id, phase, msg_size)) {
            continue;
        }

        ++(*received);
        if (active && latency_builder) {
            const uint64_t now = perf_single_metric::now_us ();
            const double latency_us =
              now >= header.sent_ts_us
                ? static_cast<double> (now - header.sent_ts_us)
                : 0.0;
            latency_builder->add (latency_us);
        }
    }
}

bool ensure_spot_subscription_ready (zlink::service::spot_t &publisher,
                                     zlink::service::spot_t &subscriber,
                                     size_t payload_size,
                                     int recv_timeout_ms)
{
    const int ready_timeout_ms =
      perf::single::resolve_bench_count ("PERF_SPOT_READY_TIMEOUT_MS", 2000);
    const auto deadline = std::chrono::steady_clock::now ()
                          + std::chrono::milliseconds (std::max (ready_timeout_ms, 1000));
    std::vector<char> probe (std::max<size_t> (payload_size, 1), 'p');
    std::string topic;
    std::vector<char> recv_buffer (probe.size ());
    (void) recv_timeout_ms;

    while (std::chrono::steady_clock::now () < deadline) {
        if (!send_spot_payload (publisher, "bench", probe.data (), probe.size ())) {
            return false;
        }

        perf_single_metric::header_t header;
        bool header_ok = false;
        const int recv_rc = recv_spot_payload_header (subscriber,
                                                      topic,
                                                      recv_buffer,
                                                      probe.size (),
                                                      zlink::recv_flag::none,
                                                      &header,
                                                      &header_ok);
        if (recv_rc > 0)
            return true;
        if (recv_rc == 0)
            continue;
        if (recv_rc < 0)
            return false;
    }

    return false;
}

bool run_phase (zlink::service::spot_t &publisher,
                zlink::service::spot_t &subscriber,
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

    const size_t payload_size = payload.size ();
    const bool active = phase == perf_single_metric::phase_active;
    const auto deadline =
      active ? std::chrono::steady_clock::now ()
                   + std::chrono::seconds (duration_s > 0 ? duration_s : 1)
             : std::chrono::steady_clock::time_point ();
    const int recv_timeout_ms =
      perf::single::resolve_single_recv_timeout_ms ();
    const auto drain_idle_limit =
      std::chrono::milliseconds (recv_timeout_ms > 0 ? recv_timeout_ms : 200);
    std::string topic;
    std::vector<char> recv_buffer (payload_size);

    perf::single::latency_stats_builder_t latency_builder (
      perf::single::resolve_single_latency_sample_cap ());
    unsigned long long received = 0;
    std::atomic<bool> sender_done (false);
    std::atomic<bool> recv_failed (false);

    std::thread receiver_thread ([&] () {
        auto last_recv_at = std::chrono::steady_clock::now ();

        auto account_header =
          [&] (const perf_single_metric::header_t &header, bool header_ok) {
              if (active && queue_probe)
                  queue_probe->sample_recv_if_due ();

              if (!header_ok
                  || !perf_single_metric::is_expected (
                    header, run_id, phase, msg_size)) {
                  return;
              }

              ++received;
              if (!active)
                  return;

              const uint64_t now = perf_single_metric::now_us ();
              const double latency_us =
                now >= header.sent_ts_us
                  ? static_cast<double> (now - header.sent_ts_us)
                  : 0.0;
              latency_builder.add (latency_us);
          };

        if (active && queue_probe)
            queue_probe->force_sample_recv ();

        while (true) {
            const bool done = sender_done.load (std::memory_order_acquire);
            const zlink::recv_flag flags =
              done ? zlink::recv_flag::dontwait : zlink::recv_flag::none;

            perf_single_metric::header_t header;
            bool header_ok = false;
            int recv_rc = recv_spot_payload_header (
              subscriber,
              topic,
              recv_buffer,
              payload_size,
              flags,
              &header,
              &header_ok);
            if (recv_rc > 0) {
                last_recv_at = std::chrono::steady_clock::now ();
                account_header (header, header_ok);

                for (;;) {
                    perf_single_metric::header_t burst_header;
                    bool burst_header_ok = false;
                    recv_rc = recv_spot_payload_header (
                      subscriber,
                      topic,
                      recv_buffer,
                      payload_size,
                      zlink::recv_flag::dontwait,
                      &burst_header,
                      &burst_header_ok);
                    if (recv_rc > 0) {
                        last_recv_at = std::chrono::steady_clock::now ();
                        account_header (burst_header, burst_header_ok);
                        continue;
                    }

                    const int err = errno;
                    if (recv_rc == 0 || err == EAGAIN)
                        break;
                    if (err == EINTR)
                        continue;

                    recv_failed.store (true, std::memory_order_release);
                    break;
                }

                if (recv_failed.load (std::memory_order_acquire))
                    break;
                continue;
            }

            const int err = errno;
            if (err == EINTR)
                continue;
            if (err == EAGAIN) {
                if (done
                    && std::chrono::steady_clock::now () - last_recv_at
                         >= drain_idle_limit) {
                    break;
                }
                std::this_thread::yield ();
                continue;
            }

            recv_failed.store (true, std::memory_order_release);
            break;
        }

        if (active && queue_probe)
            queue_probe->force_sample_recv ();
    });

    bool send_failed = false;
    if (active && queue_probe)
        queue_probe->force_sample_send ();

    auto send_one = [&] (uint64_t sent_ts) -> bool {
        if (!perf_single_metric::stamp_payload (payload.data (),
                                                payload_size,
                                                run_id,
                                                phase,
                                                msg_size,
                                                seq++,
                                                sent_ts)) {
            return false;
        }

        return send_spot_payload (publisher, "bench", payload.data (), payload_size);
    };

    if (active) {
        while (std::chrono::steady_clock::now () < deadline) {
            if (!send_one (perf_single_metric::now_us ())) {
                send_failed = true;
                break;
            }
            if (queue_probe)
                queue_probe->sample_send_if_due ();
        }
    } else {
        for (int i = 0; i < warmup_count; ++i) {
            if (!send_one (perf_single_metric::now_us ())) {
                send_failed = true;
                break;
            }
        }
    }

    if (active && queue_probe)
        queue_probe->force_sample_send ();

    sender_done.store (true, std::memory_order_release);
    receiver_thread.join ();

    if (send_failed || recv_failed.load (std::memory_order_acquire))
        return false;

    if (queue_probe) {
        queue_probe->force_sample_send ();
        queue_probe->force_sample_recv ();
    }

    if (active) {
        if (received == 0 || latency_builder.count () == 0 || !latency_out)
            return false;
        *latency_out = latency_builder.snapshot ();
    } else if (received < static_cast<unsigned long long> (warmup_count)) {
        return false;
    }

    *received_out = received;
    return true;
}

} // namespace

void run_pattern_spot (const std::string &transport,
                       size_t msg_size,
                       const std::string &lib_name)
{
    if (transport == "inproc" || transport == "ipc") {
        std::cout << "UNSUPPORTED,SPOT," << transport << std::endl;
        return;
    }
    if (!perf::single::transport_available (transport)) {
        std::cout << "UNSUPPORTED,SPOT," << transport << std::endl;
        return;
    }

    perf::single::ctx_guard_t ctx;
    if (!ctx.valid ()) {
        perf::single::print_fail_result (lib_name, "SPOT", transport, msg_size);
        return;
    }

    zlink::service::spot_node_t pub_node (ctx.ctx ());
    zlink::service::spot_node_t sub_node (ctx.ctx ());

    const int sndhwm = perf::single::resolve_single_socket_hwm (true);
    const int rcvhwm = perf::single::resolve_single_socket_hwm (false);
    const int send_timeout = perf::single::resolve_single_send_timeout_ms ();
    const int recv_timeout = perf::single::resolve_single_recv_timeout_ms ();
    const int xpub_nodrop =
      perf::single::parse_positive_env ("PERF_MULTI_SPOT_XPUB_NODROP", 1) > 0 ? 1
                                                                               : 0;

    (void) pub_node.set_sockopt (
      zlink::spot_node_socket_role::pub, zlink::socket_options::sndhwm, sndhwm);
    (void) pub_node.set_sockopt (
      zlink::spot_node_socket_role::pub,
      zlink::socket_options::sndtimeo,
      send_timeout);
    (void) pub_node.set_sockopt (
      zlink::spot_node_socket_role::pub,
      zlink::socket_options::xpub_nodrop,
      xpub_nodrop);
    (void) sub_node.set_sockopt (
      zlink::spot_node_socket_role::sub, zlink::socket_options::rcvhwm, rcvhwm);
    (void) sub_node.set_sockopt (
      zlink::spot_node_socket_role::sub,
      zlink::socket_options::rcvtimeo,
      recv_timeout);
    if (!configure_spot_server_tls (pub_node, transport)
        || !configure_spot_client_tls (sub_node, transport)) {
        perf::single::print_fail_result (lib_name, "SPOT", transport, msg_size);
        return;
    }

    zlink::service::spot_t publisher_spot (pub_node);
    zlink::service::spot_t subscriber_spot (sub_node);
    if (!publisher_spot.valid () || !subscriber_spot.valid ()) {
        perf::single::print_fail_result (lib_name, "SPOT", transport, msg_size);
        return;
    }

    const int base_port = 38500 + (bench_pid () % 1000) * 4;
    const std::string endpoint = bind_spot_endpoint (pub_node, transport, base_port);
    if (endpoint.empty () || sub_node.connect_peer_pub (endpoint) != 0) {
        perf::single::print_fail_result (lib_name, "SPOT", transport, msg_size);
        return;
    }

    if (!wait_sub_peer_ready (sub_node, 3000)) {
        perf::single::print_fail_result (lib_name, "SPOT", transport, msg_size);
        return;
    }

    zlink::socket_t publisher_socket =
      zlink::socket_t::wrap (pub_node.pub_socket_handle ());
    zlink::socket_t subscriber_socket =
      zlink::socket_t::wrap (sub_node.sub_socket_handle ());
    if (!publisher_socket.handle () || !subscriber_socket.handle ()
        || subscriber_spot.subscribe ("bench") != 0) {
        perf::single::print_fail_result (lib_name, "SPOT", transport, msg_size);
        return;
    }

    perf::single::queue_probe_t queue_probe (&publisher_socket, &subscriber_socket);

    const size_t payload_size =
      std::max<size_t> (msg_size, perf_single_metric::header_size ());
    if (!ensure_spot_subscription_ready (
          publisher_spot, subscriber_spot, payload_size, recv_timeout)) {
        perf::single::print_fail_result (
          lib_name, "SPOT", transport, msg_size, &queue_probe);
        return;
    }

    perf::single::settle ();

    std::vector<char> payload (payload_size, 's');

    const uint32_t run_id = static_cast<uint32_t> (perf_single_metric::now_us ());
    uint64_t seq = 1;

    int warmup_default = 200;
    if (msg_size >= 65536)
        warmup_default = 20;
    const int warmup_count =
      perf::single::resolve_bench_count ("PERF_WARMUP_COUNT", warmup_default);

    unsigned long long warmup_received = 0;
    if (!run_phase (publisher_spot,
                    subscriber_spot,
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
          lib_name, "SPOT", transport, msg_size, &queue_probe);
        return;
    }

    const int duration_s = std::max (1, perf::single::resolve_single_duration_seconds ());
    unsigned long long received = 0;
    perf::single::latency_stats_t latency;
    if (!run_phase (publisher_spot,
                    subscriber_spot,
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
                                queue_probe.snapshot ());
}

int main (int argc, char **argv)
{
    return perf::single::run_standard_bench_main (argc, argv, run_pattern_spot);
}
