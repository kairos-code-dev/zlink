#include "../common/bench_common.hpp"
#include "../common/perf_single_metric_header.hpp"
#include <zlink.h>
#include <algorithm>
#include <atomic>
#include <chrono>
#include <thread>
#include <vector>

namespace {

struct dealer_router_callback_state_t
{
    dealer_router_callback_state_t () :
        run_id (0),
        msg_size (0),
        payload_size (0),
        active_deadline_us (0),
        fatal (false),
        active_received (0),
        callback_queue (NULL)
    {}

    uint32_t run_id;
    size_t msg_size;
    size_t payload_size;
    std::atomic<uint64_t> active_deadline_us;
    std::atomic<bool> fatal;
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

void dealer_router_recv_handler (const zlink_routing_id_t *source_rid_,
                                 zlink_msg_t *parts_,
                                 size_t part_count_,
                                 void *userdata_)
{
    dealer_router_callback_state_t *state =
      static_cast<dealer_router_callback_state_t *> (userdata_);
    if (!state) {
        close_parts (parts_, part_count_);
        return;
    }

    bool fatal = false;
    perf_single_metric::header_t header;
    bool header_ok = false;

    if (!source_rid_ || source_rid_->size == 0 || part_count_ != 1) {
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

inline bool setup_dealer_router_session (void *router,
                                         void *dealer,
                                         const std::string &transport,
                                         const std::string &pair_id)
{
    if (!router || !dealer)
        return false;

    zlink_set_routing_id (dealer, "CLIENT", 6);
    return setup_connected_pair (router, dealer, transport, pair_id);
}

inline bool run_active_phase (void *dealer,
                              std::vector<char> *payload,
                              dealer_router_callback_state_t *state,
                              uint64_t *seq,
                              int duration_s,
                              int recv_timeout_ms,
                              unsigned long long *out_received,
                              latency_stats_t *out_latency)
{
    if (!dealer || !payload || !state || !seq || !out_received
        || !out_latency) {
        return false;
    }

    const auto deadline =
      std::chrono::steady_clock::now ()
      + std::chrono::seconds (duration_s > 0 ? duration_s : 1);
    state->fatal.store (false, std::memory_order_release);
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
                                                state->payload_size,
                                                state->run_id,
                                                perf_single_metric::phase_active,
                                                state->msg_size,
                                                current_seq,
                                                sent_ts)) {
            send_failed = true;
            break;
        }

        zlink_msg_t part;
        if (::zlink_msg_init_size (&part, state->payload_size) != 0) {
            send_failed = true;
            break;
        }
        if (state->payload_size > 0) {
            memcpy (::zlink_msg_data (&part), payload->data (),
                    state->payload_size);
        }

        perf_send_class_t send_class = perf_send_fatal;
        while (true) {
            send_class = perf_classify_send_result (
              ::zlink_send (dealer, &part, 1, 0));
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

void run_dealer_router (const std::string &transport,
                        size_t msg_size,
                        const std::string &lib_name)
{
    if (!transport_available (transport))
        return;

    auto print_fail = [&] () {
        print_fail_result (lib_name, "DEALER_ROUTER", transport, msg_size);
    };

    ctx_guard_t ctx;
    if (!ctx.valid ()) {
        print_fail ();
        return;
    }

    socket_guard_t router (ctx.get (), ZLINK_SOCKET_ROUTER);
    socket_guard_t dealer (ctx.get (), ZLINK_SOCKET_DEALER);
    if (!router.valid () || !dealer.valid ()) {
        print_fail ();
        return;
    }

    const size_t payload_size =
      std::max<size_t> (msg_size, perf_single_metric::header_size ());
    std::vector<char> payload (payload_size, 'a');
    dealer_router_callback_state_t callback_state;
    single_callback_metric_queue_t callback_queue (65536);
    single_metric_worker_t<dealer_router_callback_state_t> metric_worker;

    callback_state.run_id =
      static_cast<uint32_t> (perf_single_metric::now_us ());
    callback_state.msg_size = msg_size;
    callback_state.payload_size = payload_size;
    callback_state.callback_queue = &callback_queue;
    metric_worker.state = &callback_state;
    metric_worker.queue = &callback_queue;
    if (zlink_recv_handler (
          router.get (), &dealer_router_recv_handler, &callback_state)
        != 0) {
        print_fail ();
        return;
    }
    if (!start_single_metric_worker (&metric_worker)) {
        print_fail ();
        return;
    }

    if (!setup_dealer_router_session (
          router.get (), dealer.get (), transport,
          lib_name + "_dealer_router")) {
        stop_single_metric_worker (&metric_worker);
        print_fail ();
        return;
    }

    const int recv_timeout_ms = resolve_single_recv_timeout_ms ();
    const int duration_s = std::max (1, resolve_single_duration_seconds ());
    uint64_t seq = 1;
    unsigned long long received = 0;
    latency_stats_t latency_stats;
    if (!run_active_phase (dealer.get (),
                           &payload,
                           &callback_state,
                           &seq,
                           duration_s,
                           recv_timeout_ms,
                           &received,
                           &latency_stats)) {
        stop_single_metric_worker (&metric_worker);
        print_fail ();
        return;
    }
    stop_single_metric_worker (&metric_worker);

    const double throughput =
      static_cast<double> (received) / static_cast<double> (duration_s);
    print_result (lib_name,
                  "DEALER_ROUTER",
                  transport,
                  msg_size,
                  throughput,
                  latency_stats.mean_us,
                  latency_stats.p95_us,
                  latency_stats.p99_us);
}

int main (int argc, char **argv)
{
    return run_standard_bench_main (
      argc, argv, "DEALER_ROUTER", run_dealer_router);
}
