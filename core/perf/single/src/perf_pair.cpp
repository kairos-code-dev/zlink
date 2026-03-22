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

inline void debug_pair (const char *message_)
{
    if (bench_debug_enabled ())
        std::cerr << "[perf-pair] " << message_ << std::endl;
}

struct pair_callback_state_t
{
    pair_callback_state_t () :
        run_id (0),
        msg_size (0),
        payload_size (0),
        active_deadline_us (0),
        fatal (false),
        warmup_received (0),
        active_received (0),
        probe (NULL),
        callback_queue (NULL)
    {}

    uint32_t run_id;
    size_t msg_size;
    size_t payload_size;
    std::atomic<uint64_t> active_deadline_us;
    std::atomic<bool> fatal;
    std::atomic<unsigned long long> warmup_received;
    std::atomic<unsigned long long> active_received;
    latency_stats_builder_t latency;
    queue_probe_t *probe;
    single_callback_metric_queue_t *callback_queue;
    std::mutex mutex;
    std::mutex latency_mutex;
    std::condition_variable cv;
};

inline void close_parts (zlink_msg_t *parts_, size_t part_count_)
{
    if (!parts_)
        return;
    zlink_multipart_close (parts_, part_count_);
}

void pair_recv_handler (const zlink_routing_id_t *,
                        zlink_msg_t *parts_,
                        size_t part_count_,
                        void *userdata_)
{
    pair_callback_state_t *state =
      static_cast<pair_callback_state_t *> (userdata_);
    if (!state) {
        close_parts (parts_, part_count_);
        return;
    }

    bool fatal = false;
    perf_single_metric::header_t header;
    bool header_ok = false;

    if (part_count_ != 1) {
        fatal = true;
    } else {
        const size_t actual_size = zlink_msg_size (&parts_[0]);
        if (actual_size != state->payload_size) {
            fatal = true;
        } else {
            header_ok = perf_single_metric::decode_payload_header (
              zlink_msg_data (&parts_[0]), actual_size, &header);
        }
    }

    close_parts (parts_, part_count_);

    if (fatal) {
        single_mark_callback_fatal (state);
        return;
    }

    single_note_callback_receive (state);
    if (header_ok && header.run_id == state->run_id
        && header.msg_size == state->msg_size)
        (void) single_enqueue_metric_event (state, header);
}

inline int send_single_part_blocking (void *socket, zlink_msg_t *part)
{
    if (!socket || !part)
        return -1;

    while (true) {
        if (::zlink_send (socket, part, 1, 0) >= 0)
            return 1;

        const int err = zlink_errno ();
        if (err == EINTR)
            continue;
        return err == EAGAIN ? 0 : -1;
    }
}

inline bool run_oneway_phase_callback (void *sender,
                                       std::vector<char> *payload,
                                       size_t payload_size,
                                       pair_callback_state_t *state,
                                       uint64_t *seq,
                                       perf_single_metric::phase_t phase,
                                       int duration_s,
                                       int recv_timeout_ms,
                                       queue_probe_t *queue_probe,
                                       unsigned long long *out_received,
                                       latency_stats_t *out_latency)
{
    if (!sender || !payload || !state || !seq || !out_received)
        return false;

    const bool active_phase = phase == perf_single_metric::phase_active;
    const auto deadline =
      std::chrono::steady_clock::now ()
      + std::chrono::seconds (duration_s > 0 ? duration_s : 1);
    state->fatal.store (false, std::memory_order_release);
    state->warmup_received.store (0, std::memory_order_release);
    state->active_received.store (0, std::memory_order_release);
    state->active_deadline_us.store (
      active_phase
        ? perf_single_metric::now_us ()
            + static_cast<uint64_t> (std::max (1, duration_s) * 1000000ULL)
        : 0,
      std::memory_order_release);
    state->probe = queue_probe;
    {
        std::lock_guard<std::mutex> lock (state->latency_mutex);
        state->latency = latency_stats_builder_t ();
    }

    bool send_failed = false;
    unsigned long long successful_send_count = 0;
    if (active_phase && queue_probe)
        queue_probe->force_sample_send ();

    while (std::chrono::steady_clock::now () < deadline) {
        const uint64_t sent_ts = perf_single_metric::now_us ();
        const uint64_t current_seq = *seq;
        if (!perf_single_metric::stamp_payload (payload->data (),
                                                payload_size,
                                                state->run_id,
                                                phase,
                                                state->msg_size,
                                                current_seq,
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
        const int send_rc = send_single_part_blocking (sender, &part);
        if (send_rc < 0) {
            ::zlink_msg_close (&part);
            send_failed = true;
            break;
        }
        if (send_rc == 0) {
            ::zlink_msg_close (&part);
            if (!single_wait_for_send_backpressure (queue_probe)) {
                send_failed = true;
                break;
            }
            continue;
        }
        ++successful_send_count;
        ++(*seq);
        if (active_phase && queue_probe)
            queue_probe->sample_send_if_due ();
    }

    if (active_phase && queue_probe)
        queue_probe->force_sample_send ();

    const int drain_timeout_ms =
      single_phase_drain_timeout_ms (duration_s, recv_timeout_ms);
    const bool drained = single_wait_for_phase_processed (
      *state, phase, successful_send_count, drain_timeout_ms);

    if (send_failed || !drained
        || state->fatal.load (std::memory_order_acquire)) {
        debug_pair ("phase failed before metrics were collected");
        return false;
    }

    *out_received = active_phase
                      ? state->active_received.load (std::memory_order_relaxed)
                      : state->warmup_received.load (std::memory_order_relaxed);

    if (active_phase) {
        state->active_deadline_us.store (0, std::memory_order_release);
        if (*out_received == 0 || !out_latency) {
            return false;
        }
        std::lock_guard<std::mutex> lock (state->latency_mutex);
        *out_latency = state->latency.snapshot ();
        if (state->latency.count () == 0)
            return false;
    } else if (*out_received == 0) {
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

    socket_guard_t s_bind (ctx.get (), ZLINK_SOCKET_PAIR);
    socket_guard_t s_conn (ctx.get (), ZLINK_SOCKET_PAIR);
    if (!s_bind.valid () || !s_conn.valid ()) {
        print_fail_no_queue ();
        return;
    }

    const size_t payload_size =
      std::max<size_t> (msg_size, perf_single_metric::header_size ());
    std::vector<char> payload (payload_size, 'a');
    queue_probe_t queue_probe (s_conn.get (), s_bind.get ());
    pair_callback_state_t callback_state;
    single_callback_metric_queue_t callback_queue (65536);
    single_metric_worker_t<pair_callback_state_t> metric_worker;

    auto print_fail_with_queue = [&] () {
        print_fail_result (lib_name, "PAIR", transport, msg_size, &queue_probe);
    };

    callback_state.run_id =
      static_cast<uint32_t> (perf_single_metric::now_us ());
    callback_state.msg_size = msg_size;
    callback_state.payload_size = payload_size;
    callback_state.callback_queue = &callback_queue;
    metric_worker.state = &callback_state;
    metric_worker.queue = &callback_queue;
    if (zlink_recv_handler (s_bind.get (), &pair_recv_handler, &callback_state)
        != 0) {
        print_fail_with_queue ();
        return;
    }
    if (!start_single_metric_worker (&metric_worker)) {
        print_fail_with_queue ();
        return;
    }

    int nodelay = 1;
    set_sockopt_int (s_bind.get (), ZLINK_OPT_TCP_NODELAY, nodelay,
                     "ZLINK_OPT_TCP_NODELAY");
    set_sockopt_int (s_conn.get (), ZLINK_OPT_TCP_NODELAY, nodelay,
                     "ZLINK_OPT_TCP_NODELAY");

    if (!setup_connected_pair (
          s_bind.get (), s_conn.get (), transport, lib_name + "_pair")) {
        stop_single_metric_worker (&metric_worker);
        print_fail_with_queue ();
        return;
    }

    const int recv_timeout_ms = resolve_single_recv_timeout_ms ();

    uint64_t seq = 1;

    unsigned long long warmup_received = 0;
    const int warmup_s = resolve_single_warmup_seconds ();
    const bool warmup_ok = run_oneway_phase_callback (
      s_conn.get (), &payload, payload_size, &callback_state, &seq,
      perf_single_metric::phase_warmup, warmup_s, recv_timeout_ms, NULL,
      &warmup_received, NULL);
    if (!warmup_ok) {
        stop_single_metric_worker (&metric_worker);
        print_fail_with_queue ();
        return;
    }

    const int duration_s = std::max (1, resolve_single_duration_seconds ());
    unsigned long long received = 0;
    latency_stats_t latency_stats;
    const bool active_ok = run_oneway_phase_callback (
      s_conn.get (), &payload, payload_size, &callback_state, &seq,
      perf_single_metric::phase_active, duration_s, recv_timeout_ms,
      &queue_probe, &received, &latency_stats);
    stop_single_metric_worker (&metric_worker);
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
    return run_standard_bench_main (argc, argv, "PAIR", run_pair);
}
