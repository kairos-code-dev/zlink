#include "../common/bench_common.hpp"
#include "../common/perf_single_metric_header.hpp"

#include <zlink.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdlib>
#include <cstring>
#include <vector>

namespace {

static const char *k_pubsub_topic = "bench";

inline int resolve_pubsub_xpub_nodrop_opt()
{
    const char *env = std::getenv("PERF_SINGLE_PUBSUB_XPUB_NODROP");
    return (env && std::strcmp(env, "0") == 0) ? 0 : 1;
}

struct pubsub_callback_state_t
{
    pubsub_callback_state_t() :
        run_id(0),
        msg_size(0),
        payload_size(0),
        active_deadline_us(0),
        fatal(false),
        warmup_received(0),
        active_received(0),
        warmup_processed(0),
        active_processed(0),
        probe(NULL),
        callback_queue(NULL)
    {
    }

    uint32_t run_id;
    size_t msg_size;
    size_t payload_size;
    std::atomic<uint64_t> active_deadline_us;
    std::atomic<bool> fatal;
    std::atomic<unsigned long long> warmup_received;
    std::atomic<unsigned long long> active_received;
    std::atomic<unsigned long long> warmup_processed;
    std::atomic<unsigned long long> active_processed;
    latency_stats_builder_t latency;
    queue_probe_t *probe;
    single_callback_metric_queue_t *callback_queue;
    std::mutex mutex;
    std::condition_variable cv;
};

inline void close_parts(zlink_msg_t *parts_, size_t part_count_)
{
    if (parts_)
        zlink_multipart_close(parts_, part_count_);
}

bool decode_pubsub_metric_header(pubsub_callback_state_t *state_,
                                 const char *topic_,
                                 size_t topic_len_,
                                 zlink_msg_t *parts_,
                                 size_t part_count_,
                                 perf_single_metric::header_t *header_out_)
{
    if (!state_ || !topic_ || !parts_ || part_count_ != 1 || !header_out_)
        return false;
    const bool topic_ok =
      topic_len_ == std::strlen(k_pubsub_topic)
      && std::memcmp(topic_, k_pubsub_topic, topic_len_) == 0;
    if (!topic_ok || zlink_msg_size(&parts_[0]) != state_->payload_size)
        return false;
    return perf_single_metric::decode_payload_header(
             zlink_msg_data(&parts_[0]), state_->payload_size, header_out_)
           && header_out_->run_id == state_->run_id
           && header_out_->msg_size == state_->msg_size;
}

void pubsub_recv_handler(const zlink_routing_id_t *,
                         const char *topic_,
                         size_t topic_len_,
                         zlink_msg_t *parts_,
                         size_t part_count_,
                         void *userdata_)
{
    pubsub_callback_state_t *state =
      static_cast<pubsub_callback_state_t *>(userdata_);
    perf_single_metric::header_t header;
    const bool header_ok = decode_pubsub_metric_header(
      state, topic_, topic_len_, parts_, part_count_, &header);
    close_parts(parts_, part_count_);
    if (!state || !header_ok)
        return;
    single_note_callback_receive(state, header);
    (void) single_enqueue_metric_event(state, header);
}

bool send_pubsub_sample(void *pub_socket_,
                        std::vector<char> *payload_,
                        size_t payload_size_,
                        size_t msg_size_,
                        uint32_t run_id_,
                        uint64_t *seq_,
                        perf_single_metric::phase_t phase_)
{
    const uint64_t sent_ts = perf_single_metric::now_us();
    if (!perf_single_metric::stamp_payload(payload_->data(),
                                           payload_size_,
                                           run_id_,
                                           phase_,
                                           msg_size_,
                                           *seq_,
                                           sent_ts)) {
        return false;
    }
    zlink_msg_t part;
    if (zlink_msg_init_size(&part, payload_size_) != 0)
        return false;
    std::memcpy(zlink_msg_data(&part), payload_->data(), payload_size_);
    if (zlink_publish(pub_socket_, k_pubsub_topic, &part, 1, 0) != 0) {
        zlink_msg_close(&part);
        return false;
    }
    ++(*seq_);
    return true;
}

bool setup_connected_pubsub_pair(void *pub_socket_,
                                 void *sub_socket_,
                                 const std::string &transport_,
                                 const std::string &id_)
{
    if (!setup_tls_server(pub_socket_, transport_)
        || !setup_tls_client(sub_socket_, transport_))
        return false;
    apply_single_hwm(pub_socket_);
    apply_single_hwm(sub_socket_);
    if (zlink_set_subscription(sub_socket_, "") != 0)
        return false;
    const std::string endpoint =
      bind_and_resolve_endpoint(pub_socket_, transport_, id_);
    if (endpoint.empty())
        return false;
    void *sub_monitor = open_configured_socket_monitor(
      sub_socket_, ZLINK_EVENT_CONNECTION_READY);
    void *pub_monitor = open_configured_socket_monitor(
      pub_socket_, ZLINK_EVENT_CONNECTION_READY);
    if (!sub_monitor || !pub_monitor || !connect_checked(sub_socket_, endpoint))
        return false;
    apply_single_benchmark_socket_options(pub_socket_, transport_);
    apply_single_benchmark_socket_options(sub_socket_, transport_);
    const int timeout_ms =
      parse_positive_env("PERF_CONNECT_READY_TIMEOUT_MS", 3000);
    const bool sub_ready = wait_for_socket_monitor_event(
      sub_monitor, ZLINK_EVENT_CONNECTION_READY, timeout_ms);
    const bool pub_ready = wait_for_socket_monitor_event(
      pub_monitor, ZLINK_EVENT_CONNECTION_READY, timeout_ms);
    zlink_monitor_close(&sub_monitor);
    zlink_monitor_close(&pub_monitor);
    return sub_ready && pub_ready;
}

bool run_oneway_phase(void *pub_socket,
                      std::vector<char> *payload,
                      pubsub_callback_state_t *state,
                      uint64_t *seq,
                      perf_single_metric::phase_t phase,
                      int duration_s,
                      int recv_timeout_ms,
                      queue_probe_t *queue_probe,
                      unsigned long long *out_received,
                      latency_stats_t *out_latency)
{
    const bool active_phase = phase == perf_single_metric::phase_active;
    const auto deadline =
      std::chrono::steady_clock::now()
      + std::chrono::seconds(duration_s > 0 ? duration_s : 1);
    state->fatal.store(false, std::memory_order_release);
    state->warmup_received.store(0, std::memory_order_release);
    state->active_received.store(0, std::memory_order_release);
    state->warmup_processed.store(0, std::memory_order_release);
    state->active_processed.store(0, std::memory_order_release);
    state->active_deadline_us.store(
      active_phase
        ? perf_single_metric::now_us()
            + static_cast<uint64_t>(std::max(1, duration_s) * 1000000ULL)
        : 0,
      std::memory_order_release);
    state->probe = queue_probe;
    state->latency = latency_stats_builder_t();

    unsigned long long sent_count = 0;
    while (std::chrono::steady_clock::now() < deadline) {
        const bool sent = send_pubsub_sample(
          pub_socket, payload, state->payload_size, state->msg_size,
          state->run_id, seq, phase);
        const perf_send_class_t send_class =
          sent ? perf_send_success : perf_classify_send_result(-1);
        if (send_class == perf_send_backpressure) {
            if (!single_wait_for_send_backpressure(queue_probe))
                return false;
            continue;
        }
        if (send_class == perf_send_fatal)
            return false;
        ++sent_count;
        if (active_phase && queue_probe)
            queue_probe->sample_send_if_due();
    }

    const bool completed = single_wait_for_phase_processed(
      *state,
      phase,
      sent_count,
      single_phase_completion_timeout_ms(duration_s, recv_timeout_ms));
    if (!completed || state->fatal.load(std::memory_order_acquire))
        return false;

    *out_received = active_phase
                      ? state->active_received.load(std::memory_order_relaxed)
                      : state->warmup_received.load(std::memory_order_relaxed);
    if (active_phase) {
        state->active_deadline_us.store(0, std::memory_order_release);
        if (*out_received == 0 || !out_latency)
            return false;
        *out_latency = state->latency.snapshot();
    }
    return *out_received > 0;
}

} // namespace

void run_pubsub(const std::string &transport,
                size_t msg_size,
                const std::string &lib_name)
{
    if (!transport_available(transport))
        return;
    ctx_guard_t ctx;
    if (!ctx.valid()) {
        print_fail_result(lib_name, "PUBSUB", transport, msg_size);
        return;
    }
    socket_guard_t pub(ctx.get(), ZLINK_SOCKET_PUB);
    socket_guard_t sub(ctx.get(), ZLINK_SOCKET_SUB);
    if (!pub.valid() || !sub.valid()) {
        print_fail_result(lib_name, "PUBSUB", transport, msg_size);
        return;
    }
    set_pub_opt_int(
      pub.get(), ZLINK_PUB_OPT_NODROP, resolve_pubsub_xpub_nodrop_opt(),
      "ZLINK_PUB_OPT_NODROP");
    if (!setup_connected_pubsub_pair(
          pub.get(), sub.get(), transport, lib_name + "_pubsub")) {
        print_fail_result(lib_name, "PUBSUB", transport, msg_size);
        return;
    }

    const size_t payload_size =
      std::max<size_t>(msg_size, perf_single_metric::header_size());
    std::vector<char> payload(payload_size, 'a');
    queue_probe_t queue_probe(pub.get(), sub.get());
    pubsub_callback_state_t state;
    single_callback_metric_queue_t queue(65536);
    single_metric_worker_t<pubsub_callback_state_t> worker;
    worker.state = &state;
    worker.queue = &queue;
    state.run_id = static_cast<uint32_t>(perf_single_metric::now_us());
    state.msg_size = msg_size;
    state.payload_size = payload_size;
    state.callback_queue = &queue;
    if (zlink_subscribe_handler(sub.get(), &pubsub_recv_handler, &state) != 0
        || !start_single_metric_worker(&worker)) {
        print_fail_result(lib_name, "PUBSUB", transport, msg_size, &queue_probe);
        return;
    }

    uint64_t seq = 1;
    unsigned long long warmup_received = 0;
    unsigned long long received = 0;
    latency_stats_t latency_stats;
    const int recv_timeout_ms = resolve_single_pubsub_recv_timeout_ms();
    if (!run_oneway_phase(pub.get(),
                          &payload,
                          &state,
                          &seq,
                          perf_single_metric::phase_warmup,
                          resolve_single_warmup_seconds(),
                          recv_timeout_ms,
                          NULL,
                          &warmup_received,
                          NULL)
        || !run_oneway_phase(pub.get(),
                             &payload,
                             &state,
                             &seq,
                             perf_single_metric::phase_active,
                             std::max(1, resolve_single_duration_seconds()),
                             recv_timeout_ms,
                             &queue_probe,
                             &received,
                             &latency_stats)) {
        stop_single_metric_worker(&worker);
        print_fail_result(lib_name, "PUBSUB", transport, msg_size, &queue_probe);
        return;
    }
    stop_single_metric_worker(&worker);
    print_result(lib_name,
                 "PUBSUB",
                 transport,
                 msg_size,
                 static_cast<double>(received)
                   / static_cast<double>(std::max(
                     1, resolve_single_duration_seconds())),
                 latency_stats.mean_us,
                 latency_stats.p95_us,
                 latency_stats.p99_us,
                 queue_probe.snapshot());
}

int main(int argc, char **argv)
{
    return run_standard_bench_main(argc, argv, "PUBSUB", run_pubsub);
}
