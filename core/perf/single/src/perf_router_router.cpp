#include "../common/bench_common.hpp"
#include "../common/perf_single_metric_header.hpp"
#include <zlink.h>
#include <algorithm>
#include <atomic>
#include <thread>
#include <vector>

namespace {

inline bool perform_router_router_handshake (void *router1, void *router2)
{
    bool connected = false;
    char buf[16];

    const int handshake_timeout_ms =
      resolve_bench_count ("PERF_ROUTER_HANDSHAKE_TIMEOUT_MS", 3000);
    const auto deadline =
      std::chrono::steady_clock::now ()
      + std::chrono::milliseconds (
        handshake_timeout_ms > 0 ? handshake_timeout_ms : 3000);

    while (!connected && std::chrono::steady_clock::now () < deadline) {
        const bool id_sent =
          send_exact (router2, "ROUTER1", 7, ZLINK_SNDMORE | ZLINK_DONTWAIT);
        if (!id_sent) {
            const int err = zlink_errno ();
            if (err != EAGAIN && err != EINTR)
                return false;
        } else {
            const int rc = zlink_send (router2, "PING", 4, ZLINK_DONTWAIT);
            if (rc == 4) {
                const int id_len =
                  zlink_recv (router1, buf, sizeof (buf), ZLINK_DONTWAIT);
                if (id_len > 0) {
                    if (!recv_exact (router1, buf, 4, 0))
                        return false;
                    connected = true;
                } else if (id_len < 0) {
                    const int err = zlink_errno ();
                    if (err != EAGAIN && err != EINTR)
                        return false;
                }
            } else if (rc < 0) {
                const int err = zlink_errno ();
                if (err != EAGAIN && err != EINTR)
                    return false;
            }
        }

        if (!connected && zlink_poll (NULL, 0, 10) < 0 && zlink_errno () != EINTR)
            return false;
    }

    if (!connected)
        return false;

    if (!send_exact (router1, "ROUTER2", 7, ZLINK_SNDMORE)
        || !send_exact (router1, "PONG", 4, 0)) {
        return false;
    }

    const int pong_id_len = zlink_recv (router2, buf, sizeof (buf), 0);
    if (pong_id_len <= 0 || !recv_exact (router2, buf, 4, 0))
        return false;

    return true;
}

inline int recv_router_header_flags (void *socket,
                                     size_t payload_size,
                                     int flags,
                                     perf_single_metric::header_t *header_out,
                                     bool *header_ok_out)
{
    if (!socket)
        return -1;

    if (header_ok_out)
        *header_ok_out = false;

    zlink_msg_t rid;
    if (zlink_msg_init (&rid) != 0)
        return -1;

    const int id_rc = zlink_msg_recv (&rid, socket, flags);
    if (id_rc < 0) {
        const int err = zlink_errno ();
        zlink_msg_close (&rid);
        if (err == EAGAIN || err == EINTR)
            return 0;
        return -1;
    }

    if (zlink_msg_more (&rid) == 0) {
        zlink_msg_close (&rid);
        return -1;
    }
    zlink_msg_close (&rid);

    zlink_msg_t payload;
    if (zlink_msg_init (&payload) != 0)
        return -1;
    if (zlink_msg_recv (&payload, socket, 0) < 0) {
        zlink_msg_close (&payload);
        return -1;
    }

    const size_t actual_size = zlink_msg_size (&payload);
    const bool size_ok = actual_size == payload_size;
    const bool has_more = zlink_msg_more (&payload) != 0;
    bool header_ok = false;

    if (size_ok && !has_more) {
        if (header_out) {
            header_ok = perf_single_metric::decode_payload_header (
              zlink_msg_data (&payload), actual_size, header_out);
        } else {
            header_ok = true;
        }
    }

    zlink_msg_close (&payload);

    if (has_more)
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

    zlink_setsockopt (router1, ZLINK_ROUTING_ID, "ROUTER1", 7);
    zlink_setsockopt (router2, ZLINK_ROUTING_ID, "ROUTER2", 7);

    int mandatory = 1;
    zlink_setsockopt (router1, ZLINK_ROUTER_MANDATORY, &mandatory,
                      sizeof (mandatory));
    zlink_setsockopt (router2, ZLINK_ROUTER_MANDATORY, &mandatory,
                      sizeof (mandatory));

    if (!setup_connected_pair (router1, router2, transport, pair_id)
        || !perform_router_router_handshake (router1, router2)) {
        return false;
    }

    const int timeout_ms = resolve_single_recv_timeout_ms ();
    set_sockopt_int (router1, ZLINK_RCVTIMEO, timeout_ms, "ZLINK_RCVTIMEO");
    set_sockopt_int (router2, ZLINK_RCVTIMEO, timeout_ms, "ZLINK_RCVTIMEO");
    return true;
}

inline bool run_oneway_phase (void *router1,
                              void *router2,
                              std::vector<char> *payload,
                              size_t payload_size,
                              size_t msg_size,
                              uint32_t run_id,
                              uint64_t *seq,
                              perf_single_metric::phase_t phase,
                              int warmup_count,
                              int duration_s,
                              int recv_timeout_ms,
                              queue_probe_t *queue_probe,
                              unsigned long long *out_received,
                              latency_stats_t *out_latency)
{
    if (!router1 || !router2 || !payload || !seq || !out_received)
        return false;
    (void) recv_timeout_ms;

    const bool active_phase = phase == perf_single_metric::phase_active;
    const auto deadline =
      active_phase
        ? std::chrono::steady_clock::now ()
            + std::chrono::seconds (duration_s > 0 ? duration_s : 1)
        : std::chrono::steady_clock::time_point ();

    unsigned long long received = 0;
    latency_stats_builder_t latency_builder;
    if (active_phase && queue_probe)
        queue_probe->force_sample_send ();
    if (active_phase && queue_probe)
        queue_probe->force_sample_recv ();

    auto account_header =
      [&] (const perf_single_metric::header_t &header, bool header_ok) {
          if (active_phase && queue_probe)
              queue_probe->sample_recv_if_due ();

          if (!header_ok || header.magic != perf_single_metric::k_magic
              || header.phase != static_cast<uint32_t> (phase)) {
              return;
          }

          ++received;
          if (!active_phase)
              return;

          const uint64_t now = perf_single_metric::now_us ();
          const double latency_us =
            now >= header.sent_ts_us
              ? static_cast<double> (now - header.sent_ts_us)
              : 0.0;
          latency_builder.add (latency_us);
      };

    unsigned long long iterations = 0;
    while (true) {
        if (active_phase) {
            if (std::chrono::steady_clock::now () >= deadline)
                break;
        } else if (iterations >= static_cast<unsigned long long> (warmup_count)) {
            break;
        }

        const uint64_t sent_ts = perf_single_metric::now_us ();
        if (!perf_single_metric::stamp_payload (payload->data (),
                                                payload_size,
                                                run_id,
                                                phase,
                                                msg_size,
                                                (*seq)++,
                                                sent_ts)
            || !send_exact (router2, "ROUTER1", 7, ZLINK_SNDMORE)
            || !send_exact (router2, payload->data (), payload_size, 0)) {
            return false;
        }
        if (active_phase && queue_probe)
            queue_probe->sample_send_if_due ();

        perf_single_metric::header_t header;
        bool header_ok = false;
        int recv_rc =
          recv_router_header_flags (router1, payload_size, 0, &header, &header_ok);
        while (recv_rc == 0 && zlink_errno () == EINTR)
            recv_rc = recv_router_header_flags (
              router1, payload_size, 0, &header, &header_ok);
        if (recv_rc <= 0)
            return false;
        account_header (header, header_ok);

        for (;;) {
            perf_single_metric::header_t burst_header;
            bool burst_header_ok = false;
            recv_rc = recv_router_header_flags (router1,
                                                payload_size,
                                                ZLINK_DONTWAIT,
                                                &burst_header,
                                                &burst_header_ok);
            if (recv_rc > 0) {
                account_header (burst_header, burst_header_ok);
                continue;
            }
            if (recv_rc == 0 && zlink_errno () == EINTR)
                continue;
            if (recv_rc == 0)
                break;
            return false;
        }

        ++iterations;
    }

    if (active_phase && queue_probe) {
        queue_probe->force_sample_send ();
        queue_probe->force_sample_recv ();
    }

    *out_received = received;
    if (active_phase) {
        if (received == 0 || latency_builder.count () == 0 || !out_latency)
            return false;
        *out_latency = latency_builder.snapshot ();
    } else if (received < iterations) {
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

    auto print_fail_no_queue = [&] () {
        print_fail_result (lib_name, "ROUTER_ROUTER", transport, msg_size);
    };

    ctx_guard_t ctx;
    if (!ctx.valid ()) {
        print_fail_no_queue ();
        return;
    }

    socket_guard_t router1 (ctx.get (), ZLINK_ROUTER);
    socket_guard_t router2 (ctx.get (), ZLINK_ROUTER);
    if (!router1.valid () || !router2.valid ()) {
        print_fail_no_queue ();
        return;
    }

    if (!setup_router_router_session (
          router1.get (), router2.get (), transport,
          lib_name + "_router_router")) {
        print_fail_no_queue ();
        return;
    }

    const int recv_timeout_ms = resolve_single_recv_timeout_ms ();
    const size_t payload_size =
      std::max<size_t> (msg_size, perf_single_metric::header_size ());
    std::vector<char> payload (payload_size, 'a');
    queue_probe_t queue_probe (router2.get (), router1.get ());

    auto print_fail_with_queue = [&] () {
        print_fail_result (
          lib_name, "ROUTER_ROUTER", transport, msg_size, &queue_probe);
    };

    const uint32_t run_id = static_cast<uint32_t> (perf_single_metric::now_us ());
    uint64_t seq = 1;

    unsigned long long warmup_received = 0;
    const int warmup_count = resolve_bench_count ("PERF_WARMUP_COUNT", 1000);
    if (!run_oneway_phase (router1.get (),
                           router2.get (),
                           &payload,
                           payload_size,
                           msg_size,
                           run_id,
                           &seq,
                           perf_single_metric::phase_warmup,
                           warmup_count,
                           0,
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
    if (!run_oneway_phase (router1.get (),
                           router2.get (),
                           &payload,
                           payload_size,
                           msg_size,
                           run_id,
                           &seq,
                           perf_single_metric::phase_active,
                           0,
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
    return run_standard_bench_main (argc, argv, run_router_router);
}
