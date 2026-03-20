#include "../common/bench_common.hpp"
#include "../common/perf_single_metric_header.hpp"
#include <zlink.h>
#include <algorithm>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <mutex>
#include <vector>

namespace {

inline int recv_single_part_header_flags (void *socket,
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
    const int rc = ::zlink_recv (
      socket, &source_rid, &parts, &part_count,
      static_cast<zlink_send_flags_t> (flags));
    if (rc < 0) {
        const int err = zlink_errno ();
        if (err == EAGAIN || err == EINTR)
            return 0;
        return -1;
    }

    if (part_count < 1) {
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

struct recv_state_t {
    recv_state_t () :
        expected_payload_size (0),
        current_phase (0),
        active_phase (false),
        deadline (),
        queue_probe (NULL),
        received (0),
        latency_builder (),
        sender_done (false),
        recv_failed (false),
        total_dispatched (0)
    {}

    // Phase configuration (set before phase, read-only during phase)
    size_t expected_payload_size;
    uint32_t current_phase;
    bool active_phase;
    std::chrono::steady_clock::time_point deadline;
    queue_probe_t *queue_probe;

    // Recv stats (written by handler on I/O thread, read after completion)
    unsigned long long received;
    latency_stats_builder_t latency_builder;

    // Drain coordination
    std::atomic<bool> sender_done;
    std::atomic<bool> recv_failed;

    // Completion signaling
    std::mutex done_mutex;
    std::condition_variable done_cv;
    std::atomic<unsigned long long> total_dispatched;

private:
    recv_state_t (const recv_state_t &);
    recv_state_t &operator= (const recv_state_t &);
};

inline void pair_recv_handler (const zlink_routing_id_t *,
                               zlink_msg_t *parts_,
                               size_t part_count_,
                               void *userdata_)
{
    recv_state_t *state = static_cast<recv_state_t *> (userdata_);

    // Validate: expect exactly one part
    if (part_count_ != 1) {
        for (size_t i = 0; i < part_count_; ++i)
            zlink_msg_close (&parts_[i]);
        state->recv_failed.store (true, std::memory_order_release);
        if (state->sender_done.load (std::memory_order_acquire)) {
            std::lock_guard<std::mutex> lock (state->done_mutex);
            state->done_cv.notify_one ();
        }
        return;
    }

    const size_t actual_size = zlink_msg_size (&parts_[0]);
    const bool size_ok = actual_size == state->expected_payload_size;

    perf_single_metric::header_t header;
    bool header_ok = false;
    if (size_ok) {
        header_ok = perf_single_metric::decode_payload_header (
          zlink_msg_data (&parts_[0]), actual_size, &header);
    }

    zlink_msg_close (&parts_[0]);

    if (!size_ok) {
        state->recv_failed.store (true, std::memory_order_release);
        if (state->sender_done.load (std::memory_order_acquire)) {
            std::lock_guard<std::mutex> lock (state->done_mutex);
            state->done_cv.notify_one ();
        }
        return;
    }

    // Account header
    if (state->active_phase && state->queue_probe)
        state->queue_probe->sample_recv_if_due ();

    if (header_ok && header.magic == perf_single_metric::k_magic
        && header.phase == state->current_phase) {
        if (state->active_phase) {
            if (std::chrono::steady_clock::now () < state->deadline) {
                ++state->received;
                const uint64_t now = perf_single_metric::now_us ();
                const double latency_us =
                  now >= header.sent_ts_us
                    ? static_cast<double> (now - header.sent_ts_us)
                    : 0.0;
                state->latency_builder.add (latency_us);
            }
        } else {
            ++state->received;
        }
    }

    state->total_dispatched.fetch_add (1, std::memory_order_release);
    if (state->sender_done.load (std::memory_order_acquire)) {
        std::lock_guard<std::mutex> lock (state->done_mutex);
        state->done_cv.notify_one ();
    }
}

inline bool run_oneway_phase_callback (void *sender,
                                       recv_state_t *state,
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
    if (!sender || !state || !payload || !seq || !out_received)
        return false;

    const bool active_phase = phase == perf_single_metric::phase_active;
    const auto deadline =
      std::chrono::steady_clock::now ()
        + std::chrono::seconds (duration_s > 0 ? duration_s : 1);
    const auto drain_idle_limit = std::chrono::milliseconds (
      recv_timeout_ms > 0 ? recv_timeout_ms : 200);

    // Configure recv state for this phase
    state->expected_payload_size = payload_size;
    state->current_phase = static_cast<uint32_t> (phase);
    state->active_phase = active_phase;
    state->deadline = deadline;
    state->queue_probe = queue_probe;
    state->received = 0;
    state->latency_builder = latency_stats_builder_t ();
    state->sender_done.store (false, std::memory_order_release);
    state->recv_failed.store (false, std::memory_order_relaxed);
    state->total_dispatched.store (0, std::memory_order_relaxed);

    if (active_phase && queue_probe)
        queue_probe->force_sample_recv ();

    // Send phase
    bool send_failed = false;
    if (active_phase && queue_probe)
        queue_probe->force_sample_send ();

    while (std::chrono::steady_clock::now () < deadline) {
        const uint64_t sent_ts = perf_single_metric::now_us ();
        if (!perf_single_metric::stamp_payload (payload->data (),
                                                payload_size,
                                                run_id,
                                                phase,
                                                msg_size,
                                                (*seq)++,
                                                sent_ts)) {
            if (bench_debug_enabled ())
                std::cerr << "[perf-pair] stamp failed" << std::endl;
            send_failed = true;
            break;
        }

        zlink_msg_t part;
        if (::zlink_msg_init_size (&part, payload_size) != 0) {
            if (bench_debug_enabled ())
                std::cerr << "[perf-pair] msg init failed err="
                          << zlink_errno () << std::endl;
            send_failed = true;
            break;
        }
        if (payload_size > 0)
            memcpy (::zlink_msg_data (&part), payload->data (), payload_size);
        if (::zlink_send (
              sender, &part, 1, static_cast<zlink_send_flags_t> (0))
            < 0) {
            ::zlink_msg_close (&part);
            send_failed = true;
            break;
        }
        if (active_phase && queue_probe)
            queue_probe->sample_send_if_due ();
    }

    if (active_phase && queue_probe)
        queue_probe->force_sample_send ();

    // Signal sender done and wait for drain
    state->sender_done.store (true, std::memory_order_release);

    {
        std::unique_lock<std::mutex> lock (state->done_mutex);
        auto drain_deadline =
          std::chrono::steady_clock::now () + drain_idle_limit;
        unsigned long long prev_dispatched =
          state->total_dispatched.load (std::memory_order_acquire);

        while (!state->recv_failed.load (std::memory_order_acquire)) {
            const auto status =
              state->done_cv.wait_until (lock, drain_deadline);
            const unsigned long long curr_dispatched =
              state->total_dispatched.load (std::memory_order_acquire);

            if (state->recv_failed.load (std::memory_order_acquire))
                break;

            if (curr_dispatched != prev_dispatched) {
                prev_dispatched = curr_dispatched;
                drain_deadline =
                  std::chrono::steady_clock::now () + drain_idle_limit;
                continue;
            }

            if (status == std::cv_status::timeout
                || std::chrono::steady_clock::now () >= drain_deadline) {
                break;
            }
        }
    }

    if (active_phase && queue_probe)
        queue_probe->force_sample_recv ();

    if (send_failed || state->recv_failed.load (std::memory_order_acquire))
        return false;

    *out_received = state->received;

    if (active_phase) {
        if (state->received == 0 || state->latency_builder.count () == 0
            || !out_latency)
            return false;
        *out_latency = state->latency_builder.snapshot ();
    } else if (state->received == 0) {
        return false;
    }

    return true;
}

inline bool run_oneway_phase_recv (void *sender,
                                   void *receiver,
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
    if (!sender || !receiver || !payload || !seq || !out_received)
        return false;

    const bool active_phase = phase == perf_single_metric::phase_active;
    const auto deadline =
      std::chrono::steady_clock::now ()
      + std::chrono::seconds (duration_s > 0 ? duration_s : 1);
    const auto drain_idle_limit = std::chrono::milliseconds (
      recv_timeout_ms > 0 ? recv_timeout_ms : 200);

    std::atomic<bool> sender_done (false);
    std::atomic<bool> recv_failed (false);
    std::atomic<unsigned long long> received (0);
    latency_stats_builder_t latency_builder;

    std::thread receiver_thread ([&] () {
        auto last_recv_at = std::chrono::steady_clock::now ();

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

            perf_single_metric::header_t header;
            bool header_ok = false;
            const int recv_rc = recv_single_part_header_flags (
              receiver, payload_size, 0, &header, &header_ok);
            if (recv_rc > 0) {
                last_recv_at = std::chrono::steady_clock::now ();
                account_header (header, header_ok);

                for (;;) {
                    perf_single_metric::header_t burst_header;
                    bool burst_header_ok = false;
                    const int burst_rc = recv_single_part_header_flags (
                      receiver, payload_size, ZLINK_DONTWAIT, &burst_header,
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
    if (active_phase && queue_probe)
        queue_probe->force_sample_send ();

    while (std::chrono::steady_clock::now () < deadline) {
        const uint64_t sent_ts = perf_single_metric::now_us ();
        if (!perf_single_metric::stamp_payload (payload->data (),
                                                payload_size,
                                                run_id,
                                                phase,
                                                msg_size,
                                                (*seq)++,
                                                sent_ts)) {
            send_failed = true;
            break;
        }

        zlink_msg_t part;
        if (::zlink_msg_init_size (&part, payload_size) != 0) {
            send_failed = true;
            break;
        }
        if (payload_size > 0)
            memcpy (::zlink_msg_data (&part), payload->data (), payload_size);
        if (::zlink_send (
              sender, &part, 1, static_cast<zlink_send_flags_t> (0))
            < 0) {
            ::zlink_msg_close (&part);
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

    const bool callback_mode = single_perf_callback_mode ();
    recv_state_t recv_state;

    socket_guard_t s_bind (
      ctx.get (), ZLINK_SOCKET_PAIR,
      callback_mode ? &pair_recv_handler : NULL, &recv_state);
    socket_guard_t s_conn (ctx.get (), ZLINK_SOCKET_PAIR);
    if (!s_bind.valid () || !s_conn.valid ()) {
        print_fail_no_queue ();
        return;
    }

    int nodelay = 1;
    set_sockopt_int (s_bind.get (), ZLINK_OPT_TCP_NODELAY, nodelay,
                     "ZLINK_OPT_TCP_NODELAY");
    set_sockopt_int (s_conn.get (), ZLINK_OPT_TCP_NODELAY, nodelay,
                     "ZLINK_OPT_TCP_NODELAY");

    if (!setup_connected_pair (
          s_bind.get (), s_conn.get (), transport, lib_name + "_pair")) {
        print_fail_no_queue ();
        return;
    }

    const int recv_timeout_ms = resolve_single_recv_timeout_ms ();

    const size_t payload_size =
      std::max<size_t> (msg_size, perf_single_metric::header_size ());
    std::vector<char> payload (payload_size, 'a');
    queue_probe_t queue_probe (s_conn.get (), s_bind.get ());

    auto print_fail_with_queue = [&] () {
        print_fail_result (lib_name, "PAIR", transport, msg_size, &queue_probe);
    };

    const uint32_t run_id = static_cast<uint32_t> (perf_single_metric::now_us ());
    uint64_t seq = 1;

    unsigned long long warmup_received = 0;
    const int warmup_s = resolve_single_warmup_seconds ();
    const bool warmup_ok =
      callback_mode
        ? run_oneway_phase_callback (s_conn.get (),
                                     &recv_state,
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
                                     NULL)
        : run_oneway_phase_recv (s_conn.get (),
                                 s_bind.get (),
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
                                 NULL);
    if (!warmup_ok) {
        print_fail_with_queue ();
        return;
    }

    const int duration_s = std::max (1, resolve_single_duration_seconds ());
    unsigned long long received = 0;
    latency_stats_t latency_stats;
    const bool active_ok =
      callback_mode
        ? run_oneway_phase_callback (s_conn.get (),
                                     &recv_state,
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
                                     &latency_stats)
        : run_oneway_phase_recv (s_conn.get (),
                                 s_bind.get (),
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
                                 &latency_stats);
    if (!active_ok) {
        print_fail_with_queue ();
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
}

int main (int argc, char **argv)
{
    return run_standard_bench_main (argc, argv, run_pair);
}
