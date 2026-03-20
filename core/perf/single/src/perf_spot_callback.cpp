#include "../common/bench_common.hpp"
#include "../common/perf_single_metric_header.hpp"

#include <algorithm>
#include <atomic>
#include <cerrno>
#include <chrono>
#include <condition_variable>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#if !defined(_WIN32)
#include <unistd.h>
#else
#include <process.h>
#endif

namespace
{
static const char *k_pattern = "SPOT";
static const char *k_topic = "bench";
static const size_t k_metric_queue_capacity = 65536;

bool validate_spot_target_recv_mode ()
{
    if (!single_perf_callback_mode ()) {
        std::cerr << "policy violation: perf_spot_callback requires --recv callback"
                  << std::endl;
        return false;
    }
    return true;
}

int current_process_id ()
{
#if !defined(_WIN32)
    return static_cast<int> (getpid ());
#else
    return static_cast<int> (_getpid ());
#endif
}

struct spot_metric_event_t
{
    spot_metric_event_t () : phase (0), sent_ts_us (0) {}

    uint32_t phase;
    uint64_t sent_ts_us;
};

class spot_metric_queue_t
{
  public:
    explicit spot_metric_queue_t (size_t capacity_) :
        _events (capacity_ > 1 ? capacity_ + 1 : 2),
        _head (0),
        _tail (0)
    {
    }

    bool push (const spot_metric_event_t &event_)
    {
        const size_t head = _head.load (std::memory_order_relaxed);
        const size_t next = advance (head);
        if (next == _tail.load (std::memory_order_acquire))
            return false;
        _events[head] = event_;
        _head.store (next, std::memory_order_release);
        return true;
    }

    bool pop (spot_metric_event_t *event_)
    {
        if (!event_)
            return false;
        const size_t tail = _tail.load (std::memory_order_relaxed);
        if (tail == _head.load (std::memory_order_acquire))
            return false;
        *event_ = _events[tail];
        _tail.store (advance (tail), std::memory_order_release);
        return true;
    }

    bool empty () const
    {
        return _tail.load (std::memory_order_acquire)
               == _head.load (std::memory_order_acquire);
    }

  private:
    size_t advance (size_t index_) const
    {
        return index_ + 1 < _events.size () ? index_ + 1 : 0;
    }

    std::vector<spot_metric_event_t> _events;
    std::atomic<size_t> _head;
    std::atomic<size_t> _tail;
};

struct spot_client_state_t
{
    spot_client_state_t () :
        run_id (0),
        msg_size (0),
        active_deadline_us (0),
        fatal (false),
        warmup_received (0),
        active_received (0),
        recv_activity (0),
        probe (NULL),
        callback_queue (NULL)
    {
    }

    uint32_t run_id;
    size_t msg_size;
    std::atomic<uint64_t> active_deadline_us;
    std::atomic<bool> fatal;
    std::atomic<unsigned long long> warmup_received;
    std::atomic<unsigned long long> active_received;
    std::atomic<unsigned long long> recv_activity;
    latency_stats_builder_t latency;
    queue_probe_t *probe;
    spot_metric_queue_t *callback_queue;
    std::mutex mutex;
    std::mutex latency_mutex;
    std::mutex callback_wait_mutex;
    std::condition_variable cv;
    std::condition_variable callback_wait_cv;
};

struct spot_metric_worker_t
{
    spot_metric_worker_t () : state (NULL), queue (NULL), stop (false) {}

    spot_client_state_t *state;
    spot_metric_queue_t *queue;
    std::atomic<bool> stop;
    std::thread thread;
};

struct service_monitor_probe_t
{
    std::mutex mutex;
    std::condition_variable cv;
    std::vector<zlink_service_event_t> events;
};

service_monitor_probe_t *g_service_monitor_probe = NULL;
service_monitor_probe_t *g_pub_service_monitor_probe = NULL;
std::atomic<spot_client_state_t *> g_spot_client_state (NULL);

struct spot_client_state_scope_t
{
    explicit spot_client_state_scope_t (spot_client_state_t *state_) :
        state (state_)
    {
        g_spot_client_state.store (state_, std::memory_order_release);
    }

    ~spot_client_state_scope_t ()
    {
        g_spot_client_state.store (NULL, std::memory_order_release);
    }

    spot_client_state_t *state;
};

bool is_supported_transport (const std::string &transport_)
{
    return transport_ == "tcp" || transport_ == "tls" || transport_ == "ws"
           || transport_ == "wss";
}

void close_parts (zlink_msg_t *parts_, size_t part_count_)
{
    if (!parts_)
        return;
    for (size_t i = 0; i < part_count_; ++i)
        zlink_msg_close (&parts_[i]);
}

bool configure_tls_server (void *node_, const std::string &transport_)
{
    if (transport_ != "tls" && transport_ != "wss")
        return true;

    static const std::string cert_path =
      write_temp_cert (test_certs::server_cert_pem, "perf_spot_cert");
    static const std::string key_path =
      write_temp_cert (test_certs::server_key_pem, "perf_spot_key");
    return zlink_set_tls_server (
             node_, cert_path.c_str (), key_path.c_str (), 0)
           == 0;
}

bool configure_tls_client (void *node_, const std::string &transport_)
{
    if (transport_ != "tls" && transport_ != "wss")
        return true;

    static const std::string ca_path =
      write_temp_cert (test_certs::ca_cert_pem, "perf_spot_ca");
    return zlink_set_tls_client (
             node_, ca_path.c_str (), "localhost", 0)
           == 0;
}

std::string bind_node (void *node_, const std::string &transport_, int base_port_)
{
    for (int i = 0; i < 64; ++i) {
        const std::string endpoint =
          make_fixed_endpoint (transport_, base_port_ + i);
        if (zlink_spot_node_bind (node_, endpoint.c_str ()) == 0)
            return endpoint;
    }
    return std::string ();
}

void spot_monitor_handler (const zlink_service_event_t *event_, void *)
{
    service_monitor_probe_t *probe = g_service_monitor_probe;
    if (!probe || !event_)
        return;

    {
        std::lock_guard<std::mutex> lock (probe->mutex);
        probe->events.push_back (*event_);
    }
    probe->cv.notify_all ();
}

void spot_pub_monitor_handler (const zlink_service_event_t *event_, void *)
{
    service_monitor_probe_t *probe = g_pub_service_monitor_probe;
    if (!probe || !event_)
        return;

    {
        std::lock_guard<std::mutex> lock (probe->mutex);
        probe->events.push_back (*event_);
    }
    probe->cv.notify_all ();
}

bool consume_matching_service_event_locked (
  service_monitor_probe_t *probe_,
  uint32_t expected_event_type_,
  const char *endpoint_prefix_,
  const char *subject_,
  int min_value_)
{
    if (!probe_)
        return false;

    for (std::vector<zlink_service_event_t>::iterator it =
           probe_->events.begin ();
         it != probe_->events.end (); ++it) {
        if (it->event_type != expected_event_type_)
            continue;
        if (endpoint_prefix_ && endpoint_prefix_[0] != '\0') {
            if ((it->detail_flags & ZLINK_EVENT_DETAIL_ENDPOINT) == 0)
                continue;
            if (std::strncmp (
                  it->endpoint, endpoint_prefix_, std::strlen (endpoint_prefix_))
                != 0) {
                continue;
            }
        }
        if (subject_ && subject_[0] != '\0') {
            if ((it->detail_flags & ZLINK_EVENT_DETAIL_SUBJECT) == 0)
                continue;
            if (std::strcmp (it->subject, subject_) != 0)
                continue;
        }
        if (min_value_ >= 0 && static_cast<int> (it->value) < min_value_)
            continue;
        probe_->events.erase (it);
        return true;
    }

    return false;
}

bool wait_for_service_event (service_monitor_probe_t *probe_,
                             uint32_t expected_event_type_,
                             const char *endpoint_prefix_,
                             const char *subject_,
                             int min_value_,
                             int timeout_ms_)
{
    if (!probe_)
        return false;

    std::unique_lock<std::mutex> lock (probe_->mutex);
    if (consume_matching_service_event_locked (
          probe_, expected_event_type_, endpoint_prefix_, subject_,
          min_value_)) {
        return true;
    }

    return probe_->cv.wait_for (
      lock,
      std::chrono::milliseconds (timeout_ms_ > 0 ? timeout_ms_ : 1),
      [probe_, expected_event_type_, endpoint_prefix_, subject_, min_value_] () {
          return consume_matching_service_event_locked (
            probe_, expected_event_type_, endpoint_prefix_, subject_,
            min_value_);
      });
}

int resolve_spot_subscription_ready_timeout_ms (
  const std::string &transport_)
{
    if (transport_ == "tls" || transport_ == "wss")
        return 10000;
    return 3000;
}

bool wait_for_receive_quiet (spot_client_state_t &state_,
                             int idle_timeout_ms_,
                             int total_timeout_ms_)
{
    const auto total_deadline =
      std::chrono::steady_clock::now ()
      + std::chrono::milliseconds (total_timeout_ms_ > 0 ? total_timeout_ms_ : 1);
    auto idle_deadline =
      std::chrono::steady_clock::now ()
      + std::chrono::milliseconds (idle_timeout_ms_ > 0 ? idle_timeout_ms_ : 1);
    unsigned long long observed =
      state_.recv_activity.load (std::memory_order_acquire);

    std::unique_lock<std::mutex> lock (state_.mutex);
    while (std::chrono::steady_clock::now () < total_deadline) {
        const auto wait_deadline =
          idle_deadline < total_deadline ? idle_deadline : total_deadline;
        const bool changed = state_.cv.wait_until (
          lock, wait_deadline, [&state_, observed] () {
              return state_.recv_activity.load (std::memory_order_acquire)
                       != observed
                     || state_.fatal.load (std::memory_order_acquire)
                     || (state_.callback_queue
                         && !state_.callback_queue->empty ());
          });
        if (state_.fatal.load (std::memory_order_acquire))
            return false;
        if (!changed)
            return true;

        observed = state_.recv_activity.load (std::memory_order_acquire);
        idle_deadline =
          std::chrono::steady_clock::now ()
          + std::chrono::milliseconds (idle_timeout_ms_ > 0 ? idle_timeout_ms_
                                                            : 1);
    }

    return !state_.fatal.load (std::memory_order_acquire);
}

bool topic_matches (const char *topic_, size_t topic_len_)
{
    if (!topic_)
        return false;
    if (topic_len_ > 0 && topic_[topic_len_ - 1] == '\0')
        --topic_len_;
    return topic_len_ == std::strlen (k_topic)
           && std::memcmp (topic_, k_topic, topic_len_) == 0;
}

bool decode_spot_metric_event (spot_client_state_t *state_,
                               const char *topic_,
                               size_t topic_len_,
                               zlink_msg_t *parts_,
                               size_t part_count_,
                               spot_metric_event_t *event_out_)
{
    if (!state_ || !topic_ || !parts_ || part_count_ == 0 || !event_out_
        || !topic_matches (topic_, topic_len_)) {
        return false;
    }

    perf_single_metric::header_t header;
    const bool header_ok =
      perf_single_metric::decode_payload_header (
        zlink_msg_data (&parts_[0]), zlink_msg_size (&parts_[0]), &header)
      && header.run_id == state_->run_id && header.msg_size == state_->msg_size;
    if (!header_ok)
        return false;

    event_out_->phase = header.phase;
    event_out_->sent_ts_us = header.sent_ts_us;
    return true;
}

void account_spot_metric_event (spot_client_state_t *state_,
                                const spot_metric_event_t &event_)
{
    if (!state_)
        return;

    if (event_.phase
        == static_cast<uint32_t> (perf_single_metric::phase_warmup)) {
        state_->warmup_received.fetch_add (1, std::memory_order_acq_rel);
    } else if (event_.phase
               == static_cast<uint32_t> (
                 perf_single_metric::phase_active)) {
        const uint64_t now_us = perf_single_metric::now_us ();
        const uint64_t deadline_us =
          state_->active_deadline_us.load (std::memory_order_acquire);
        if (deadline_us > 0 && now_us <= deadline_us) {
            state_->active_received.fetch_add (1, std::memory_order_acq_rel);
            const double latency_us =
              now_us >= event_.sent_ts_us
                ? static_cast<double> (now_us - event_.sent_ts_us)
                : 0.0;
            {
                std::lock_guard<std::mutex> lock (state_->latency_mutex);
                state_->latency.add (latency_us);
            }
        }
    }

    state_->recv_activity.fetch_add (1, std::memory_order_acq_rel);
    if (state_->probe)
        state_->probe->sample_recv_if_due ();
    state_->cv.notify_all ();
}

bool start_spot_metric_worker (spot_metric_worker_t *worker_)
{
    if (!worker_ || !worker_->state || !worker_->queue)
        return false;

    worker_->stop.store (false, std::memory_order_release);
    worker_->thread = std::thread ([worker_] () {
        spot_metric_event_t event;
        for (;;) {
            {
                std::unique_lock<std::mutex> lock (
                  worker_->state->callback_wait_mutex);
                worker_->state->callback_wait_cv.wait (
                  lock, [worker_] () {
                      return worker_->stop.load (std::memory_order_acquire)
                             || worker_->state->fatal.load (
                                  std::memory_order_acquire)
                             || !worker_->queue->empty ();
                  });
            }

            while (worker_->queue->pop (&event))
                account_spot_metric_event (worker_->state, event);

            if (worker_->stop.load (std::memory_order_acquire)
                || (worker_->state->fatal.load (std::memory_order_acquire)
                    && worker_->queue->empty ())) {
                break;
            }
        }
        worker_->state->cv.notify_all ();
    });
    return true;
}

void stop_spot_metric_worker (spot_metric_worker_t *worker_)
{
    if (!worker_)
        return;
    worker_->stop.store (true, std::memory_order_release);
    if (worker_->state)
        worker_->state->callback_wait_cv.notify_all ();
    if (worker_->thread.joinable ())
        worker_->thread.join ();
}

void spot_client_handler (const zlink_routing_id_t *,
                          const char *topic_,
                          size_t topic_len_,
                          zlink_msg_t *parts_,
                          size_t part_count_,
                          void *)
{
    spot_client_state_t *state =
      g_spot_client_state.load (std::memory_order_acquire);
    spot_metric_event_t event;
    const bool event_ok =
      decode_spot_metric_event (state, topic_, topic_len_, parts_, part_count_,
                                &event);
    close_parts (parts_, part_count_);
    if (!event_ok || !state)
        return;
    if (!state->callback_queue || !state->callback_queue->push (event)) {
        state->fatal.store (true, std::memory_order_release);
        state->callback_wait_cv.notify_all ();
        state->cv.notify_all ();
        return;
    }
    state->callback_wait_cv.notify_one ();
    state->cv.notify_all ();
}

void cleanup_spot_case (void **sub_monitor_,
                        void **pub_monitor_,
                        void **sub_node_,
                        void **pub_node_)
{
    g_service_monitor_probe = NULL;
    g_pub_service_monitor_probe = NULL;
    if (sub_monitor_ && *sub_monitor_)
        (void) zlink_monitor_close (sub_monitor_);
    if (pub_monitor_ && *pub_monitor_)
        (void) zlink_monitor_close (pub_monitor_);
    if (sub_node_ && *sub_node_)
        (void) zlink_spot_node_destroy (sub_node_);
    if (pub_node_ && *pub_node_)
        (void) zlink_spot_node_destroy (pub_node_);
}

bool publish_payload (void *pub_,
                      std::vector<char> &payload_,
                      size_t msg_size_,
                      uint32_t run_id_,
                      perf_single_metric::phase_t phase_,
                      uint64_t seq_,
                      int flags_)
{
    const size_t payload_size =
      std::max (msg_size_, perf_single_metric::header_size ());
    if (payload_.size () != payload_size)
        payload_.assign (payload_size, 's');
    if (!perf_single_metric::stamp_payload (
          payload_.data (),
          payload_.size (),
          run_id_,
          phase_,
          msg_size_,
          seq_,
          perf_single_metric::now_us ())) {
        return false;
    }

    zlink_msg_t part;
    if (zlink_msg_init_size (&part, payload_.size ()) != 0)
        return false;
    std::memcpy (zlink_msg_data (&part), payload_.data (), payload_.size ());
    if (zlink_publish (pub_, k_topic, &part, 1, flags_) != 0) {
        const int err = errno;
        zlink_msg_close (&part);
        errno = err;
        return false;
    }

    return true;
}

bool run_publish_window (void *pub_,
                         spot_client_state_t &client_state_,
                         queue_probe_t &probe_,
                         std::vector<char> &payload_,
                         size_t msg_size_,
                         perf_single_metric::phase_t phase_,
                         int duration_s_)
{
    const auto deadline =
      std::chrono::steady_clock::now ()
      + std::chrono::seconds (std::max (0, duration_s_));
    uint64_t seq = 1;

    while (std::chrono::steady_clock::now () < deadline) {
        if (client_state_.fatal.load (std::memory_order_acquire))
            return false;
        probe_.sample_send_if_due ();

        if (!publish_payload (pub_,
                              payload_,
                              msg_size_,
                              client_state_.run_id,
                              phase_,
                              seq,
                              0)) {
            if (errno == EINTR)
                continue;
            return false;
        }
        ++seq;
    }

    return !client_state_.fatal.load (std::memory_order_acquire);
}

bool run_phase_window (void *pub_,
                       spot_client_state_t &client_state_,
                       queue_probe_t &probe_,
                       std::vector<char> &payload_,
                       size_t msg_size_,
                       perf_single_metric::phase_t phase_,
                       int duration_s_,
                       double *throughput_out_,
                       latency_stats_t *latency_out_)
{
    client_state_.fatal.store (false, std::memory_order_release);
    client_state_.warmup_received.store (0, std::memory_order_release);
    client_state_.active_received.store (0, std::memory_order_release);
    client_state_.recv_activity.store (0, std::memory_order_release);
    client_state_.active_deadline_us.store (
      phase_ == perf_single_metric::phase_active
        ? perf_single_metric::now_us ()
            + static_cast<uint64_t> (std::max (1, duration_s_) * 1000000ULL)
        : 0,
      std::memory_order_release);
    {
        std::lock_guard<std::mutex> lock (client_state_.latency_mutex);
        client_state_.latency = latency_stats_builder_t ();
    }

    if (!run_publish_window (pub_,
                             client_state_,
                             probe_,
                             payload_,
                             msg_size_,
                             phase_,
                             duration_s_)) {
        return false;
    }

    const int idle_timeout_ms = std::max (10, resolve_single_recv_timeout_ms ());
    const int total_timeout_ms = std::max (100, idle_timeout_ms * 2);
    (void) wait_for_receive_quiet (client_state_, idle_timeout_ms, total_timeout_ms);
    if (phase_ != perf_single_metric::phase_active) {
        return !client_state_.fatal.load (std::memory_order_acquire)
               && client_state_.warmup_received.load (std::memory_order_acquire)
                    > 0;
    }

    client_state_.active_deadline_us.store (0, std::memory_order_release);
    const double elapsed_s = std::max (0.001, static_cast<double> (duration_s_));
    if (throughput_out_) {
        *throughput_out_ =
          static_cast<double> (
            client_state_.active_received.load (std::memory_order_acquire))
          / elapsed_s;
    }
    if (latency_out_) {
        std::lock_guard<std::mutex> lock (client_state_.latency_mutex);
        *latency_out_ = client_state_.latency.snapshot ();
    }

    return !client_state_.fatal.load (std::memory_order_acquire)
           && client_state_.active_received.load (std::memory_order_acquire)
                > 0;
}

int run_case (const std::string &lib_name_,
              const std::string &transport_,
              size_t msg_size_)
{
    if (!is_supported_transport (transport_)) {
        std::cout << "UNSUPPORTED," << k_pattern << "," << transport_
                  << std::endl;
        return 0;
    }
    if (!transport_available (transport_)) {
        std::cout << "UNSUPPORTED," << k_pattern << "," << transport_
                  << std::endl;
        return 0;
    }

    ctx_guard_t ctx;
    if (!ctx.valid ())
        return 1;

    void *pub_node = zlink_spot_node_new (ctx.get (), "perf-spot");
    void *sub_node = zlink_spot_node_new (ctx.get (), "perf-spot-client");
    if (!pub_node || !sub_node) {
        if (bench_debug_enabled ())
            std::cerr << "[perf-spot] node create failed err=" << zlink_errno ()
                      << std::endl;
        if (pub_node)
            zlink_spot_node_destroy (&pub_node);
        if (sub_node)
            zlink_spot_node_destroy (&sub_node);
        return 1;
    }

    const int linger = 0;
    const int sndhwm = resolve_single_socket_hwm (true);
    const int rcvhwm = resolve_single_socket_hwm (false);
    const int sndtimeo_ms = resolve_single_send_timeout_ms ();
    const int rcvtimeo_ms = resolve_single_recv_timeout_ms ();
    const int nodrop = 1;
    (void) zlink_set_option (
      pub_node, ZLINK_OPT_LINGER, &linger, sizeof (linger));
    (void) zlink_set_option (
      pub_node, ZLINK_OPT_SNDHWM, &sndhwm, sizeof (sndhwm));
    (void) zlink_set_option (
      pub_node, ZLINK_OPT_SNDTIMEO, &sndtimeo_ms, sizeof (sndtimeo_ms));
    (void) zlink_set_pub_option (
      pub_node, ZLINK_PUB_OPT_NODROP, &nodrop, sizeof (nodrop));
    (void) zlink_set_option (
      sub_node, ZLINK_OPT_LINGER, &linger, sizeof (linger));
    (void) zlink_set_option (
      sub_node, ZLINK_OPT_RCVHWM, &rcvhwm, sizeof (rcvhwm));
    (void) zlink_set_option (
      sub_node, ZLINK_OPT_RCVTIMEO, &rcvtimeo_ms, sizeof (rcvtimeo_ms));

    if (!configure_tls_server (pub_node, transport_)
        || !configure_tls_client (sub_node, transport_)) {
        if (bench_debug_enabled ())
            std::cerr << "[perf-spot] tls configure failed err="
                      << zlink_errno () << std::endl;
        zlink_spot_node_destroy (&sub_node);
        zlink_spot_node_destroy (&pub_node);
        return 1;
    }

    void *pub = pub_node;
    void *sub = sub_node;
    if (zlink_subscribe_handler (sub, &spot_client_handler, NULL) != 0) {
        if (bench_debug_enabled ())
            std::cerr << "[perf-spot] callback handler attach failed err="
                      << zlink_errno () << std::endl;
        zlink_spot_node_destroy (&sub_node);
        zlink_spot_node_destroy (&pub_node);
        return 1;
    }

    void *sub_monitor = NULL;
    void *pub_monitor = NULL;
    spot_metric_queue_t metric_queue (k_metric_queue_capacity);
    spot_metric_worker_t metric_worker;
    service_monitor_probe_t monitor_probe;
    service_monitor_probe_t pub_monitor_probe;

    zlink_service_monitor_open_options_t sub_monitor_opts;
    memset (&sub_monitor_opts, 0, sizeof (sub_monitor_opts));
    sub_monitor_opts.events =
      ZLINK_SPOT_SUB_FILTER_APPLIED | ZLINK_SPOT_SUB_DELIVERY_READY_CHANGED
      | ZLINK_MONITOR_EVENT_ERROR;
    sub_monitor = zlink_service_monitor_open (sub, &sub_monitor_opts);

    zlink_service_monitor_open_options_t pub_monitor_opts;
    memset (&pub_monitor_opts, 0, sizeof (pub_monitor_opts));
    pub_monitor_opts.events =
      ZLINK_SPOT_PUB_DELIVERY_READY_CHANGED
      | ZLINK_SPOT_PUB_FIRST_DELIVERY_READY_CHANGED | ZLINK_MONITOR_EVENT_ERROR;
    pub_monitor = zlink_service_monitor_open (pub, &pub_monitor_opts);
    if (!sub_monitor || !pub_monitor) {
        if (bench_debug_enabled ())
            std::cerr << "[perf-spot] monitor open failed sub="
                      << (sub_monitor != NULL) << " pub="
                      << (pub_monitor != NULL) << " err=" << zlink_errno ()
                      << std::endl;
        cleanup_spot_case (
          &sub_monitor, &pub_monitor, &sub_node, &pub_node);
        return 1;
    }
    if (zlink_service_monitor_handler (sub_monitor, &spot_monitor_handler,
                                       NULL)
        != 0
        || zlink_service_monitor_handler (pub_monitor,
                                          &spot_pub_monitor_handler, NULL)
             != 0) {
        if (bench_debug_enabled ())
            std::cerr << "[perf-spot] monitor handler attach failed err="
                      << zlink_errno () << std::endl;
        cleanup_spot_case (
          &sub_monitor, &pub_monitor, &sub_node, &pub_node);
        return 1;
    }

    g_service_monitor_probe = &monitor_probe;
    g_pub_service_monitor_probe = &pub_monitor_probe;
    const int base_port = 35000 + (current_process_id () % 1000) * 8;
    const std::string endpoint = bind_node (pub_node, transport_, base_port);
    if (endpoint.empty ()) {
        if (bench_debug_enabled ())
            std::cerr << "[perf-spot] bind failed err=" << zlink_errno ()
                      << std::endl;
        cleanup_spot_case (
          &sub_monitor, &pub_monitor, &sub_node, &pub_node);
        return 1;
    }

    if (zlink_spot_node_connect_peer (sub_node, endpoint.c_str ()) != 0) {
        if (bench_debug_enabled ())
            std::cerr << "[perf-spot] connect peer failed err=" << zlink_errno ()
                      << std::endl;
        cleanup_spot_case (
          &sub_monitor, &pub_monitor, &sub_node, &pub_node);
        return 1;
    }

    if (zlink_set_subscription (sub, k_topic) != 0) {
        if (bench_debug_enabled ())
            std::cerr << "[perf-spot] subscribe failed err=" << zlink_errno ()
                      << std::endl;
        cleanup_spot_case (
          &sub_monitor, &pub_monitor, &sub_node, &pub_node);
        return 1;
    }

    spot_client_state_t client_state;
    client_state.run_id = static_cast<uint32_t> (current_process_id ());
    client_state.msg_size = msg_size_;
    client_state.callback_queue = &metric_queue;
    spot_client_state_scope_t client_state_scope (&client_state);
    queue_probe_t probe (pub, sub);
    client_state.probe = &probe;
    metric_worker.state = &client_state;
    metric_worker.queue = &metric_queue;

    if (!wait_for_service_event (
          &monitor_probe, ZLINK_SPOT_SUB_FILTER_APPLIED, NULL, k_topic, -1,
          3000)
        || !wait_for_service_event (&monitor_probe,
                                    ZLINK_SPOT_SUB_DELIVERY_READY_CHANGED,
                                    endpoint.c_str (),
                                    k_topic,
                                    1,
                                    resolve_spot_subscription_ready_timeout_ms (
                                      transport_))
        || !wait_for_service_event (&pub_monitor_probe,
                                    ZLINK_SPOT_PUB_FIRST_DELIVERY_READY_CHANGED,
                                    NULL,
                                    k_topic,
                                    1,
                                    resolve_spot_subscription_ready_timeout_ms (
                                      transport_))) {
        if (bench_debug_enabled ())
            std::cerr << "[perf-spot] ready wait failed" << std::endl;
        cleanup_spot_case (
          &sub_monitor, &pub_monitor, &sub_node, &pub_node);
        return 1;
    }

    if (!start_spot_metric_worker (&metric_worker)) {
        cleanup_spot_case (&sub_monitor, &pub_monitor, &sub_node, &pub_node);
        return 1;
    }

    std::vector<char> payload;
    if (!run_phase_window (
          pub, client_state, probe, payload, msg_size_,
          perf_single_metric::phase_warmup,
          resolve_single_warmup_seconds (), NULL, NULL)) {
        const queue_stats_t queue_stats = probe.snapshot ();
        stop_spot_metric_worker (&metric_worker);
        print_result (lib_name_,
                      k_pattern,
                      transport_,
                      msg_size_,
                      0.0,
                      0.0,
                      0.0,
                      0.0,
                      queue_stats);
        cleanup_spot_case (&sub_monitor, &pub_monitor, &sub_node, &pub_node);
        return 1;
    }

    double throughput = 0.0;
    latency_stats_t latency;
    const bool active_ok = run_phase_window (
      pub, client_state, probe, payload, msg_size_,
      perf_single_metric::phase_active, resolve_single_duration_seconds (),
      &throughput, &latency);
    const queue_stats_t queue_stats = probe.snapshot ();
    stop_spot_metric_worker (&metric_worker);
    print_result (lib_name_,
                  k_pattern,
                  transport_,
                  msg_size_,
                  active_ok ? throughput : 0.0,
                  active_ok ? latency.mean_us : 0.0,
                  active_ok ? latency.p95_us : 0.0,
                  active_ok ? latency.p99_us : 0.0,
                  queue_stats);

    cleanup_spot_case (&sub_monitor, &pub_monitor, &sub_node, &pub_node);
    return active_ok ? 0 : 1;
}
} // namespace

int main (int argc, char **argv)
{
    if (argc < 4)
        return 1;
    if (!single_perf_validate_recv_mode_for_pattern (k_pattern))
        return 1;
    if (!validate_spot_target_recv_mode ())
        return 1;

    const std::string lib_name = argv[1];
    const std::string transport = argv[2];
    const size_t msg_size =
      static_cast<size_t> (std::strtoull (argv[3], NULL, 10));
    return run_case (lib_name, transport, msg_size > 0 ? msg_size : 64);
}
