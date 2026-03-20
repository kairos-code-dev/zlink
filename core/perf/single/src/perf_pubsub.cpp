#include "../common/bench_common.hpp"
#include "../common/perf_single_metric_header.hpp"
#include <zlink.h>
#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdlib>
#include <cstring>
#include <thread>
#include <vector>

namespace {
static const char *k_pubsub_topic = "bench";

inline int resolve_pubsub_xpub_nodrop_opt ()
{
    const char *env = std::getenv ("PERF_SINGLE_PUBSUB_XPUB_NODROP");
    if (!env || !*env)
        return 1;

    if (std::strcmp (env, "1") == 0 || std::strcmp (env, "true") == 0
        || std::strcmp (env, "TRUE") == 0 || std::strcmp (env, "on") == 0
        || std::strcmp (env, "ON") == 0) {
        return 1;
    }
    if (std::strcmp (env, "0") == 0 || std::strcmp (env, "false") == 0
        || std::strcmp (env, "FALSE") == 0 || std::strcmp (env, "off") == 0
        || std::strcmp (env, "OFF") == 0) {
        return 0;
    }
    return 1;
}

inline int recv_pubsub_header_flags (
  void *socket,
  size_t expected_size,
  int flags,
  perf_single_metric::header_t *header_out,
  bool *header_ok_out)
{
    if (!socket)
        return -1;

    if (header_ok_out)
        *header_ok_out = false;

    zlink_routing_id_t source_rid;
    source_rid.size = 0;
    zlink_msg_t *parts = NULL;
    size_t part_count = 0;
    char topic[32];
    std::memset (topic, 0, sizeof (topic));
    size_t topic_len = sizeof (topic);
    const int rc = ::zlink_subscribe (
      socket, &source_rid, &parts, &part_count, topic, &topic_len,
      static_cast<zlink_send_flags_t> (flags));
    if (rc < 0) {
        const int err = zlink_errno ();
        if (err == EAGAIN || err == EINTR)
            return 0;
        return -1;
    }

    const bool topic_ok =
      topic_len == std::strlen (k_pubsub_topic)
      && std::memcmp (topic, k_pubsub_topic, topic_len) == 0;
    if (!topic_ok || part_count != 1) {
        if (parts) {
            zlink_multipart_close (parts, part_count);
            free (parts);
        }
        return -1;
    }

    const size_t actual_size = zlink_msg_size (&parts[0]);
    const bool size_ok = actual_size == expected_size;
    bool header_ok = false;
    if (size_ok) {
        if (header_out) {
            header_ok = perf_single_metric::decode_payload_header (
              zlink_msg_data (&parts[0]), actual_size, header_out);
        } else {
            header_ok = true;
        }
    }

    zlink_multipart_close (parts, part_count);
    free (parts);

    if (!size_ok)
        return -1;

    if (header_ok_out)
        *header_ok_out = header_ok;

    return 1;
}

inline bool send_pubsub_sample (void *pub_socket_,
                                std::vector<char> *payload_,
                                size_t payload_size_,
                                size_t msg_size_,
                                uint32_t run_id_,
                                uint64_t *seq_,
                                perf_single_metric::phase_t phase_,
                                int flags_)
{
    if (!pub_socket_ || !payload_ || !seq_)
        return false;

    const uint64_t sent_ts = perf_single_metric::now_us ();
    if (!perf_single_metric::stamp_payload (payload_->data (),
                                            payload_size_,
                                            run_id_,
                                            phase_,
                                            msg_size_,
                                            (*seq_)++,
                                            sent_ts)) {
        return false;
    }

    zlink_msg_t payload_part;
    if (zlink_msg_init_size (&payload_part, payload_size_) != 0)
        return false;

    std::memcpy (
      zlink_msg_data (&payload_part), payload_->data (), payload_size_);
    if (::zlink_publish (pub_socket_, k_pubsub_topic, &payload_part, 1, flags_)
        < 0) {
        zlink_msg_close (&payload_part);
        return false;
    }

    return true;
}

inline bool setup_connected_pubsub_pair (void *pub_socket_,
                                         void *sub_socket_,
                                         const std::string &transport_,
                                         const std::string &id_)
{
    if (!pub_socket_ || !sub_socket_)
        return false;

    if (!setup_tls_server (pub_socket_, transport_)
        || !setup_tls_client (sub_socket_, transport_)) {
        return false;
    }

    apply_single_hwm (pub_socket_);
    apply_single_hwm (sub_socket_);

    if (zlink_set_subscription (sub_socket_, "") != 0)
        return false;

    readiness_monitor_t pub_monitor;
    readiness_monitor_t sub_monitor;
    if (!open_socket_readiness_monitor (
          pub_socket_, ZLINK_EVENT_PUB_DELIVERY_READY_CHANGED, pub_monitor)) {
        return false;
    }
    if (!open_socket_readiness_monitor (
          sub_socket_, ZLINK_EVENT_SUB_DELIVERY_READY_CHANGED, sub_monitor)) {
        close_socket_readiness_monitor (pub_monitor);
        return false;
    }

    const std::string endpoint =
      bind_and_resolve_endpoint (pub_socket_, transport_, id_);
    if (endpoint.empty ()) {
        close_socket_readiness_monitor (sub_monitor);
        close_socket_readiness_monitor (pub_monitor);
        return false;
    }

    if (!connect_checked (sub_socket_, endpoint)) {
        close_socket_readiness_monitor (sub_monitor);
        close_socket_readiness_monitor (pub_monitor);
        return false;
    }

    apply_single_benchmark_socket_options (pub_socket_, transport_);
    apply_single_benchmark_socket_options (sub_socket_, transport_);

    const int timeout_ms = parse_positive_env ("PERF_CONNECT_READY_TIMEOUT_MS",
                                               3000);
    const bool sub_ready = wait_socket_readiness (sub_monitor, timeout_ms);
    const bool pub_ready = wait_socket_readiness (pub_monitor, timeout_ms);

    close_socket_readiness_monitor (sub_monitor);
    close_socket_readiness_monitor (pub_monitor);

    if (bench_debug_enabled () && !(sub_ready && pub_ready)) {
        std::cerr << "[perf-pubsub] delivery-ready gate failed"
                  << " sub_ready=" << (sub_ready ? 1 : 0)
                  << " pub_ready=" << (pub_ready ? 1 : 0) << std::endl;
    }
    return sub_ready && pub_ready;
}

inline int send_pubsub_sample_with_backpressure (
  void *pub_socket_,
  std::vector<char> *payload_,
  size_t payload_size_,
  size_t msg_size_,
  uint32_t run_id_,
  uint64_t *seq_,
  perf_single_metric::phase_t phase_,
  poller_guard_t *send_poller_,
  const std::chrono::steady_clock::time_point &deadline_)
{
    if (!send_poller_ || !send_poller_->valid ())
        return -1;

    while (std::chrono::steady_clock::now () < deadline_) {
        if (send_pubsub_sample (pub_socket_, payload_, payload_size_, msg_size_,
                                run_id_, seq_, phase_, ZLINK_DONTWAIT)) {
            return 1;
        }

        const int err = zlink_errno ();
        if (err == EINTR)
            continue;
        if (err != EAGAIN)
            return -1;
        zlink_poller_event_t event;
        const int poll_rc =
          send_poller_->wait (&event, static_cast<int> (remaining_timeout_ms (
                                   deadline_, 1)));
        if (poll_rc < 0) {
            if (zlink_errno () == EINTR || zlink_errno () == EAGAIN)
                continue;
            return -1;
        }
        if (poll_rc == 0 || (event.events & ZLINK_POLLOUT) == 0)
            return 0;
    }

    return 0;
}

inline bool run_oneway_phase (void *pub_socket,
                              void *sub_socket,
                              std::vector<char> *payload,
                              size_t payload_size,
                              size_t msg_size,
                              uint32_t run_id,
                              uint64_t *seq,
                              perf_single_metric::phase_t phase,
                              int duration_s,
                              int recv_timeout_ms,
                              queue_probe_t *queue_probe,
                              unsigned long long *out_received,
                              latency_stats_t *out_latency)
{
    if (!pub_socket || !sub_socket || !payload || !seq || !out_received)
        return false;

    const bool active_phase = phase == perf_single_metric::phase_active;
    const auto deadline =
      std::chrono::steady_clock::now ()
        + std::chrono::seconds (duration_s > 0 ? duration_s : 1);
    const auto drain_idle_limit = std::chrono::milliseconds (
      recv_timeout_ms > 0 ? recv_timeout_ms : 200);
    const auto recv_poll_window = std::chrono::milliseconds (
      recv_timeout_ms > 0 ? recv_timeout_ms : 200);

    std::atomic<bool> sender_done (false);
    std::atomic<bool> recv_failed (false);
    std::atomic<unsigned long long> received (0);
    latency_stats_builder_t latency_builder;

    std::thread receiver_thread ([&] () {
        auto last_recv_at = std::chrono::steady_clock::now ();
        poller_guard_t recv_poller;
        if (!recv_poller.valid ()
            || !recv_poller.add (sub_socket, sub_socket, ZLINK_POLLIN)) {
            recv_failed.store (true, std::memory_order_release);
            return;
        }

        auto account_header =
          [&] (const perf_single_metric::header_t &header,
               bool header_ok) {
              if (active_phase && queue_probe)
                  queue_probe->sample_recv_if_due ();

              if (!header_ok || header.magic != perf_single_metric::k_magic
                  || header.phase != static_cast<uint32_t> (phase)) {
                  return;
              }

              if (active_phase) {
                  if (std::chrono::steady_clock::now () < deadline) {
                      received.fetch_add (1, std::memory_order_relaxed);
                      const uint64_t now = perf_single_metric::now_us ();
                      const double latency_us =
                        now >= header.sent_ts_us
                          ? static_cast<double> (now - header.sent_ts_us)
                          : 0.0;
                      latency_builder.add (latency_us);
                  }
              } else {
                  received.fetch_add (1, std::memory_order_relaxed);
              }
          };

        if (active_phase && queue_probe)
            queue_probe->force_sample_recv ();

        while (true) {
            const bool done = sender_done.load (std::memory_order_acquire);
            const auto now = std::chrono::steady_clock::now ();
            auto poll_deadline = now + recv_poll_window;
            if (done) {
                const auto idle_deadline = last_recv_at + drain_idle_limit;
                if (now >= idle_deadline)
                    break;
                poll_deadline = idle_deadline;
            }

            zlink_poller_event_t event;
            const int poll_rc = recv_poller.wait (
              &event, static_cast<int> (remaining_timeout_ms (poll_deadline, 1)));
            if (poll_rc < 0) {
                const int err = zlink_errno ();
                if (err == EINTR || err == EAGAIN)
                    continue;
                recv_failed.store (true, std::memory_order_release);
                break;
            }
            if (poll_rc == 0)
                continue;

            perf_single_metric::header_t header;
            bool header_ok = false;
            const int recv_rc = recv_pubsub_header_flags (
              sub_socket, payload_size, ZLINK_DONTWAIT, &header, &header_ok);
            if (recv_rc > 0) {
                last_recv_at = std::chrono::steady_clock::now ();
                account_header (header, header_ok);

                for (;;) {
                    perf_single_metric::header_t burst_header;
                    bool burst_header_ok = false;
                    const int burst_rc = recv_pubsub_header_flags (
                      sub_socket, payload_size, ZLINK_DONTWAIT, &burst_header,
                      &burst_header_ok);
                    if (burst_rc > 0) {
                        last_recv_at = std::chrono::steady_clock::now ();
                        account_header (burst_header, burst_header_ok);
                        continue;
                    }
                    if (burst_rc == 0)
                        break;

                    recv_failed.store (true, std::memory_order_release);
                    break;
                }

                if (recv_failed.load (std::memory_order_acquire))
                    break;
                continue;
            }

            if (recv_rc == 0) {
                if (done
                    && std::chrono::steady_clock::now () - last_recv_at
                         >= drain_idle_limit) {
                    break;
                }
                continue;
            }

            recv_failed.store (true, std::memory_order_release);
            break;
        }

        if (active_phase && queue_probe)
            queue_probe->force_sample_recv ();
    });

    bool send_failed = false;
    poller_guard_t send_poller;
    if (!send_poller.valid ()
        || !send_poller.add (pub_socket, pub_socket, ZLINK_POLLOUT)) {
        sender_done.store (true, std::memory_order_release);
        receiver_thread.join ();
        return false;
    }
    if (active_phase && queue_probe)
        queue_probe->force_sample_send ();

    while (std::chrono::steady_clock::now () < deadline) {
        const int send_rc = send_pubsub_sample_with_backpressure (
          pub_socket, payload, payload_size, msg_size, run_id, seq, phase,
          &send_poller, deadline);
        if (send_rc < 0) {
            send_failed = true;
            break;
        }
        if (send_rc == 0)
            break;
        if (active_phase && queue_probe)
            queue_probe->sample_send_if_due ();
    }

    if (active_phase && queue_probe)
        queue_probe->force_sample_send ();

    sender_done.store (true, std::memory_order_release);
    receiver_thread.join ();

    if (send_failed || recv_failed.load (std::memory_order_acquire))
        return false;

    *out_received = received.load (std::memory_order_relaxed);

    if (active_phase) {
        if (received.load (std::memory_order_relaxed) == 0
            || latency_builder.count () == 0 || !out_latency) {
            return false;
        }
        *out_latency = latency_builder.snapshot ();
    } else if (received.load (std::memory_order_relaxed) == 0) {
        return false;
    }

    return true;
}

} // namespace

void run_pubsub (const std::string &transport,
                 size_t msg_size,
                 const std::string &lib_name)
{
    if (!transport_available (transport))
        return;

    auto print_fail_no_queue = [&] () {
        print_fail_result (lib_name, "PUBSUB", transport, msg_size);
    };

    ctx_guard_t ctx;
    if (!ctx.valid ()) {
        print_fail_no_queue ();
        return;
    }

    socket_guard_t pub (ctx.get (), ZLINK_SOCKET_PUB);
    socket_guard_t sub (ctx.get (), ZLINK_SOCKET_SUB);
    if (!pub.valid () || !sub.valid ()) {
        print_fail_no_queue ();
        return;
    }

    const int xpub_nodrop_opt = resolve_pubsub_xpub_nodrop_opt ();
    set_pub_opt_int (pub.get (), ZLINK_PUB_OPT_NODROP, xpub_nodrop_opt,
                     "ZLINK_PUB_OPT_NODROP");

    if (!setup_connected_pubsub_pair (
          pub.get (), sub.get (), transport, lib_name + "_pubsub")) {
        print_fail_no_queue ();
        return;
    }

    const int recv_timeout_ms = resolve_single_pubsub_recv_timeout_ms ();
    const size_t payload_size =
      std::max<size_t> (msg_size, perf_single_metric::header_size ());
    std::vector<char> payload (payload_size, 'a');
    queue_probe_t queue_probe (pub.get (), sub.get ());

    auto print_fail_with_queue = [&] () {
        print_fail_result (lib_name, "PUBSUB", transport, msg_size, &queue_probe);
    };

    const uint32_t run_id = static_cast<uint32_t> (perf_single_metric::now_us ());
    uint64_t seq = 1;

    unsigned long long warmup_received = 0;
    const int warmup_s = resolve_single_warmup_seconds ();
    if (!run_oneway_phase (pub.get (),
                           sub.get (),
                           &payload,
                           payload_size,
                           msg_size,
                           run_id,
                           &seq,
                           perf_single_metric::phase_warmup,
                           warmup_s,
                           recv_timeout_ms,
                           NULL,
                           &warmup_received,
                           NULL)) {
        print_fail_with_queue ();
        return;
    }

    const int duration_s = std::max (1, resolve_single_duration_seconds ());
    unsigned long long received = 0;
    latency_stats_t latency_stats;
    if (!run_oneway_phase (pub.get (),
                           sub.get (),
                           &payload,
                           payload_size,
                           msg_size,
                           run_id,
                           &seq,
                           perf_single_metric::phase_active,
                           duration_s,
                           recv_timeout_ms,
                           &queue_probe,
                           &received,
                           &latency_stats)) {
        print_fail_with_queue ();
        return;
    }

    const double throughput =
      static_cast<double> (received) / static_cast<double> (duration_s);
    const queue_stats_t queue_stats = queue_probe.snapshot ();

    print_result (lib_name,
                  "PUBSUB",
                  transport,
                  msg_size,
                  throughput,
                  latency_stats.mean_us,
                  latency_stats.p95_us,
                  latency_stats.p99_us,
                  queue_stats);
}

int main (int argc, char **argv)
{
    return run_standard_bench_main (argc, argv, "PUBSUB", run_pubsub);
}
