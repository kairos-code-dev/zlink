#include "../common/bench_common.hpp"
#include "../common/perf_single_metric_header.hpp"
#include <zlink.h>
#include <algorithm>
#include <atomic>
#include <condition_variable>
#include <cstring>
#include <deque>
#include <mutex>
#include <thread>
#include <vector>

#ifndef ZLINK_TCP_NODELAY
#define ZLINK_TCP_NODELAY 26
#endif

namespace {

typedef std::chrono::steady_clock steady_clock_t;

struct pair_receive_event_t
{
    pair_receive_event_t () :
        payload_size (0),
        header_ok (false),
        valid (false)
    {
        std::memset (&header, 0, sizeof (header));
    }

    size_t payload_size;
    bool header_ok;
    bool valid;
    perf_single_metric::header_t header;
};

struct pair_receive_probe_t
{
    std::mutex mutex;
    std::condition_variable cv;
    std::deque<pair_receive_event_t> events;
    bool failed;
    bool closed;

    pair_receive_probe_t () : failed (false), closed (false) {}
};

static pair_receive_probe_t *g_pair_receive_probe = NULL;

inline zlink_socket_handler_t make_msg_handler (zlink_socket_msg_handler_fn fn_)
{
    zlink_socket_handler_t handler;
    std::memset (&handler, 0, sizeof (handler));
    handler.kind = ZLINK_SOCKET_HANDLER_MSG;
    handler.fn.msg = fn_;
    return handler;
}

void pair_receive_handler (const zlink_routing_id_t *,
                           zlink_msg_t *parts_,
                           size_t part_count_)
{
    pair_receive_probe_t *probe = g_pair_receive_probe;
    pair_receive_event_t event;
    event.valid = part_count_ == 1;

    if (event.valid && parts_) {
        event.payload_size = zlink_msg_size (&parts_[0]);
        event.header_ok = perf_single_metric::decode_payload_header (
          zlink_msg_data (&parts_[0]), event.payload_size, &event.header);
    }

    if (parts_) {
        for (size_t i = 0; i < part_count_; ++i)
            zlink_msg_close (&parts_[i]);
    }

    if (!probe)
        return;

    {
        std::lock_guard<std::mutex> guard (probe->mutex);
        if (!event.valid)
            probe->failed = true;
        probe->events.push_back (event);
    }
    probe->cv.notify_one ();
}

inline bool run_one_way_phase (void *sender,
                               void *receiver,
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
    if (!sender || !receiver || !payload || !seq || !out_received)
        return false;

    const bool active_phase = phase == perf_single_metric::phase_active;
    const auto deadline =
      active_phase
        ? steady_clock_t::now ()
            + seconds_t (duration_s > 0 ? duration_s : 1)
        : steady_clock_t::time_point ();
    const auto drain_idle_limit =
      milliseconds_t (recv_timeout_ms > 0 ? recv_timeout_ms : 200);

    std::atomic<bool> sender_done (false);
    std::atomic<bool> recv_failed (false);
    unsigned long long received = 0;
    latency_stats_builder_t latency_builder;
    pair_receive_probe_t probe;
    g_pair_receive_probe = &probe;

    std::thread receiver_thread ([&] () {
        auto last_recv_at = steady_clock_t::now ();

        auto account_header =
          [&] (const perf_single_metric::header_t &header, bool header_ok) {
              if (active_phase && queue_probe)
                  queue_probe->sample_recv_if_due ();

              if (!header_ok || header.magic != perf_single_metric::k_magic
                  || header.phase != static_cast<uint32_t> (phase)) {
                  return;
              }

              if (active_phase) {
                  if (steady_clock_t::now () < deadline) {
                      ++received;
                      const uint64_t now = perf_single_metric::now_us ();
                      const double latency_us =
                        now >= header.sent_ts_us
                          ? static_cast<double> (now - header.sent_ts_us)
                          : 0.0;
                      latency_builder.add (latency_us);
                  }
              } else {
                  ++received;
              }
          };

        if (active_phase && queue_probe)
            queue_probe->force_sample_recv ();

        while (true) {
            pair_receive_event_t event;
            bool have_event = false;
            {
                std::unique_lock<std::mutex> guard (probe.mutex);
                if (probe.events.empty ()) {
                    const bool done = sender_done.load (std::memory_order_acquire);
                    if (done) {
                        probe.cv.wait_for (guard, drain_idle_limit);
                    } else {
                        probe.cv.wait_for (guard, std::chrono::milliseconds (10));
                    }
                }
                if (!probe.events.empty ()) {
                    event = probe.events.front ();
                    probe.events.pop_front ();
                    have_event = true;
                } else if (probe.failed) {
                    recv_failed.store (true, std::memory_order_release);
                    break;
                }
            }

            if (have_event) {
                if (!event.valid || event.payload_size != payload_size) {
                    recv_failed.store (true, std::memory_order_release);
                    break;
                }
                last_recv_at = steady_clock_t::now ();
                account_header (event.header, event.header_ok);
                continue;
            }

            if (sender_done.load (std::memory_order_acquire)
                && steady_clock_t::now () - last_recv_at >= drain_idle_limit) {
                break;
            }
        }

        if (active_phase && queue_probe)
            queue_probe->force_sample_recv ();
    });

    if (active_phase && queue_probe)
        queue_probe->force_sample_send ();
    bool send_failed = false;
    if (active_phase) {
        while (steady_clock_t::now () < deadline) {
            const uint64_t sent_ts = perf_single_metric::now_us ();
            if (!perf_single_metric::stamp_payload (payload->data (),
                                                    payload_size,
                                                    run_id,
                                                    phase,
                                                    msg_size,
                                                    (*seq)++,
                                                    sent_ts)
                || !send_exact (sender, payload->data (), payload_size, 0)) {
                send_failed = true;
                break;
            }
            if (queue_probe)
                queue_probe->sample_send_if_due ();
        }
    } else {
        for (int i = 0; i < warmup_count; ++i) {
            if (!perf_single_metric::stamp_payload (
                  payload->data (),
                  payload_size,
                  run_id,
                  phase,
                  msg_size,
                  (*seq)++,
                  perf_single_metric::now_us ())
                || !send_exact (sender, payload->data (), payload_size, 0)) {
                send_failed = true;
                break;
            }
        }
    }

    if (active_phase && queue_probe)
        queue_probe->force_sample_send ();

    sender_done.store (true, std::memory_order_release);
    probe.cv.notify_all ();
    receiver_thread.join ();
    g_pair_receive_probe = NULL;

    if (send_failed || recv_failed.load (std::memory_order_acquire))
        return false;

    *out_received = received;
    if (active_phase) {
        if (received == 0 || latency_builder.count () == 0 || !out_latency)
            return false;
        *out_latency = latency_builder.snapshot ();
    } else if (received < static_cast<unsigned long long> (warmup_count)) {
        return false;
    }

    return true;
}

} // namespace

void run_pair (const std::string &transport,
               size_t msg_size,
               const std::string &lib_name)
{
    if (!transport_available (transport))
        return;

    auto print_fail_no_queue = [&] () {
        print_fail_result (lib_name, "PAIR", transport, msg_size);
    };

    ctx_guard_t ctx;
    if (!ctx.valid ()) {
        print_fail_no_queue ();
        return;
    }

    const zlink_socket_handler_t recv_handler = make_msg_handler (
      &pair_receive_handler);
    void *s_bind = zlink_socket (ctx.get (), ZLINK_PAIR, &recv_handler);
    void *s_conn = zlink_socket (ctx.get (), ZLINK_PAIR);
    if (!s_bind || !s_conn) {
        if (s_bind)
            zlink_close (s_bind);
        if (s_conn)
            zlink_close (s_conn);
        print_fail_no_queue ();
        return;
    }

    int nodelay = 1;
    set_sockopt_int (s_bind, ZLINK_TCP_NODELAY, nodelay,
                     "ZLINK_TCP_NODELAY");
    set_sockopt_int (s_conn, ZLINK_TCP_NODELAY, nodelay,
                     "ZLINK_TCP_NODELAY");

    if (!setup_connected_pair (s_bind, s_conn, transport, lib_name + "_pair")) {
        zlink_close (s_conn);
        zlink_close (s_bind);
        print_fail_no_queue ();
        return;
    }

    const int recv_timeout_ms = resolve_single_recv_timeout_ms ();
    set_sockopt_int (s_bind, ZLINK_RCVTIMEO, recv_timeout_ms,
                     "ZLINK_RCVTIMEO");
    set_sockopt_int (s_conn, ZLINK_RCVTIMEO, recv_timeout_ms,
                     "ZLINK_RCVTIMEO");

    const size_t payload_size =
      std::max<size_t> (msg_size, perf_single_metric::header_size ());
    std::vector<char> payload (payload_size, 'a');
    queue_probe_t queue_probe (s_conn, s_bind);

    auto print_fail_with_queue = [&] () {
        print_fail_result (lib_name, "PAIR", transport, msg_size, &queue_probe);
    };

    const uint32_t run_id = static_cast<uint32_t> (perf_single_metric::now_us ());
    uint64_t seq = 1;

    unsigned long long warmup_received = 0;
    const int warmup_count = resolve_bench_count ("PERF_WARMUP_COUNT", 1000);
    if (!run_one_way_phase (s_conn,
                            s_bind,
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
        zlink_close (s_conn);
        zlink_close (s_bind);
        return;
    }

    const int duration_s = std::max (1, resolve_single_duration_seconds ());
    unsigned long long received = 0;
    latency_stats_t latency_stats;
    if (!run_one_way_phase (s_conn,
                            s_bind,
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
        zlink_close (s_conn);
        zlink_close (s_bind);
        return;
    }

    const double throughput =
      static_cast<double> (received) / static_cast<double> (duration_s);
    const queue_stats_t queue_stats = queue_probe.snapshot ();

    print_result (lib_name,
                  "PAIR",
                  transport,
                  msg_size,
                  throughput,
                  latency_stats.mean_us,
                  latency_stats.p95_us,
                  latency_stats.p99_us,
                  queue_stats);
    zlink_close (s_conn);
    zlink_close (s_bind);
}

int main (int argc, char **argv)
{
    return run_standard_bench_main (argc, argv, run_pair);
}
