#include "../common/bench_common.hpp"
#include "../common/perf_single_latency.hpp"
#include "../common/perf_single_metric_header.hpp"
#include "../common/perf_single_phase.hpp"
#include "../../common/perf_c_agg.hpp"

#include <algorithm>
#include <atomic>
#include <cerrno>
#include <chrono>
#include <cstdlib>
#include <cstring>
#include <string>
#include <thread>
#include <vector>

#if !defined(_WIN32)
#include <unistd.h>
#else
#include <process.h>
#endif

namespace {

static const char *k_pattern = "SPOT";
static const char *k_topic = "bench";
static const char *k_service_name = "spot-bench";

int current_process_id ()
{
#if !defined(_WIN32)
    return static_cast<int> (getpid ());
#else
    return static_cast<int> (_getpid ());
#endif
}

struct spot_recv_state_t
{
    spot_recv_state_t () : run_id (0), msg_size (0), active_received (0) {}

    uint32_t run_id;
    size_t msg_size;
    std::atomic<unsigned long long> active_received;
    latency_stats_builder_t latency;
};

std::string bind_node (void *node_, const std::string &transport_, int base_port_)
{
    return perf_bind_fixed_endpoint_range (
      node_, transport_, base_port_, 64, &perf_bind_spot_node_endpoint);
}

bool bind_registry (void *registry_,
                    const std::string &transport_,
                    int base_port_,
                    std::string *pub_endpoint_out_,
                    std::string *router_endpoint_out_)
{
    if (!registry_ || !pub_endpoint_out_ || !router_endpoint_out_)
        return false;

    for (int i = 0; i < 64; ++i) {
        const std::string pub_endpoint =
          make_fixed_endpoint (transport_, base_port_ + (i * 2));
        const std::string router_endpoint =
          make_fixed_endpoint (transport_, base_port_ + (i * 2) + 1);
        if (pub_endpoint.empty () || router_endpoint.empty ())
            continue;
        if (zlink_registry_bind (
              registry_, pub_endpoint.c_str (), router_endpoint.c_str ())
            != 0) {
            continue;
        }
        *pub_endpoint_out_ =
          perf_normalize_bind_endpoint_host (pub_endpoint, transport_);
        *router_endpoint_out_ =
          perf_normalize_bind_endpoint_host (router_endpoint, transport_);
        return true;
    }

    return false;
}

int resolve_spot_subscription_ready_timeout_ms (const std::string &transport_)
{
    if (transport_ == "tls" || transport_ == "wss")
        return 10000;
    return 3000;
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

bool service_matches (const char *service_name_, size_t service_name_len_)
{
    if (!service_name_)
        return false;
    if (service_name_len_ > 0 && service_name_[service_name_len_ - 1] == '\0')
        --service_name_len_;
    return service_name_len_ == std::strlen (k_service_name)
           && std::memcmp (service_name_, k_service_name, service_name_len_) == 0;
}

int wait_for_spot_node_subject_ready_local (void *node_, int timeout_ms_)
{
    const auto deadline =
      std::chrono::steady_clock::now ()
      + std::chrono::milliseconds (timeout_ms_ > 0 ? timeout_ms_ : 1);
    while (std::chrono::steady_clock::now () < deadline) {
        zlink_spot_node_status_t status;
        if (zlink_spot_node_status_snapshot (node_, &status) == 0
            && status.subject_count > 0
            && (status.ready_subject_count > 0
                || status.connected_peer_count > 0
                || status.active_peer_count > 0
                || status.configured_peer_count == 0)) {
            return 1;
        }
        (void) perf_socket_poll (NULL, 0, 10);
    }
    return 0;
}

int recv_spot_header_flags (void *subscriber_,
                            int flags_,
                            perf_single_metric::header_t *header_out_,
                            bool *header_ok_out_)
{
    if (!subscriber_)
        return -1;

    if (header_ok_out_)
        *header_ok_out_ = false;

    zlink_msg_t *parts = NULL;
    size_t part_count = 0;
    char service_name[256];
    size_t service_name_len = sizeof (service_name);
    char topic[256];
    size_t topic_len = sizeof (topic);
    const int rc = zlink_spot_subscribe (
      subscriber_, NULL, &parts, &part_count, service_name, &service_name_len,
      topic, &topic_len,
      static_cast<zlink_recv_flags_t> (flags_));
    if (rc != 0) {
        const int err = zlink_errno ();
        if (err == EAGAIN || err == EINTR)
            return 0;
        return -1;
    }

    const bool header_ok =
      service_matches (service_name, service_name_len)
      && topic_matches (topic, topic_len) && parts && part_count >= 1
      && header_out_
      && perf_single_metric::decode_payload_header (
        zlink_msg_data (&parts[0]), zlink_msg_size (&parts[0]), header_out_);
    if (header_ok_out_)
        *header_ok_out_ = header_ok;
    return 1;
}

bool publish_metric_payload (void *publisher_,
                             std::vector<char> *payload_,
                             size_t msg_size_,
                             uint32_t run_id_,
                             uint64_t seq_,
                             perf_single_metric::phase_t phase_,
                             int flags_)
{
    if (!publisher_ || !payload_)
        return false;

    const size_t payload_size =
      std::max (msg_size_, perf_single_metric::header_size ());
    if (payload_->size () != payload_size)
        payload_->assign (payload_size, 's');
    if (!perf_single_metric::stamp_payload (
          payload_->data (),
          payload_->size (),
          run_id_,
          phase_,
          msg_size_,
          seq_,
          perf_single_metric::now_ns ())) {
        return false;
    }

    zlink_msg_t part;
    if (zlink_msg_init_size (&part, payload_->size ()) != 0)
        return false;
    std::memcpy (zlink_msg_data (&part), payload_->data (), payload_->size ());
    if (zlink_spot_publish (
          publisher_, k_service_name, k_topic, &part, 1,
          static_cast<zlink_send_flags_t> (flags_))
        != 0) {
        const int err = zlink_errno ();
        const bool retry_ready_probe =
          (flags_ & ZLINK_DONTWAIT) != 0
          && (err == EAGAIN || err == ENOTCONN || err == EHOSTUNREACH
              || err == ENETUNREACH);
        if (err == EINTR || retry_ready_probe)
            return true;
        if (bench_debug_enabled ()) {
            std::cerr << "[perf-spot] publish failed err=" << err << std::endl;
        }
        return false;
    }

    return true;
}

bool wait_for_spot_ready_barrier (void *publisher_,
                                  void *subscriber_,
                                  spot_recv_state_t *state_,
                                  size_t msg_size_,
                                  int timeout_ms_)
{
    if (!publisher_ || !subscriber_ || !state_)
        return false;

    state_->active_received.store (0, std::memory_order_release);
    state_->latency = latency_stats_builder_t ();
    std::vector<char> payload;
    const auto deadline =
      std::chrono::steady_clock::now ()
      + std::chrono::milliseconds (timeout_ms_ > 0 ? timeout_ms_ : 1);
    while (std::chrono::steady_clock::now () < deadline) {
        if (!publish_metric_payload (publisher_,
                                     &payload,
                                     msg_size_,
                                     state_->run_id,
                                     0,
                                     perf_single_metric::phase_active,
                                     ZLINK_DONTWAIT)) {
            if (bench_debug_enabled ())
                std::cerr << "[perf-spot] probe publish failed" << std::endl;
            return false;
        }

        const auto probe_deadline =
          std::min (deadline, std::chrono::steady_clock::now ()
                                 + std::chrono::milliseconds (50));
        while (std::chrono::steady_clock::now () < probe_deadline) {
            bool progressed = false;
            perf_single_metric::header_t header;
            bool header_ok = false;
            const int recv_rc =
              recv_spot_header_flags (
                subscriber_, ZLINK_DONTWAIT, &header, &header_ok);
            if (recv_rc > 0) {
                progressed = true;
                if (header_ok) {
                    (void) single_record_active_header (state_, header);
                    if (state_->active_received.load (std::memory_order_acquire)
                        > 0) {
                        if (bench_debug_enabled ())
                            std::cerr << "[perf-spot] ready barrier complete"
                                      << std::endl;
                        return true;
                    }
                }
            } else if (recv_rc < 0) {
                return false;
            }

            if (!progressed
                && !wait_socket_event_until (
                  subscriber_, ZLINK_POLLIN, probe_deadline)) {
                break;
            }
        }
    }

    return state_->active_received.load (std::memory_order_acquire) > 0;
}

bool run_spot_post_ready_settle ()
{
    const int settle_ms = resolve_single_spot_ready_settle_ms ();
    return settle_ms <= 0 || perf_socket_poll (NULL, 0, settle_ms) >= 0;
}

bool send_spot_samples (void *publisher_,
                        std::vector<char> *payload_,
                        spot_recv_state_t *state_,
                        int duration_s_,
                        std::atomic<unsigned long long> *sent_count_)
{
    if (!publisher_ || !payload_ || !state_ || !sent_count_)
        return false;

    const auto deadline =
      std::chrono::steady_clock::now ()
      + std::chrono::seconds (std::max (1, duration_s_));
    uint64_t seq = 1;
    while (std::chrono::steady_clock::now () < deadline) {
        if (!publish_metric_payload (publisher_,
                                     payload_,
                                     state_->msg_size,
                                     state_->run_id,
                                     seq,
                                     perf_single_metric::phase_active,
                                     0)) {
            return false;
        }
        sent_count_->fetch_add (1, std::memory_order_release);
        ++seq;
    }
    return true;
}

bool run_active_window (void *publisher_,
                        void *subscriber_,
                        spot_recv_state_t *state_,
                        int duration_s_,
                        int recv_timeout_ms_,
                        double *throughput_out_,
                        latency_stats_t *latency_out_)
{
    if (!publisher_ || !subscriber_ || !state_ || !throughput_out_
        || !latency_out_) {
        return false;
    }

    state_->active_received.store (0, std::memory_order_release);
    state_->latency = latency_stats_builder_t ();
    std::vector<char> payload;
    std::atomic<unsigned long long> sent_count (0);
    std::atomic<bool> sender_ok (true);
    std::atomic<bool> sender_done (false);
    std::atomic<unsigned long long> received (0);
    latency_stats_builder_t latency_builder;
    const auto deadline =
      std::chrono::steady_clock::now ()
      + std::chrono::seconds (std::max (1, duration_s_));
    const auto drain_idle_limit =
      std::chrono::milliseconds (recv_timeout_ms_ > 0 ? recv_timeout_ms_ : 200);

    std::thread receiver_thread ([&]() {
        auto last_recv_at = std::chrono::steady_clock::now ();
        while (true) {
            const bool done = sender_done.load (std::memory_order_acquire);
            perf_single_metric::header_t header;
            bool header_ok = false;
            const int recv_rc =
              recv_spot_header_flags (
                subscriber_, ZLINK_DONTWAIT, &header, &header_ok);
            if (recv_rc > 0) {
                last_recv_at = std::chrono::steady_clock::now ();
                if (header_ok && single_header_matches_run (*state_, header)
                    && std::chrono::steady_clock::now () < deadline) {
                    received.fetch_add (1, std::memory_order_relaxed);
                    latency_builder.add (single_latency_ns (header));
                }

                for (;;) {
                    perf_single_metric::header_t burst_header;
                    bool burst_header_ok = false;
                    const int burst_rc = recv_spot_header_flags (
                      subscriber_, ZLINK_DONTWAIT, &burst_header,
                      &burst_header_ok);
                    if (burst_rc > 0) {
                        last_recv_at = std::chrono::steady_clock::now ();
                        if (burst_header_ok
                            && single_header_matches_run (*state_, burst_header)
                            && std::chrono::steady_clock::now () < deadline) {
                            received.fetch_add (1, std::memory_order_relaxed);
                            latency_builder.add (
                              single_latency_ns (burst_header));
                        }
                        continue;
                    }
                    if (burst_rc == 0)
                        break;
                    sender_ok.store (false, std::memory_order_release);
                    return;
                }
                continue;
            }

            if (recv_rc == 0) {
                if (done
                    && std::chrono::steady_clock::now () - last_recv_at
                         >= drain_idle_limit) {
                    break;
                }
                (void) perf_socket_poll (NULL, 0, 1);
                continue;
            }

            sender_ok.store (false, std::memory_order_release);
            return;
        }
    });

    std::thread sender_thread ([&]() {
        sender_ok.store (
          send_spot_samples (
            publisher_, &payload, state_, duration_s_, &sent_count),
          std::memory_order_release);
        sender_done.store (true, std::memory_order_release);
    });

    sender_thread.join ();
    receiver_thread.join ();
    if (!sender_ok.load (std::memory_order_acquire)) {
        if (bench_debug_enabled ()) {
            std::cerr << "[perf-spot] active phase failed sent="
                      << sent_count.load (std::memory_order_relaxed)
                      << " received="
                      << received.load (std::memory_order_relaxed)
                      << std::endl;
        }
        return false;
    }

    const unsigned long long recv_total =
      received.load (std::memory_order_relaxed);
    if (recv_total == 0 || latency_builder.count () == 0)
        return false;

    *throughput_out_ =
      static_cast<double> (recv_total)
      / static_cast<double> (std::max (1, duration_s_));
    *latency_out_ = latency_builder.snapshot ();
    return true;
}

void cleanup_spot_case (void **subscriber_,
                        void **publisher_,
                        void **subscriber_discovery_,
                        void **publisher_discovery_,
                        void **registry_,
                        void **subscriber_node_,
                        void **publisher_node_)
{
    if (subscriber_ && *subscriber_)
        zlink_spot_destroy (subscriber_);
    if (publisher_ && *publisher_)
        zlink_spot_destroy (publisher_);
    if (subscriber_discovery_ && *subscriber_discovery_)
        (void) zlink_discovery_destroy (subscriber_discovery_);
    if (publisher_discovery_ && *publisher_discovery_)
        (void) zlink_discovery_destroy (publisher_discovery_);
    if (registry_ && *registry_)
        (void) zlink_registry_destroy (registry_);
    if (subscriber_node_ && *subscriber_node_)
        (void) zlink_spot_node_destroy (subscriber_node_);
    if (publisher_node_ && *publisher_node_)
        (void) zlink_spot_node_destroy (publisher_node_);
}

int run_case (const std::string &lib_name_,
              const std::string &transport_,
              size_t msg_size_)
{
    if (!perf_supports_service_transport (transport_)
        || !transport_available (transport_)) {
        std::cout << "UNSUPPORTED," << k_pattern << "," << transport_
                  << std::endl;
        return 0;
    }

    auto print_fail = [&] () {
        print_fail_result (lib_name_, k_pattern, transport_, msg_size_);
    };

    ctx_guard_t ctx;
    if (!ctx.valid ()) {
        print_fail ();
        return 1;
    }

    void *publisher_node = zlink_spot_node_new (ctx.get ());
    void *subscriber_node = zlink_spot_node_new (ctx.get ());
    void *registry = zlink_registry_new (ctx.get ());
    void *publisher_discovery =
      zlink_discovery_new (ctx.get (), ZLINK_SERVICE_TYPE_SPOT, k_service_name);
    void *subscriber_discovery =
      zlink_discovery_new (ctx.get (), ZLINK_SERVICE_TYPE_SPOT, k_service_name);
    void *publisher = NULL;
    void *subscriber = NULL;
    if (!publisher_node || !subscriber_node || !registry
        || !publisher_discovery || !subscriber_discovery) {
        if (bench_debug_enabled ())
            std::cerr << "[perf-spot] object creation failed" << std::endl;
        cleanup_spot_case (&subscriber,
                           &publisher,
                           &subscriber_discovery,
                           &publisher_discovery,
                           &registry,
                           &subscriber_node,
                           &publisher_node);
        print_fail ();
        return 1;
    }

    if (!setup_tls_server (registry, transport_)
        || !setup_tls_client (publisher_discovery, transport_)
        || !setup_tls_client (subscriber_discovery, transport_)
        || !setup_tls_server (publisher_node, transport_)
        || !setup_tls_client (publisher_node, transport_)
        || !setup_tls_server (subscriber_node, transport_)
        || !setup_tls_client (subscriber_node, transport_)) {
        if (bench_debug_enabled ())
            std::cerr << "[perf-spot] tls setup failed err=" << zlink_errno ()
                      << std::endl;
        cleanup_spot_case (&subscriber,
                           &publisher,
                           &subscriber_discovery,
                           &publisher_discovery,
                           &registry,
                           &subscriber_node,
                           &publisher_node);
        print_fail ();
        return 1;
    }

    const int base_port = 35000 + (current_process_id () % 1000) * 8;
    std::string registry_pub_endpoint;
    std::string registry_router_endpoint;
    if (!bind_registry (
          registry, transport_, base_port, &registry_pub_endpoint,
          &registry_router_endpoint)
        || zlink_discovery_connect_registry (
             publisher_discovery, registry_router_endpoint.c_str ())
             != ZLINK_CONNECT_OK
        || zlink_discovery_connect_registry (
             subscriber_discovery, registry_router_endpoint.c_str ())
             != ZLINK_CONNECT_OK
        || zlink_spot_node_attach_discovery (
             publisher_node, publisher_discovery)
             != ZLINK_CONFIG_OK
        || zlink_spot_node_attach_discovery (
             subscriber_node, subscriber_discovery)
             != ZLINK_CONFIG_OK) {
        if (bench_debug_enabled ()) {
            std::cerr << "[perf-spot] registry/discovery setup failed"
                      << " registry_router=" << registry_router_endpoint
                      << " err=" << zlink_errno () << std::endl;
        }
        cleanup_spot_case (&subscriber,
                           &publisher,
                           &subscriber_discovery,
                           &publisher_discovery,
                           &registry,
                           &subscriber_node,
                           &publisher_node);
        print_fail ();
        return 1;
    }

    publisher = zlink_spot_new (publisher_node);
    subscriber = zlink_spot_new (subscriber_node);
    if (!publisher || !subscriber) {
        if (bench_debug_enabled ())
            std::cerr << "[perf-spot] spot handle creation failed"
                      << std::endl;
        cleanup_spot_case (&subscriber,
                           &publisher,
                           &subscriber_discovery,
                           &publisher_discovery,
                           &registry,
                           &subscriber_node,
                           &publisher_node);
        print_fail ();
        return 1;
    }

    apply_single_hwm (publisher);
    apply_single_hwm (subscriber);
    apply_single_benchmark_socket_options (publisher, transport_);
    apply_single_benchmark_socket_options (subscriber, transport_);

    const std::string publisher_endpoint =
      bind_node (publisher_node, transport_, base_port + 128);
    const std::string subscriber_endpoint =
      bind_node (subscriber_node, transport_, base_port + 192);
    if (publisher_endpoint.empty () || subscriber_endpoint.empty ()) {
        if (bench_debug_enabled ()) {
            std::cerr << "[perf-spot] node bind failed"
                      << " pub=" << publisher_endpoint
                      << " sub=" << subscriber_endpoint << std::endl;
        }
        cleanup_spot_case (&subscriber,
                           &publisher,
                           &subscriber_discovery,
                           &publisher_discovery,
                           &registry,
                           &subscriber_node,
                           &publisher_node);
        print_fail ();
        return 1;
    }
    if (zlink_set_subscription (subscriber, k_topic) != ZLINK_CONFIG_OK) {
        if (bench_debug_enabled ())
            std::cerr << "[perf-spot] set subscription failed err="
                      << zlink_errno () << std::endl;
        cleanup_spot_case (&subscriber,
                           &publisher,
                           &subscriber_discovery,
                           &publisher_discovery,
                           &registry,
                           &subscriber_node,
                           &publisher_node);
        print_fail ();
        return 1;
    }
    spot_recv_state_t state;
    state.run_id = next_single_metric_run_id ();
    state.msg_size = msg_size_;
    if (!wait_for_spot_ready_barrier (
          publisher,
          subscriber,
          &state,
          msg_size_,
          resolve_spot_subscription_ready_timeout_ms (transport_))) {
        if (bench_debug_enabled ())
            std::cerr << "[perf-spot] ready barrier failed" << std::endl;
        cleanup_spot_case (&subscriber,
                           &publisher,
                           &subscriber_discovery,
                           &publisher_discovery,
                           &registry,
                           &subscriber_node,
                           &publisher_node);
        print_fail ();
        return 1;
    }
    if (!run_spot_post_ready_settle ()) {
        if (bench_debug_enabled ())
            std::cerr << "[perf-spot] post-ready settle failed" << std::endl;
        cleanup_spot_case (&subscriber,
                           &publisher,
                           &subscriber_discovery,
                           &publisher_discovery,
                           &registry,
                           &subscriber_node,
                           &publisher_node);
        print_fail ();
        return 1;
    }

    double throughput = 0.0;
    latency_stats_t latency;
    const bool active_ok = run_active_window (publisher,
                                              subscriber,
                                              &state,
                                              std::max (
                                                1, resolve_single_duration_seconds ()),
                                              resolve_single_recv_timeout_ms (),
                                              &throughput,
                                              &latency);
    if (bench_debug_enabled () && !active_ok)
        std::cerr << "[perf-spot] active window failed" << std::endl;
    cleanup_spot_case (&subscriber,
                       &publisher,
                       &subscriber_discovery,
                       &publisher_discovery,
                       &registry,
                       &subscriber_node,
                       &publisher_node);
    if (!active_ok) {
        print_fail ();
        return 1;
    }

    print_result (lib_name_,
                  k_pattern,
                  transport_,
                  msg_size_,
                  throughput,
                  latency.mean_ns,
                  latency.p95_ns,
                  latency.p99_ns);
    return 0;
}

} // namespace

int main (int argc, char **argv)
{
    if (argc < 4)
        return 1;
    if (!single_perf_validate_recv_mode_for_pattern (k_pattern))
        return 1;

    const std::string lib_name = argv[1];
    const std::string transport = argv[2];
    const size_t msg_size =
      static_cast<size_t> (std::strtoull (argv[3], NULL, 10));
    return run_case (lib_name, transport, msg_size > 0 ? msg_size : 64);
}
