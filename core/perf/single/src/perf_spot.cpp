#include "../common/bench_common.hpp"
#include "../common/perf_single_metric_header.hpp"

#include <atomic>
#include <condition_variable>
#include <cstdlib>
#include <cstring>
#include <mutex>
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

int current_process_id ()
{
#if !defined(_WIN32)
    return static_cast<int> (getpid ());
#else
    return static_cast<int> (_getpid ());
#endif
}

struct spot_queue_probe_t
{
    spot_queue_probe_t (void *pub_monitor_, void *sub_monitor_) :
        sample_interval_ns (resolve_sample_interval_ns ()),
        sample_every_msgs (resolve_sample_every_msgs ()),
        last_send_sample_ns (0),
        last_recv_sample_ns (0),
        send_msgs_since_sample (0),
        recv_msgs_since_sample (0),
        sent_total (0),
        recv_total (0),
        snd_pending_max (0),
        rcv_pending_max (0),
        rcv_pending_end (0),
        seen_send (false),
        seen_recv (false)
    {
        (void) pub_monitor_;
        (void) sub_monitor_;
    }

    void sample_send_if_due () { maybe_sample (true, false); }
    void sample_recv_if_due () { maybe_sample (false, false); }
    void force_sample () { maybe_sample (true, true); }

    queue_stats_t snapshot ()
    {
        force_sample ();
        queue_stats_t stats;
        if (seen_send) {
            stats.has_snd_pending = true;
            stats.snd_pending_max = static_cast<double> (snd_pending_max);
        }
        if (seen_recv) {
            stats.has_rcv_pending = true;
            stats.rcv_pending_max = static_cast<double> (rcv_pending_max);
            stats.rcv_pending_end = static_cast<double> (rcv_pending_end);
        }
        return stats;
    }

    unsigned long long sample_interval_ns;
    unsigned int sample_every_msgs;
    unsigned long long last_send_sample_ns;
    unsigned long long last_recv_sample_ns;
    unsigned int send_msgs_since_sample;
    unsigned int recv_msgs_since_sample;
    unsigned long long sent_total;
    unsigned long long recv_total;
    unsigned long long snd_pending_max;
    unsigned long long rcv_pending_max;
    unsigned long long rcv_pending_end;
    bool seen_send;
    bool seen_recv;

  private:
    static unsigned long long resolve_sample_interval_ns ()
    {
        const int sample_ms = resolve_single_queue_sample_ms ();
        const unsigned long long clamped_ms =
          static_cast<unsigned long long> (sample_ms > 0 ? sample_ms : 100);
        return clamped_ms * 1000000ULL;
    }

    static unsigned int resolve_sample_every_msgs ()
    {
        const int value = resolve_single_queue_sample_every_msgs ();
        return static_cast<unsigned int> (value > 0 ? value : 64);
    }

    static unsigned long long now_ns ()
    {
        return static_cast<unsigned long long> (
          std::chrono::duration_cast<std::chrono::nanoseconds> (
            std::chrono::steady_clock::now ().time_since_epoch ())
            .count ());
    }

    void maybe_sample (bool send_path_, bool force_)
    {
        if (!force_) {
            if (send_path_)
                ++sent_total;
            else
                ++recv_total;
        }

        unsigned int &msgs_since_sample =
          send_path_ ? send_msgs_since_sample : recv_msgs_since_sample;
        if (force_) {
            msgs_since_sample = 0;
        } else if (sample_every_msgs > 1) {
            ++msgs_since_sample;
            if (msgs_since_sample < sample_every_msgs)
                return;
            msgs_since_sample = 0;
        }

        const unsigned long long now = now_ns ();
        unsigned long long &last_sample_ns =
          send_path_ ? last_send_sample_ns : last_recv_sample_ns;
        if (!force_ && last_sample_ns > 0
            && now - last_sample_ns < sample_interval_ns) {
            return;
        }
        last_sample_ns = now;

        const unsigned long long pending =
          sent_total > recv_total ? (sent_total - recv_total) : 0ULL;
        if (send_path_) {
            if (!seen_send || pending > snd_pending_max)
                snd_pending_max = pending;
            seen_send = true;
            return;
        }

        if (!seen_recv || pending > rcv_pending_max)
            rcv_pending_max = pending;
        rcv_pending_end = pending;
        seen_recv = true;
    }
};

struct spot_client_state_t
{
    spot_client_state_t () :
        run_id (0),
        msg_size (0),
        active_deadline_us (0),
        fatal (false),
        warmup_received (0),
        warmup_target (0),
        active_received (0),
        recv_activity (0),
        probe (NULL)
    {
    }

    uint32_t run_id;
    size_t msg_size;
    std::atomic<uint64_t> active_deadline_us;
    std::atomic<bool> fatal;
    std::atomic<unsigned long long> warmup_received;
    std::atomic<unsigned long long> warmup_target;
    std::atomic<unsigned long long> active_received;
    std::atomic<unsigned long long> recv_activity;
    latency_stats_builder_t latency;
    spot_queue_probe_t *probe;
    std::mutex mutex;
    std::mutex latency_mutex;
    std::condition_variable cv;
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
                     || state_.fatal.load (std::memory_order_acquire);
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

void handle_spot_client_parts (spot_client_state_t *state_,
                               const char *topic_,
                               size_t topic_len_,
                               zlink_msg_t *parts_,
                               size_t part_count_)
{
    if (!state_ && bench_debug_enabled ()) {
        std::cerr << "[perf-spot-client] recv without state topic="
                  << std::string (topic_ ? topic_ : "", topic_len_)
                  << " part_count=" << part_count_ << std::endl;
    }
    std::string topic_value =
      topic_ ? std::string (topic_, topic_len_) : std::string ();
    if (!topic_value.empty () && topic_value[topic_value.size () - 1] == '\0')
        topic_value.erase (topic_value.size () - 1);

    if (!state_ || !topic_ || !parts_ || part_count_ == 0
        || topic_value != k_topic) {
        close_parts (parts_, part_count_);
        return;
    }

    perf_single_metric::header_t header;
    const bool header_ok =
      perf_single_metric::decode_payload_header (
        zlink_msg_data (&parts_[0]), zlink_msg_size (&parts_[0]), &header)
      && header.run_id == state_->run_id && header.msg_size == state_->msg_size;
    close_parts (parts_, part_count_);
    if (!header_ok)
        return;

    if (header.phase
        == static_cast<uint32_t> (perf_single_metric::phase_warmup)) {
        state_->warmup_received.fetch_add (1, std::memory_order_acq_rel);
    } else if (header.phase
               == static_cast<uint32_t> (
                 perf_single_metric::phase_active)) {
        const uint64_t now_us = perf_single_metric::now_us ();
        const uint64_t deadline_us =
          state_->active_deadline_us.load (std::memory_order_acquire);
        if (deadline_us > 0 && now_us <= deadline_us) {
            state_->active_received.fetch_add (1, std::memory_order_acq_rel);
            const double latency_us =
              now_us >= header.sent_ts_us
                ? static_cast<double> (now_us - header.sent_ts_us)
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

void spot_client_handler (const zlink_routing_id_t *,
                          const char *topic_,
                          size_t topic_len_,
                          zlink_msg_t *parts_,
                          size_t part_count_,
                          void *)
{
    handle_spot_client_parts (
      g_spot_client_state.load (std::memory_order_acquire), topic_, topic_len_,
      parts_, part_count_);
}

struct spot_recv_loop_t
{
    spot_recv_loop_t () : sub (NULL), state (NULL), poller (NULL), stop (false) {}

    void *sub;
    spot_client_state_t *state;
    void *poller;
    std::atomic<bool> stop;
    std::thread thread;
};

bool start_spot_recv_loop (spot_recv_loop_t *loop_)
{
    if (!loop_ || !loop_->sub || !loop_->state)
        return false;

    loop_->poller = zlink_poller_new ();
    if (!loop_->poller
        || zlink_poller_add (
             loop_->poller, loop_->sub, loop_->sub, ZLINK_POLLIN)
             != 0) {
        if (loop_->poller)
            (void) zlink_poller_destroy (&loop_->poller);
        return false;
    }

    loop_->stop.store (false, std::memory_order_release);
    loop_->thread = std::thread ([loop_] () {
        while (!loop_->stop.load (std::memory_order_acquire)) {
            zlink_poller_event_t event;
            const int poll_rc = zlink_poller_wait (loop_->poller, &event, 10);
            if (poll_rc < 0) {
                const int err = zlink_errno ();
                if (err == EAGAIN || err == EINTR)
                    continue;
                loop_->state->fatal.store (true, std::memory_order_release);
                loop_->state->cv.notify_all ();
                break;
            }
            if (poll_rc == 0 || (event.events & ZLINK_POLLIN) == 0)
                continue;

            zlink_msg_t *parts = NULL;
            size_t part_count = 0;
            char topic[256];
            size_t topic_len = sizeof (topic);
            while (!loop_->stop.load (std::memory_order_acquire)) {
                const int rc = zlink_subscribe (
                  loop_->sub, NULL, &parts, &part_count, topic, &topic_len,
                  ZLINK_DONTWAIT);
                if (rc == 0) {
                    handle_spot_client_parts (
                      loop_->state, topic, topic_len, parts, part_count);
                    free (parts);
                    parts = NULL;
                    part_count = 0;
                    topic_len = sizeof (topic);
                    continue;
                }

                const int err = zlink_errno ();
                if (err == EAGAIN || err == EINTR)
                    break;

                loop_->state->fatal.store (true, std::memory_order_release);
                loop_->state->cv.notify_all ();
                return;
            }
        }
    });
    return true;
}

void stop_spot_recv_loop (spot_recv_loop_t *loop_)
{
    if (!loop_)
        return;
    loop_->stop.store (true, std::memory_order_release);
    if (loop_->thread.joinable ())
        loop_->thread.join ();
    if (loop_->poller)
        (void) zlink_poller_destroy (&loop_->poller);
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

bool wait_spot_send_ready_until (
  void *poller_,
  const std::chrono::steady_clock::time_point &deadline_)
{
    if (!poller_)
        return false;

    while (std::chrono::steady_clock::now () < deadline_) {
        const long long remaining_ms =
          std::chrono::duration_cast<std::chrono::milliseconds> (
            deadline_ - std::chrono::steady_clock::now ())
            .count ();
        const int timeout_ms =
          static_cast<int> (remaining_ms > 0 ? remaining_ms : 1);
        zlink_poller_event_t event;
        const int poll_rc = zlink_poller_wait (poller_, &event, timeout_ms);
        if (poll_rc < 0) {
            const int err = zlink_errno ();
            if (err == EAGAIN || err == EINTR)
                continue;
            return false;
        }
        if (poll_rc == 0)
            return false;
        if ((event.events & ZLINK_POLLOUT) != 0)
            return true;
    }

    return false;
}

bool run_publish_window (void *pub_,
                         spot_client_state_t &client_state_,
                         spot_queue_probe_t &probe_,
                         std::vector<char> &payload_,
                         size_t msg_size_,
                         perf_single_metric::phase_t phase_,
                         int duration_s_,
                         bool callback_mode_,
                         void *send_poller_)
{
    const auto deadline =
      std::chrono::steady_clock::now ()
      + std::chrono::seconds (std::max (0, duration_s_));
    uint64_t seq = 1;

    while (std::chrono::steady_clock::now () < deadline) {
        probe_.sample_send_if_due ();

        if (callback_mode_) {
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
            continue;
        }

        if (publish_payload (pub_,
                             payload_,
                             msg_size_,
                             client_state_.run_id,
                             phase_,
                             seq,
                             ZLINK_DONTWAIT)) {
            ++seq;
            continue;
        }

        const int err = errno;
        if (err == EINTR)
            continue;
        if (err != EAGAIN)
            return false;
        if (!wait_spot_send_ready_until (send_poller_, deadline))
            break;
    }

    return !client_state_.fatal.load (std::memory_order_acquire);
}

bool run_phase_window (void *pub_,
                       spot_client_state_t &client_state_,
                       spot_queue_probe_t &probe_,
                       std::vector<char> &payload_,
                       size_t msg_size_,
                       perf_single_metric::phase_t phase_,
                       int duration_s_,
                       bool callback_mode_,
                       void *send_poller_,
                       double *throughput_out_,
                       latency_stats_t *latency_out_)
{
    client_state_.fatal.store (false, std::memory_order_release);
    client_state_.warmup_received.store (0, std::memory_order_release);
    client_state_.warmup_target.store (0, std::memory_order_release);
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
                             duration_s_,
                             callback_mode_,
                             send_poller_)) {
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
    if (throughput_out_)
        *throughput_out_ =
          static_cast<double> (
            client_state_.active_received.load (std::memory_order_acquire))
          / elapsed_s;
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
    const bool callback_mode = single_perf_callback_mode ();
    if (callback_mode && sub
        && zlink_subscribe_handler (sub, &spot_client_handler, NULL) != 0) {
        sub = NULL;
    }
    void *sub_monitor = NULL;
    void *pub_monitor = NULL;
    void *send_poller = NULL;
    spot_recv_loop_t recv_loop;
    service_monitor_probe_t monitor_probe;
    service_monitor_probe_t pub_monitor_probe;
    if (!pub || !sub) {
        if (bench_debug_enabled ())
            std::cerr << "[perf-spot] handle create failed pub="
                      << (pub != NULL) << " sub=" << (sub != NULL)
                      << " err=" << zlink_errno ()
                      << std::endl;
        cleanup_spot_case (
          &sub_monitor, &pub_monitor, &sub_node, &pub_node);
        return 1;
    }

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
    spot_client_state_scope_t client_state_scope (&client_state);
    spot_queue_probe_t probe (pub_monitor, sub_monitor);
    client_state.probe = &probe;
    recv_loop.sub = sub;
    recv_loop.state = &client_state;

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
        if (send_poller)
            (void) zlink_poller_destroy (&send_poller);
        stop_spot_recv_loop (&recv_loop);
        cleanup_spot_case (
          &sub_monitor, &pub_monitor, &sub_node, &pub_node);
        return 1;
    }

    if (!callback_mode
        && !start_spot_recv_loop (&recv_loop)) {
        if (bench_debug_enabled ())
            std::cerr << "[perf-spot] recv loop start failed err="
                      << zlink_errno () << std::endl;
        cleanup_spot_case (
          &sub_monitor, &pub_monitor, &sub_node, &pub_node);
        return 1;
    }

    if (!callback_mode) {
        send_poller = zlink_poller_new ();
        if (!send_poller
            || zlink_poller_add (send_poller, pub, pub, ZLINK_POLLOUT) != 0) {
            if (bench_debug_enabled ())
                std::cerr << "[perf-spot] send poller init failed err="
                          << zlink_errno () << std::endl;
            if (send_poller)
                (void) zlink_poller_destroy (&send_poller);
            stop_spot_recv_loop (&recv_loop);
            cleanup_spot_case (
              &sub_monitor, &pub_monitor, &sub_node, &pub_node);
            return 1;
        }
    }

    std::vector<char> payload;
    if (!run_phase_window (
          pub, client_state, probe, payload, msg_size_,
          perf_single_metric::phase_warmup,
          resolve_single_warmup_seconds (), callback_mode, send_poller, NULL,
          NULL)) {
        const queue_stats_t queue_stats = probe.snapshot ();
        if (send_poller)
            (void) zlink_poller_destroy (&send_poller);
        print_result (lib_name_,
                      k_pattern,
                      transport_,
                      msg_size_,
                      0.0,
                      0.0,
                      0.0,
                      0.0,
                      queue_stats);
        stop_spot_recv_loop (&recv_loop);
        cleanup_spot_case (
          &sub_monitor, &pub_monitor, &sub_node, &pub_node);
        return 1;
    }

    double throughput = 0.0;
    latency_stats_t latency;
    const bool active_ok = run_phase_window (
      pub, client_state, probe, payload, msg_size_,
      perf_single_metric::phase_active, resolve_single_duration_seconds (),
      callback_mode, send_poller, &throughput, &latency);
    const queue_stats_t queue_stats = probe.snapshot ();
    if (send_poller)
        (void) zlink_poller_destroy (&send_poller);
    print_result (lib_name_,
                  k_pattern,
                  transport_,
                  msg_size_,
                  active_ok ? throughput : 0.0,
                  active_ok ? latency.mean_us : 0.0,
                  active_ok ? latency.p95_us : 0.0,
                  active_ok ? latency.p99_us : 0.0,
                  queue_stats);

    stop_spot_recv_loop (&recv_loop);
    cleanup_spot_case (&sub_monitor, &pub_monitor, &sub_node, &pub_node);
    return active_ok ? 0 : 1;
}
} // namespace

int main (int argc, char **argv)
{
    if (argc < 4)
        return 1;

    const std::string lib_name = argv[1];
    const std::string transport = argv[2];
    const size_t msg_size =
      static_cast<size_t> (std::strtoull (argv[3], NULL, 10));
    return run_case (lib_name, transport, msg_size > 0 ? msg_size : 64);
}
