#include "../common/bench_common.hpp"
#include "../common/perf_single_metric_header.hpp"
#include <zlink.h>
#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstring>
#include <thread>
#include <vector>

namespace {

inline void debug_router_router (const char *message_)
{
    if (bench_debug_enabled ())
        std::cerr << "[perf-router-router] " << message_ << std::endl;
}

inline int recv_router_header_flags (
  void *socket,
  const char *expected_source_rid,
  size_t expected_source_rid_size,
  size_t expected_size,
  int flags,
  perf_single_metric::header_t *header_out,
  bool *header_ok_out)
{
    if (!socket || !expected_source_rid || expected_source_rid_size == 0)
        return -1;

    if (header_ok_out)
        *header_ok_out = false;

    zlink_routing_id_t source_rid;
    source_rid.size = 0;
    zlink_msg_t *parts = NULL;
    size_t part_count = 0;
    const int rc = ::zlink_recv (
      socket, &source_rid, &parts, &part_count,
      static_cast<zlink_send_flags_t> (flags));
    if (rc < 0) {
        const int err = zlink_errno ();
        if (err == EAGAIN || err == EINTR)
            return 0;
        return -1;
    }

    const bool rid_ok =
      source_rid.size == expected_source_rid_size
      && std::memcmp (
           source_rid.data, expected_source_rid, expected_source_rid_size)
           == 0;
    if (!rid_ok || part_count != 1) {
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

inline bool setup_router_router_session (void *router1,
                                         void *router2,
                                         const std::string &transport,
                                         const std::string &pair_id)
{
    if (!router1 || !router2)
        return false;

    zlink_set_routing_id (router1, "ROUTER1", 7);
    zlink_set_routing_id (router2, "ROUTER2", 7);
    const int mandatory = 1;
    zlink_set_router_option (router1, ZLINK_ROUTER_OPT_MANDATORY, &mandatory,
                             sizeof (mandatory));
    zlink_set_router_option (router2, ZLINK_ROUTER_OPT_MANDATORY, &mandatory,
                             sizeof (mandatory));
    zlink_set_router_option (router2, ZLINK_ROUTER_OPT_CONNECT_ROUTING_ID,
                             "ROUTER1", 7);

    if (!setup_tls_server (router1, transport)
        || !setup_tls_client (router2, transport)) {
        return false;
    }

    apply_single_hwm (router1);
    apply_single_hwm (router2);

    const std::string endpoint =
      bind_and_resolve_endpoint (router1, transport, pair_id);
    if (endpoint.empty ())
        return false;

    connect_monitor_t bind_monitor;
    connect_monitor_t connect_monitor;
    if (!open_connect_monitor (router1, bind_monitor))
        return false;
    if (!open_connect_monitor (router2, connect_monitor)) {
        close_connect_monitor (bind_monitor);
        return false;
    }
    if (!connect_checked (router2, endpoint))
    {
        close_connect_monitor (connect_monitor);
        close_connect_monitor (bind_monitor);
        return false;
    }

    apply_single_benchmark_socket_options (router1, transport);
    apply_single_benchmark_socket_options (router2, transport);

    const int timeout_ms = parse_positive_env ("PERF_CONNECT_READY_TIMEOUT_MS",
                                               3000);
    const bool bind_ready = wait_connect_ready_count (bind_monitor, 1, timeout_ms);
    const bool connect_ready =
      wait_connect_ready_count (connect_monitor, 1, timeout_ms);
    close_connect_monitor (connect_monitor);
    close_connect_monitor (bind_monitor);
    return bind_ready && connect_ready;
}

inline int send_router_parts_with_backpressure (
  void *socket,
  zlink_msg_t *parts,
  poller_guard_t *send_poller,
  const std::chrono::steady_clock::time_point &deadline)
{
    if (!socket || !parts || !send_poller || !send_poller->valid ())
        return -1;

    while (std::chrono::steady_clock::now () < deadline) {
        if (::zlink_send (socket, parts, 2, ZLINK_DONTWAIT) >= 0)
            return 1;

        const int err = zlink_errno ();
        if (err == EINTR)
            continue;
        if (err != EAGAIN)
            return -1;
        zlink_poller_event_t event;
        const int poll_rc =
          send_poller->wait (&event, static_cast<int> (remaining_timeout_ms (
                                  deadline, 1)));
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

inline bool run_oneway_phase (void *receiver,
                              void *sender,
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
    if (!receiver || !sender || !payload || !seq || !out_received)
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
            || !recv_poller.add (receiver, receiver, ZLINK_POLLIN)) {
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
            const int recv_rc = recv_router_header_flags (
              receiver, "ROUTER2", 7, payload_size, ZLINK_DONTWAIT, &header,
              &header_ok);
            if (recv_rc > 0) {
                last_recv_at = std::chrono::steady_clock::now ();
                account_header (header, header_ok);

                for (;;) {
                    perf_single_metric::header_t burst_header;
                    bool burst_header_ok = false;
                    const int burst_rc = recv_router_header_flags (
                      receiver, "ROUTER2", 7, payload_size, ZLINK_DONTWAIT,
                      &burst_header, &burst_header_ok);
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
        || !send_poller.add (sender, sender, ZLINK_POLLOUT)) {
        sender_done.store (true, std::memory_order_release);
        receiver_thread.join ();
        return false;
    }
    if (active_phase && queue_probe)
        queue_probe->force_sample_send ();

    while (std::chrono::steady_clock::now () < deadline) {
        const uint64_t sent_ts = perf_single_metric::now_us ();
        bool send_ok = false;
        if (perf_single_metric::stamp_payload (payload->data (),
                                               payload_size,
                                               run_id,
                                               phase,
                                               msg_size,
                                               (*seq)++,
                                               sent_ts)) {
            zlink_msg_t parts[2];
            if (zlink_msg_init_size (&parts[0], 7) == 0) {
                if (zlink_msg_init_size (&parts[1], payload_size) == 0) {
                    std::memcpy (zlink_msg_data (&parts[0]), "ROUTER1", 7);
                    if (payload_size > 0) {
                        std::memcpy (
                          zlink_msg_data (&parts[1]), payload->data (),
                          payload_size);
                    }
                    const int send_rc = send_router_parts_with_backpressure (
                      sender, parts, &send_poller, deadline);
                    send_ok = send_rc > 0;
                    if (send_rc < 0) {
                        zlink_msg_close (&parts[0]);
                        zlink_msg_close (&parts[1]);
                    } else if (send_rc == 0) {
                        zlink_msg_close (&parts[0]);
                        zlink_msg_close (&parts[1]);
                        break;
                    }
                } else {
                    zlink_msg_close (&parts[0]);
                }
            }
        }
        if (!send_ok) {
            debug_router_router ("active/warmup send failed");
            send_failed = true;
            break;
        }
        if (active_phase && queue_probe)
            queue_probe->sample_send_if_due ();
    }

    if (active_phase && queue_probe)
        queue_probe->force_sample_send ();

    sender_done.store (true, std::memory_order_release);
    receiver_thread.join ();

    if (send_failed || recv_failed.load (std::memory_order_acquire)) {
        debug_router_router ("phase failed before metrics were collected");
        return false;
    }

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

void run_router_router (const std::string &transport,
                        size_t msg_size,
                        const std::string &lib_name)
{
    if (!transport_available (transport))
        return;

    ctx_guard_t ctx;
    if (!ctx.valid ()) {
        print_fail_result (lib_name, "ROUTER_ROUTER", transport, msg_size);
        return;
    }

    socket_guard_t router1 (ctx.get (), ZLINK_SOCKET_ROUTER);
    socket_guard_t router2 (ctx.get (), ZLINK_SOCKET_ROUTER);
    queue_probe_t queue_probe (router2.get (), router1.get ());
    auto print_fail_with_queue = [&] () {
        print_fail_result (
          lib_name, "ROUTER_ROUTER", transport, msg_size, &queue_probe);
    };
    if (!router1.valid () || !router2.valid ()) {
        print_fail_with_queue ();
        return;
    }

    if (!setup_router_router_session (
          router1.get (), router2.get (), transport,
          lib_name + "_router_router")) {
        debug_router_router ("session setup failed");
        print_fail_with_queue ();
        return;
    }

    const int recv_timeout_ms = resolve_single_recv_timeout_ms ();
    const size_t payload_size =
      std::max<size_t> (msg_size, perf_single_metric::header_size ());
    std::vector<char> payload (payload_size, 'a');

    const uint32_t run_id = static_cast<uint32_t> (perf_single_metric::now_us ());
    uint64_t seq = 1;

    unsigned long long warmup_received = 0;
    const int warmup_s = resolve_single_warmup_seconds ();
    if (!run_oneway_phase (router1.get (),
                           router2.get (),
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
        debug_router_router ("warmup phase failed");
        print_fail_with_queue ();
        return;
    }

    const int duration_s = std::max (1, resolve_single_duration_seconds ());
    unsigned long long received = 0;
    latency_stats_t latency_stats;
    if (!run_oneway_phase (router1.get (),
                           router2.get (),
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
        debug_router_router ("active phase failed");
        print_fail_with_queue ();
        return;
    }

    const double throughput =
      static_cast<double> (received) / static_cast<double> (duration_s);
    const queue_stats_t queue_stats = queue_probe.snapshot ();

    print_result (lib_name,
                  "ROUTER_ROUTER",
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
    return run_standard_bench_main (
      argc, argv, "ROUTER_ROUTER", run_router_router);
}
