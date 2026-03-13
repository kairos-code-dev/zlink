#include "../common/bench_common.hpp"
#include "../common/perf_single_metric_header.hpp"

#include <condition_variable>
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

void discard_spot_handler (const zlink_routing_id_t *,
                           const char *,
                           size_t,
                           zlink_msg_t *parts_,
                           size_t part_count_)
{
    if (!parts_)
        return;
    for (size_t i = 0; i < part_count_; ++i)
        zlink_msg_close (&parts_[i]);
}

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
    spot_queue_probe_t (void *pub_, void *sub_) :
        pub (pub_),
        sub (sub_),
        sample_interval_ns (resolve_sample_interval_ns ()),
        last_send_sample_ns (0),
        last_recv_sample_ns (0),
        snd_pending_max (0),
        rcv_pending_max (0),
        rcv_pending_end (0),
        seen_send (false),
        seen_recv (false)
    {
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

    void *pub;
    void *sub;
    unsigned long long sample_interval_ns;
    unsigned long long last_send_sample_ns;
    unsigned long long last_recv_sample_ns;
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

    static unsigned long long now_ns ()
    {
        return static_cast<unsigned long long> (
          std::chrono::duration_cast<std::chrono::nanoseconds> (
            std::chrono::steady_clock::now ().time_since_epoch ())
            .count ());
    }

    static unsigned long long peer_score (const zlink_peer_info_t &info_)
    {
        return static_cast<unsigned long long> (info_.msgs_sent)
               + static_cast<unsigned long long> (info_.msgs_received);
    }

    static bool read_best_pub_peer (void *pub_, zlink_peer_info_t *info_out_)
    {
        if (!pub_ || !info_out_)
            return false;

        size_t count = 0;
        if (zlink_spot_peers_pub (pub_, NULL, &count) != 0 || count == 0)
            return false;

        std::vector<zlink_peer_info_t> peers (count);
        size_t filled = count;
        if (zlink_spot_peers_pub (pub_, &peers[0], &filled) != 0 || filled == 0)
            return false;

        size_t best = 0;
        for (size_t i = 1; i < filled; ++i) {
            if (peers[i].connected_time > peers[best].connected_time
                || (peers[i].connected_time == peers[best].connected_time
                    && peer_score (peers[i]) > peer_score (peers[best]))) {
                best = i;
            }
        }

        *info_out_ = peers[best];
        return true;
    }

    static bool read_best_sub_peer (void *sub_, zlink_peer_info_t *info_out_)
    {
        if (!sub_ || !info_out_)
            return false;

        size_t count = 0;
        if (zlink_spot_peers_sub (sub_, NULL, &count) != 0 || count == 0)
            return false;

        std::vector<zlink_peer_info_t> peers (count);
        size_t filled = count;
        if (zlink_spot_peers_sub (sub_, &peers[0], &filled) != 0 || filled == 0)
            return false;

        size_t best = 0;
        for (size_t i = 1; i < filled; ++i) {
            if (peers[i].connected_time > peers[best].connected_time
                || (peers[i].connected_time == peers[best].connected_time
                    && peer_score (peers[i]) > peer_score (peers[best]))) {
                best = i;
            }
        }

        *info_out_ = peers[best];
        return true;
    }

    void maybe_sample (bool send_path_, bool force_)
    {
        const unsigned long long now = now_ns ();
        unsigned long long &last_sample_ns =
          send_path_ ? last_send_sample_ns : last_recv_sample_ns;
        if (!force_ && last_sample_ns > 0
            && now - last_sample_ns < sample_interval_ns) {
            return;
        }
        last_sample_ns = now;

        zlink_peer_info_t info;
        if (send_path_) {
            if (!read_best_pub_peer (pub, &info))
                return;
            const unsigned long long pending =
              static_cast<unsigned long long> (info.snd_pending_msgs);
            if (!seen_send || pending > snd_pending_max)
                snd_pending_max = pending;
            seen_send = true;
            return;
        }

        if (!read_best_sub_peer (sub, &info))
            return;
        const unsigned long long pending =
          static_cast<unsigned long long> (info.rcv_pending_msgs);
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
        warmup_received (0),
        active_received (0),
        probe (NULL)
    {
    }

    uint32_t run_id;
    size_t msg_size;
    unsigned long long warmup_received;
    unsigned long long active_received;
    latency_stats_builder_t latency;
    spot_queue_probe_t *probe;
    std::mutex mutex;
    std::condition_variable cv;
};

struct service_monitor_probe_t
{
    std::mutex mutex;
    std::condition_variable cv;
    std::vector<zlink_service_event_t> events;
};

spot_client_state_t *g_client_state = NULL;
service_monitor_probe_t *g_service_monitor_probe = NULL;

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
    return zlink_spot_node_set_tls_server (
             node_, cert_path.c_str (), key_path.c_str ())
           == 0;
}

bool configure_tls_client (void *node_, const std::string &transport_)
{
    if (transport_ != "tls" && transport_ != "wss")
        return true;

    static const std::string ca_path =
      write_temp_cert (test_certs::ca_cert_pem, "perf_spot_ca");
    return zlink_spot_node_set_tls_client (
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

void spot_monitor_handler (const zlink_service_event_t *event_)
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

bool wait_for_counter (std::condition_variable &cv_,
                       std::mutex &mutex_,
                       unsigned long long *value_,
                       unsigned long long expected_,
                       int timeout_ms_)
{
    std::unique_lock<std::mutex> lock (mutex_);
    return cv_.wait_for (
      lock,
      std::chrono::milliseconds (timeout_ms_ > 0 ? timeout_ms_ : 1),
      [value_, expected_] () { return *value_ >= expected_; });
}

unsigned long long resolve_spot_max_inflight ()
{
    const int configured =
      resolve_bench_count ("PERF_SINGLE_SPOT_MAX_INFLIGHT", 64);
    return static_cast<unsigned long long> (configured > 0 ? configured : 64);
}

bool wait_for_inflight_budget (std::condition_variable &cv_,
                               std::mutex &mutex_,
                               unsigned long long *received_,
                               unsigned long long sent_,
                               unsigned long long max_inflight_,
                               int timeout_ms_)
{
    if (!received_ || max_inflight_ == 0 || sent_ < max_inflight_)
        return true;

    const unsigned long long required = sent_ - max_inflight_ + 1;
    return wait_for_counter (
      cv_, mutex_, received_, required, timeout_ms_ > 0 ? timeout_ms_ : 1);
}

void wait_for_idle_receive_drain (std::condition_variable &cv_,
                                  std::mutex &mutex_,
                                  unsigned long long *value_,
                                  int idle_timeout_ms_)
{
    if (!value_)
        return;

    std::unique_lock<std::mutex> lock (mutex_);
    const std::chrono::milliseconds idle_timeout (
      idle_timeout_ms_ > 0 ? idle_timeout_ms_ : 1);

    while (true) {
        const unsigned long long observed = *value_;
        const bool changed = cv_.wait_for (
          lock, idle_timeout, [value_, observed] () {
              return *value_ != observed;
          });
        if (!changed)
            return;
    }
}

void cleanup_spot_case (void **sub_monitor_,
                        void **sub_,
                        void **pub_,
                        void **sub_node_,
                        void **pub_node_)
{
    g_service_monitor_probe = NULL;
    if (sub_monitor_ && *sub_monitor_)
        (void) zlink_service_monitor_close (sub_monitor_);
    if (sub_ && *sub_)
        (void) zlink_spot_destroy (sub_);
    if (pub_ && *pub_)
        (void) zlink_spot_destroy (pub_);
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
                      uint64_t seq_)
{
    const size_t payload_size =
      std::max (msg_size_, perf_single_metric::header_size ());
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
    if (zlink_spot_publish (pub_, k_topic, &part, 1, 0) != 0) {
        const int err = errno;
        zlink_msg_close (&part);
        errno = err;
        return false;
    }

    return true;
}

void sub_handler (const zlink_routing_id_t *,
                  const char *topic_,
                  size_t topic_len_,
                  zlink_msg_t *parts_,
                  size_t part_count_)
{
    spot_client_state_t *state = g_client_state;
    if (!state || !topic_ || topic_len_ != std::strlen (k_topic)
        || std::memcmp (topic_, k_topic, topic_len_) != 0 || part_count_ == 0) {
        close_parts (parts_, part_count_);
        return;
    }

    perf_single_metric::header_t header;
    const bool header_ok =
      perf_single_metric::decode_payload_header (
        zlink_msg_data (&parts_[0]), zlink_msg_size (&parts_[0]), &header)
      && header.run_id == state->run_id && header.msg_size == state->msg_size;

    {
        std::lock_guard<std::mutex> lock (state->mutex);
        if (header_ok
            && header.phase == static_cast<uint32_t> (
                                perf_single_metric::phase_warmup)) {
            ++state->warmup_received;
        } else if (header_ok
                   && header.phase == static_cast<uint32_t> (
                                        perf_single_metric::phase_active)) {
            ++state->active_received;
            const uint64_t now_us = perf_single_metric::now_us ();
            const double latency_us =
              now_us >= header.sent_ts_us
                ? static_cast<double> (now_us - header.sent_ts_us)
                : 0.0;
            state->latency.add (latency_us);
        }
        if (state->probe)
            state->probe->sample_recv_if_due ();
    }

    close_parts (parts_, part_count_);
    state->cv.notify_all ();
}

bool run_warmup (void *pub_,
                 spot_client_state_t &client_state_,
                 spot_queue_probe_t &probe_,
                 std::vector<char> &payload_,
                 size_t msg_size_)
{
    const int default_warmup = msg_size_ >= 65536 ? 20 : 200;
    const int warmup_count =
      resolve_bench_count ("PERF_WARMUP_COUNT", default_warmup);
    const int wait_timeout_ms = std::max (
      1000, resolve_single_recv_timeout_ms () * 10);

    for (int i = 0; i < warmup_count; ++i) {
        probe_.sample_send_if_due ();
        if (!publish_payload (pub_,
                              payload_,
                              msg_size_,
                              client_state_.run_id,
                              perf_single_metric::phase_warmup,
                              static_cast<uint64_t> (i + 1))) {
            return false;
        }

        if (!wait_for_counter (client_state_.cv,
                               client_state_.mutex,
                               &client_state_.warmup_received,
                               static_cast<unsigned long long> (i + 1),
                               wait_timeout_ms)) {
            return false;
        }
    }

    wait_for_idle_receive_drain (client_state_.cv,
                                 client_state_.mutex,
                                 &client_state_.warmup_received,
                                 resolve_single_pubsub_recv_timeout_ms ());
    std::this_thread::sleep_for (std::chrono::milliseconds (SETTLE_TIME_MS));
    return true;
}

bool run_active (void *pub_,
                 spot_client_state_t &client_state_,
                 spot_queue_probe_t &probe_,
                 std::vector<char> &payload_,
                 size_t msg_size_,
                 double *throughput_out_,
                 latency_stats_t *latency_out_)
{
    const int duration_s = resolve_single_duration_seconds ();
    const auto active_start = std::chrono::steady_clock::now ();
    const auto deadline = active_start + std::chrono::seconds (duration_s);
    const int wait_timeout_ms = std::max (
      1000, resolve_single_pubsub_recv_timeout_ms () * 10);
    const unsigned long long max_inflight = resolve_spot_max_inflight ();
    unsigned long long sent = 0;
    uint64_t seq = 1;

    while (std::chrono::steady_clock::now () < deadline) {
        probe_.sample_send_if_due ();
        if (!publish_payload (pub_,
                              payload_,
                              msg_size_,
                              client_state_.run_id,
                              perf_single_metric::phase_active,
                              seq++)) {
            break;
        }
        ++sent;

        if (!wait_for_inflight_budget (client_state_.cv,
                                       client_state_.mutex,
                                       &client_state_.active_received,
                                       sent,
                                       max_inflight,
                                       wait_timeout_ms)) {
            break;
        }
    }

    wait_for_idle_receive_drain (client_state_.cv,
                                 client_state_.mutex,
                                 &client_state_.active_received,
                                 resolve_single_pubsub_recv_timeout_ms ());

    const double elapsed_s = std::max (0.001, static_cast<double> (duration_s));
    if (throughput_out_)
        *throughput_out_ =
          static_cast<double> (client_state_.active_received) / elapsed_s;
    if (latency_out_)
        *latency_out_ = client_state_.latency.snapshot ();
    return client_state_.active_received > 0;
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

    void *pub_node =
      zlink_spot_node_new (ctx.get (), "perf-spot", &discard_spot_handler);
    void *sub_node = zlink_spot_node_new (ctx.get (),
                                          "perf-spot-client",
                                          &discard_spot_handler);
    if (!pub_node || !sub_node) {
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
    (void) zlink_spot_node_set_pub_option (
      pub_node, ZLINK_SPOT_PUB_OPT_LINGER, &linger, sizeof (linger));
    (void) zlink_spot_node_set_pub_option (
      pub_node, ZLINK_SPOT_PUB_OPT_SNDHWM, &sndhwm, sizeof (sndhwm));
    (void) zlink_spot_node_set_pub_option (
      pub_node, ZLINK_SPOT_PUB_OPT_SNDTIMEO, &sndtimeo_ms, sizeof (sndtimeo_ms));
    (void) zlink_spot_node_set_sub_option (
      sub_node, ZLINK_SPOT_SUB_OPT_LINGER, &linger, sizeof (linger));
    (void) zlink_spot_node_set_sub_option (
      sub_node, ZLINK_SPOT_SUB_OPT_RCVHWM, &rcvhwm, sizeof (rcvhwm));

    if (!configure_tls_server (pub_node, transport_)
        || !configure_tls_client (sub_node, transport_)) {
        zlink_spot_node_destroy (&sub_node);
        zlink_spot_node_destroy (&pub_node);
        return 1;
    }

    void *pub = zlink_spot_new (pub_node, &discard_spot_handler);
    void *sub = zlink_spot_new (sub_node, &sub_handler);
    void *sub_monitor = NULL;
    service_monitor_probe_t monitor_probe;
    if (!pub || !sub) {
        cleanup_spot_case (&sub_monitor, &sub, &pub, &sub_node, &pub_node);
        return 1;
    }

    sub_monitor = zlink_spot_monitor_open (
      sub,
      ZLINK_SPOT_ROLE_SUB,
      ZLINK_SPOT_SUB_FILTER_APPLIED | ZLINK_SPOT_SUB_DELIVERY_READY_CHANGED
        | ZLINK_MONITOR_EVENT_ERROR,
      &spot_monitor_handler);
    if (!sub_monitor) {
        cleanup_spot_case (&sub_monitor, &sub, &pub, &sub_node, &pub_node);
        return 1;
    }

    g_service_monitor_probe = &monitor_probe;
    const int base_port = 35000 + (current_process_id () % 1000) * 8;
    const std::string endpoint = bind_node (pub_node, transport_, base_port);
    if (endpoint.empty ()) {
        cleanup_spot_case (&sub_monitor, &sub, &pub, &sub_node, &pub_node);
        return 1;
    }

    if (zlink_spot_node_connect_peer_pub (sub_node, endpoint.c_str ()) != 0) {
        cleanup_spot_case (&sub_monitor, &sub, &pub, &sub_node, &pub_node);
        return 1;
    }

    if (zlink_spot_subscribe (sub, k_topic) != 0) {
        cleanup_spot_case (&sub_monitor, &sub, &pub, &sub_node, &pub_node);
        return 1;
    }

    if (!wait_for_service_event (
          &monitor_probe, ZLINK_SPOT_SUB_FILTER_APPLIED, NULL, k_topic, -1,
          3000)
        || !wait_for_service_event (&monitor_probe,
                                    ZLINK_SPOT_SUB_DELIVERY_READY_CHANGED,
                                    endpoint.c_str (),
                                    k_topic,
                                    1,
                                    resolve_spot_subscription_ready_timeout_ms (
                                      transport_))) {
        cleanup_spot_case (&sub_monitor, &sub, &pub, &sub_node, &pub_node);
        return 1;
    }

    spot_client_state_t client_state;
    g_client_state = &client_state;
    client_state.run_id = static_cast<uint32_t> (current_process_id ());
    client_state.msg_size = msg_size_;
    spot_queue_probe_t probe (pub, sub);
    client_state.probe = &probe;

    std::vector<char> payload;
    if (!run_warmup (pub, client_state, probe, payload, msg_size_)) {
        const queue_stats_t queue_stats = probe.snapshot ();
        print_result (lib_name_,
                      k_pattern,
                      transport_,
                      msg_size_,
                      0.0,
                      0.0,
                      0.0,
                      0.0,
                      queue_stats);
        g_client_state = NULL;
        cleanup_spot_case (&sub_monitor, &sub, &pub, &sub_node, &pub_node);
        return 1;
    }

    double throughput = 0.0;
    latency_stats_t latency;
    const bool active_ok =
      run_active (pub, client_state, probe, payload, msg_size_, &throughput,
                  &latency);
    const queue_stats_t queue_stats = probe.snapshot ();
    print_result (lib_name_,
                  k_pattern,
                  transport_,
                  msg_size_,
                  active_ok ? throughput : 0.0,
                  active_ok ? latency.mean_us : 0.0,
                  active_ok ? latency.p95_us : 0.0,
                  active_ok ? latency.p99_us : 0.0,
                  queue_stats);

    g_client_state = NULL;
    cleanup_spot_case (&sub_monitor, &sub, &pub, &sub_node, &pub_node);
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
