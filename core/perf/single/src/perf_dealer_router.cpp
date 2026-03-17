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

inline void router_recv_handler (const zlink_routing_id_t *,
                                  zlink_msg_t *parts_,
                                  size_t part_count_,
                                  void *userdata_)
{
    recv_state_t *state = static_cast<recv_state_t *> (userdata_);

    // Validate: expect exactly one payload part (routing id is separate)
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

inline bool setup_dealer_router_session (void *router,
                                         void *dealer,
                                         const std::string &transport,
                                         const std::string &pair_id)
{
    if (!router || !dealer)
        return false;

    zlink_setsockopt (dealer, ZLINK_ROUTING_ID, "CLIENT", 6);
    if (!setup_connected_pair (router, dealer, transport, pair_id))
        return false;

    return true;
}

inline bool run_oneway_phase (void *dealer,
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
    if (!dealer || !state || !payload || !seq || !out_received)
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
                                                sent_ts)
            || !send_exact (dealer, payload->data (), payload_size, 0)) {
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

} // namespace

void run_dealer_router (const std::string &transport,
                        size_t msg_size,
                        const std::string &lib_name)
{
    if (!transport_available (transport))
        return;

    auto print_fail_no_queue = [&] () {
        print_fail_result (lib_name, "DEALER_ROUTER", transport, msg_size);
    };

    ctx_guard_t ctx;
    if (!ctx.valid ()) {
        print_fail_no_queue ();
        return;
    }

    recv_state_t recv_state;

    zlink_socket_handler_t handler;
    memset (&handler, 0, sizeof (handler));
    handler.kind = ZLINK_SOCKET_HANDLER_MSG;
    handler.fn.msg = &router_recv_handler;
    handler.userdata = &recv_state;

    socket_guard_t router (ctx.get (), ZLINK_ROUTER, &handler);
    socket_guard_t dealer (ctx.get (), ZLINK_DEALER);
    if (!router.valid () || !dealer.valid ()) {
        print_fail_no_queue ();
        return;
    }

    if (!setup_dealer_router_session (
          router.get (), dealer.get (), transport, lib_name + "_dealer_router")) {
        print_fail_no_queue ();
        return;
    }

    const int recv_timeout_ms = resolve_single_recv_timeout_ms ();
    const size_t payload_size =
      std::max<size_t> (msg_size, perf_single_metric::header_size ());
    std::vector<char> payload (payload_size, 'a');
    queue_probe_t queue_probe (dealer.get (), router.get ());

    auto print_fail_with_queue = [&] () {
        print_fail_result (
          lib_name, "DEALER_ROUTER", transport, msg_size, &queue_probe);
    };

    const uint32_t run_id = static_cast<uint32_t> (perf_single_metric::now_us ());
    uint64_t seq = 1;

    unsigned long long warmup_received = 0;
    const int warmup_s = resolve_single_warmup_seconds ();
    if (!run_oneway_phase (dealer.get (),
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
                           NULL)) {
        print_fail_with_queue ();
        return;
    }

    const int duration_s = std::max (1, resolve_single_duration_seconds ());
    unsigned long long received = 0;
    latency_stats_t latency_stats;
    if (!run_oneway_phase (dealer.get (),
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
                           &latency_stats)) {
        print_fail_with_queue ();
        return;
    }

    const double throughput =
      static_cast<double> (received) / static_cast<double> (duration_s);
    const queue_stats_t queue_stats = queue_probe.snapshot ();

    print_result (lib_name,
                  "DEALER_ROUTER",
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
    return run_standard_bench_main (argc, argv, run_dealer_router);
}
