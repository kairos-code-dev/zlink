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

static const size_t k_metric_queue_capacity = 65536;

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
        phase_wait_armed (false),
        active_received (0),
        callback_queue (NULL)
    {}

    uint32_t run_id;
    size_t msg_size;
    size_t payload_size;
    std::atomic<uint64_t> active_deadline_us;
    std::atomic<bool> fatal;
    std::atomic<bool> phase_wait_armed;
    std::atomic<unsigned long long> active_received;
    latency_stats_builder_t latency;
    single_callback_metric_queue_t *callback_queue;
    std::mutex mutex;
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

    if (header_ok && header.run_id == state->run_id
        && header.msg_size == state->msg_size) {
        single_note_callback_receive (state, header);
        (void) single_enqueue_metric_event (state, header);
    }
}

inline bool run_active_phase (void *sender,
                              std::vector<char> *payload,
                              size_t payload_size,
                              pair_callback_state_t *state,
                              uint64_t *seq,
                              int duration_s,
                              int recv_timeout_ms,
                              unsigned long long *out_received,
                              latency_stats_t *out_latency)
{
    if (!sender || !payload || !state || !seq || !out_received
        || !out_latency) {
        return false;
    }

    const auto deadline =
      std::chrono::steady_clock::now ()
      + std::chrono::seconds (duration_s > 0 ? duration_s : 1);
    state->fatal.store (false, std::memory_order_release);
    state->phase_wait_armed.store (false, std::memory_order_release);
    state->active_received.store (0, std::memory_order_release);
    state->active_deadline_us.store (
      perf_single_metric::now_us ()
        + static_cast<uint64_t> (std::max (1, duration_s) * 1000000ULL),
      std::memory_order_release);
    state->latency = latency_stats_builder_t ();

    bool send_failed = false;
    unsigned long long successful_send_count = 0;

    while (std::chrono::steady_clock::now () < deadline) {
        const uint64_t sent_ts = perf_single_metric::now_us ();
        const uint64_t current_seq = *seq;
        if (!perf_single_metric::stamp_payload (payload->data (),
                                                payload_size,
                                                state->run_id,
                                                perf_single_metric::phase_active,
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

        perf_send_class_t send_class = perf_send_fatal;
        while (true) {
            send_class = perf_classify_send_result (
              ::zlink_send (sender, &part, 1, 0));
            if (send_class != perf_send_retry)
                break;
        }

        if (send_class != perf_send_success) {
            ::zlink_msg_close (&part);
            send_failed = true;
            break;
        }

        ++successful_send_count;
        ++(*seq);
    }

    const int drain_timeout_ms =
      single_phase_completion_timeout_ms (duration_s, recv_timeout_ms);
    const bool drained = single_wait_for_phase_processed (
      *state,
      perf_single_metric::phase_active,
      successful_send_count,
      drain_timeout_ms);

    if (send_failed || !drained
        || state->fatal.load (std::memory_order_acquire)) {
        debug_pair ("phase failed before metrics were collected");
        return false;
    }

    *out_received = state->active_received.load (std::memory_order_relaxed);
    state->active_deadline_us.store (0, std::memory_order_release);
    if (*out_received == 0) {
        return false;
    }
    *out_latency = state->latency.snapshot ();
    return state->latency.count () > 0;
}

} // namespace

template <>
inline void single_set_phase_wait_armed<pair_callback_state_t> (
  pair_callback_state_t &state_, bool value_)
{
    state_.phase_wait_armed.store (value_, std::memory_order_relaxed);
}

template <>
inline bool single_phase_wait_notify_armed<pair_callback_state_t> (
  const pair_callback_state_t &state_)
{
    return state_.phase_wait_armed.load (std::memory_order_relaxed);
}

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
    pair_callback_state_t callback_state;
    single_callback_metric_queue_t callback_queue (k_metric_queue_capacity);
    single_metric_worker_t<pair_callback_state_t> metric_worker;

    callback_state.run_id =
      static_cast<uint32_t> (perf_single_metric::now_us ());
    callback_state.msg_size = msg_size;
    callback_state.payload_size = payload_size;
    callback_state.callback_queue = &callback_queue;
    metric_worker.state = &callback_state;
    metric_worker.queue = &callback_queue;
    if (zlink_recv_handler (s_bind.get (), &pair_recv_handler, &callback_state)
        != 0) {
        print_fail_no_queue ();
        return;
    }
    if (!start_single_metric_worker (&metric_worker)) {
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
        stop_single_metric_worker (&metric_worker);
        print_fail_no_queue ();
        return;
    }

    const int recv_timeout_ms = resolve_single_recv_timeout_ms ();
    const int duration_s = std::max (1, resolve_single_duration_seconds ());
    uint64_t seq = 1;
    unsigned long long received = 0;
    latency_stats_t latency_stats;
    const bool active_ok = run_active_phase (
      s_conn.get (),
      &payload,
      payload_size,
      &callback_state,
      &seq,
      duration_s,
      recv_timeout_ms,
      &received,
      &latency_stats);
    if (!active_ok) {
        stop_single_metric_worker (&metric_worker);
        print_fail_no_queue ();
        return;
    }
    stop_single_metric_worker (&metric_worker);

    const double throughput =
      static_cast<double> (received) / static_cast<double> (duration_s);
    print_result (lib_name,
                  "PAIR",
                  transport,
                  msg_size,
                  throughput,
                  latency_stats.mean_us,
                  latency_stats.p95_us,
                  latency_stats.p99_us);
}

int main (int argc, char **argv)
{
    return run_standard_bench_main (argc, argv, "PAIR", run_pair);
}
