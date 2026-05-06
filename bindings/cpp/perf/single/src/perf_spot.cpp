// SPOT benchmark: one-way publisher->subscriber recv loop.
// Topology: publisher spot(bind) -> subscriber spot(connect)

#include "../common/perf_single_common.hpp"
#include "../common/perf_single_runner.hpp"

#include <atomic>
#include <chrono>
#include <cstring>
#include <cstdlib>
#include <iostream>
#include <mutex>
#include <optional>
#include <thread>
#include <vector>

#include <zlink_enum.h>

#if defined(ZLINK_HAVE_WINDOWS)
#include <process.h>
#endif

#if !defined(ZLINK_HAVE_WINDOWS)
#include <unistd.h>
#endif

namespace {

const char *const k_topic = "bench";

zlink::routing_id_t routing_id_from_ascii (const char *value_)
{
    return zlink::routing_id_t::from_bytes (
      reinterpret_cast<const uint8_t *> (value_), std::strlen (value_));
}

unsigned current_process_id ()
{
#if defined(ZLINK_HAVE_WINDOWS)
    return static_cast<unsigned> (_getpid ());
#else
    return static_cast<unsigned> (getpid ());
#endif
}

std::string make_spot_endpoint (const std::string &transport_)
{
    static unsigned counter = 0;
    const unsigned port = 34000u + ((current_process_id () % 1000u) * 20u)
                          + (++counter);
    return perf::single::make_fixed_endpoint (transport_, static_cast<int> (port));
}

bool perf_debug_enabled ()
{
    return std::getenv ("PERF_DEBUG") != NULL;
}

std::optional<zlink::topic_message_t>
try_subscribe_from_spot (zlink::service::spot_t &spot_)
{
    return spot_.subscribe (zlink::recv_flags_t::dontwait);
}

struct spot_dispatch_state_t
{
    explicit spot_dispatch_state_t (size_t latency_cap_)
        : service_name (),
          subscriber (NULL),
          msg_size (0),
          payload_size (0),
          run_id (1U),
          ready_seen (false),
          collect_active (false),
          fatal (false),
          send_ready_epoch (0),
          active_deadline_ns (0),
          active_received (0),
          latency (latency_cap_),
          last_recv_at (std::chrono::steady_clock::now ())
    {
    }

    std::string service_name;
    zlink::service::spot_t *subscriber;
    size_t msg_size;
    size_t payload_size;
    uint32_t run_id;
    bool ready_seen;
    bool collect_active;
    bool fatal;
    uint64_t send_ready_epoch;
    uint64_t active_deadline_ns;
    unsigned long long active_received;
    perf::single::latency_stats_builder_t latency;
    std::chrono::steady_clock::time_point last_recv_at;
    std::mutex mutex;
    std::condition_variable cv;
};

bool send_spot_payload (zlink::service::spot_t &spot_,
                        const std::string &service_name_,
                        const void *data_,
                        size_t size_,
                        bool *sent_out_)
{
    if (sent_out_)
        *sent_out_ = false;

    zlink::message_t part = zlink::message_t::from_bytes (data_, size_);
    if (!part.valid ())
        return false;

    try {
        const bool sent =
          spot_.publish (service_name_, k_topic, part, zlink::send_flags_t::dontwait);
        if (sent_out_)
            *sent_out_ = sent;
        return true;
    }
    catch (const std::exception &) {
        return false;
    }
}

bool stamp_and_publish (zlink::service::spot_t &spot_,
                        const std::string &service_name_,
                        std::vector<char> &payload_,
                        uint32_t run_id_,
                        size_t msg_size_,
                        uint64_t seq_,
                        perf_single_metric::phase_t phase_,
                        bool *sent_out_)
{
    return perf_single_metric::stamp_payload (payload_.data (),
                                              payload_.size (),
                                              run_id_,
                                              phase_,
                                              msg_size_,
                                              seq_,
                                              perf_single_metric::now_ns ())
           && send_spot_payload (
             spot_, service_name_, payload_.data (), payload_.size (),
             sent_out_);
}

void record_spot_message (spot_dispatch_state_t *state_,
                          const zlink::topic_message_t &message_)
{
    if (!state_ || !message_.service_name ()
        || *message_.service_name () != state_->service_name
        || message_.topic () != k_topic || message_.parts ().size () != 1
        || message_.parts ()[0].size () != state_->payload_size) {
        return;
    }

    perf_single_metric::header_t header;
    if (!perf_single_metric::decode_payload_header (
          message_.parts ()[0].data (), message_.parts ()[0].size (), &header)
        || header.run_id != state_->run_id
        || header.msg_size != static_cast<uint32_t> (state_->msg_size)
        || header.phase != static_cast<uint32_t> (perf_single_metric::phase_active)) {
        return;
    }

    const uint64_t now_ns = perf_single_metric::now_ns ();
    bool notify = false;
    {
        std::lock_guard<std::mutex> lock (state_->mutex);
        state_->last_recv_at = std::chrono::steady_clock::now ();
        if (!state_->ready_seen) {
            state_->ready_seen = true;
            notify = true;
        }

        if (!state_->collect_active || now_ns > state_->active_deadline_ns)
            return;

        ++state_->active_received;
        state_->latency.add (
          now_ns >= header.sent_ts_ns ? static_cast<double> (now_ns - header.sent_ts_ns)
                                      : 0.0);
        notify = true;
    }

    if (notify)
        state_->cv.notify_all ();
}

void on_spot_dispatch_event (spot_dispatch_state_t *state_,
                             const zlink::spot_dispatch_info_t &info_)
{
    if (!state_
        || info_.event != zlink::spot_dispatch_event_t::subscribe_readable) {
        return;
    }

    if (!state_->subscriber)
        return;

    try {
        for (;;) {
            std::optional<zlink::topic_message_t> message =
              try_subscribe_from_spot (*state_->subscriber);
            if (!message.has_value ())
                return;

            record_spot_message (state_, *message);
        }
    }
    catch (const std::exception &) {
        {
            std::lock_guard<std::mutex> lock (state_->mutex);
            state_->fatal = true;
        }
        state_->cv.notify_all ();
    }
}

void on_spot_send_ready (spot_dispatch_state_t *state)
{
    if (!state)
        return;

    {
        std::lock_guard<std::mutex> lock (state->mutex);
        ++state->send_ready_epoch;
    }
    state->cv.notify_all ();
}

uint64_t current_send_ready_epoch (spot_dispatch_state_t *state_)
{
    if (!state_)
        return 0;

    std::lock_guard<std::mutex> lock (state_->mutex);
    return state_->send_ready_epoch;
}

bool wait_for_send_ready (spot_dispatch_state_t *state_,
                          uint64_t observed_epoch_,
                          std::chrono::milliseconds timeout_)
{
    if (!state_)
        return false;

    std::unique_lock<std::mutex> lock (state_->mutex);
    return state_->cv.wait_for (
      lock,
      timeout_ > std::chrono::milliseconds (0) ? timeout_
                                               : std::chrono::milliseconds (1),
      [state_, observed_epoch_]() {
          return state_->fatal || state_->send_ready_epoch != observed_epoch_;
      });
}

[[noreturn]] void fast_exit_process (int exit_code_)
{
    std::cout.flush ();
    std::cerr.flush ();
    std::_Exit (exit_code_);
}

bool wait_for_local_probe_ready (zlink::service::spot_t &publisher_,
                                 std::vector<char> &payload_,
                                 spot_dispatch_state_t *state_,
                                 int timeout_ms_)
{
    if (!state_)
        return false;

    const auto deadline =
      std::chrono::steady_clock::now ()
      + std::chrono::milliseconds (timeout_ms_ > 0 ? timeout_ms_ : 1);
    uint64_t seq = 0;
    while (std::chrono::steady_clock::now () < deadline) {
        const uint64_t send_ready_epoch = current_send_ready_epoch (state_);
        bool sent = false;
        if (!stamp_and_publish (publisher_,
                                state_->service_name,
                                payload_,
                                state_->run_id,
                                state_->msg_size,
                                seq++,
                                perf_single_metric::phase_active,
                                &sent)) {
            return false;
        }
        if (!sent) {
            const auto remaining =
              std::chrono::duration_cast<std::chrono::milliseconds> (
                deadline - std::chrono::steady_clock::now ());
            (void) wait_for_send_ready (
              state_,
              send_ready_epoch,
              remaining < std::chrono::milliseconds (50)
                ? remaining
                : std::chrono::milliseconds (50));
            continue;
        }

        std::unique_lock<std::mutex> lock (state_->mutex);
        if (state_->cv.wait_for (
              lock,
              std::chrono::milliseconds (50),
              [state_]() { return state_->ready_seen || state_->fatal; })) {
            return state_->ready_seen && !state_->fatal;
        }
    }

    return false;
}

bool wait_for_idle_drain (spot_dispatch_state_t *state_, int recv_timeout_ms_)
{
    if (!state_)
        return false;

    const auto idle_limit =
      std::chrono::milliseconds (
        std::max (1, recv_timeout_ms_ > 0 ? recv_timeout_ms_ * 2 : 400));

    std::unique_lock<std::mutex> lock (state_->mutex);
    while (!state_->fatal) {
        const auto last_recv_at = state_->last_recv_at;
        const auto now = std::chrono::steady_clock::now ();
        if (now - last_recv_at >= idle_limit)
            return true;

        const auto wake_at = last_recv_at + idle_limit;
        state_->cv.wait_until (lock, wake_at, [state_]() { return state_->fatal; });
    }
    return false;
}

} // namespace

bool run_pattern_spot (const std::string &transport,
                       size_t msg_size,
                       const std::string &lib_name)
{
    if (transport != "tcp" && transport != "tls" && transport != "ws"
        && transport != "wss") {
        std::cout << "UNSUPPORTED," << lib_name << ",SPOT," << transport
                  << std::endl;
        return true;
    }

    perf::single::ctx_guard_t ctx;
    if (!ctx.valid ()) {
        perf::single::print_fail_result (lib_name, "SPOT", transport, msg_size);
        return false;
    }

    zlink::service::spot_node_t pub_node (ctx.ctx ());
    zlink::service::spot_node_t sub_node (ctx.ctx ());
    zlink::service::registry_t registry (ctx.ctx ());
    if (!pub_node.valid () || !sub_node.valid () || !registry.valid ()) {
        perf::single::print_fail_result (lib_name, "SPOT", transport, msg_size);
        return false;
    }

    const std::string pub_service_name = "spot-bench";
    zlink::service::discovery_t pub_discovery (
      ctx.ctx (), zlink::auto_connect_type::spot_mesh, pub_service_name);
    zlink::service::discovery_t sub_discovery (
      ctx.ctx (), zlink::auto_connect_type::spot_mesh, pub_service_name);
    if (!pub_discovery.valid () || !sub_discovery.valid ()) {
        perf::single::print_fail_result (lib_name, "SPOT", transport, msg_size);
        return false;
    }

    if (transport == "tls" || transport == "wss") {
        std::string cert;
        std::string key;
        std::string ca;
        if (!perf::try_resolve_tls_paths (cert, key, ca)) {
            perf::single::print_fail_result (lib_name, "SPOT", transport, msg_size);
            return false;
        }
        try {
            pub_node.set_tls_server (cert, key, false);
            sub_node.set_tls_client (ca, std::string ("localhost"), false);
        }
        catch (const std::exception &) {
            perf::single::print_fail_result (lib_name, "SPOT", transport, msg_size);
            return false;
        }
    }

    const std::string registry_pub_endpoint = make_spot_endpoint (transport);
    const std::string registry_router_endpoint = make_spot_endpoint (transport);
    const std::string publisher_endpoint = make_spot_endpoint (transport);
    const std::string subscriber_endpoint = make_spot_endpoint (transport);

    zlink::service::spot_t pub_spot = pub_node.create_spot ();
    zlink::service::spot_t sub_spot = sub_node.create_spot ();
    if (!pub_spot.valid () || !sub_spot.valid ()) {
        perf::single::print_fail_result (lib_name, "SPOT", transport, msg_size);
        return false;
    }

    try {
        pub_node.set_routing_id (
          routing_id_from_ascii ("z-cpp-perf-spot-publisher"));
        sub_node.set_routing_id (
          routing_id_from_ascii ("a-cpp-perf-spot-subscriber"));
        pub_spot.set_routing_id (
          routing_id_from_ascii ("z-cpp-perf-spot-publisher-spot"));
        sub_spot.set_routing_id (
          routing_id_from_ascii ("a-cpp-perf-spot-subscriber-spot"));
        registry.bind (registry_pub_endpoint, registry_router_endpoint);
        registry.set_broadcast_interval (50);
        pub_discovery.connect_registry (registry_router_endpoint);
        sub_discovery.connect_registry (registry_router_endpoint);
        const int pubsub_hwm =
          perf::single::resolve_single_socket_hwm (true);
        const int router_hwm =
          perf::single::resolve_single_socket_hwm (false);
        pub_node.pubsub_admission_hwm (
          zlink::message_count_t::value (pubsub_hwm));
        sub_node.pubsub_admission_hwm (
          zlink::message_count_t::value (pubsub_hwm));
        pub_node.router_admission_hwm (
          zlink::message_count_t::value (router_hwm));
        sub_node.router_admission_hwm (
          zlink::message_count_t::value (router_hwm));
        pub_node.bind (publisher_endpoint);
        sub_node.bind (subscriber_endpoint);
        pub_node.attach_discovery (pub_discovery);
        sub_node.attach_discovery (sub_discovery);
    }
    catch (const std::exception &e) {
        if (perf_debug_enabled ())
            std::cerr << "spot: setup failed: " << e.what () << std::endl;
        perf::single::print_fail_result (lib_name, "SPOT", transport, msg_size);
        return false;
    }

    pub_spot.options ().send_timeout (
      std::chrono::milliseconds (
        perf::single::resolve_single_send_timeout_ms ()));
    sub_spot.options ().recv_timeout (
      std::chrono::milliseconds (
        perf::single::resolve_single_recv_timeout_ms ()));
    sub_spot.set_subscription (k_topic);

    const size_t payload_size =
      std::max<size_t> (msg_size, perf_single_metric::header_size ());
    std::vector<char> payload (payload_size, 's');
    spot_dispatch_state_t dispatch_state (
      perf::single::resolve_single_latency_sample_cap ());
    dispatch_state.service_name = pub_service_name;
    dispatch_state.subscriber = &sub_spot;
    dispatch_state.msg_size = msg_size;
    dispatch_state.payload_size = payload_size;

    try {
        sub_spot.on_dispatch_event (
          [&dispatch_state] (zlink::service::spot_t &,
                             const zlink::spot_dispatch_info_t &info) {
              on_spot_dispatch_event (&dispatch_state, info);
          });
        pub_spot.on_send_ready (
          [&dispatch_state] () { on_spot_send_ready (&dispatch_state); });
    }
    catch (const std::exception &) {
        perf::single::print_fail_result (lib_name, "SPOT", transport, msg_size);
        return false;
    }

    if (!wait_for_local_probe_ready (
          pub_spot, payload, &dispatch_state, 10000)) {
        if (perf_debug_enabled ())
            std::cerr << "spot: local probe ready barrier failed" << std::endl;
        perf::single::print_fail_result (lib_name, "SPOT", transport, msg_size);
        return false;
    }

    const int settle_ms = perf::single::resolve_single_spot_ready_settle_ms ();
    if (settle_ms > 0)
        std::this_thread::sleep_for (std::chrono::milliseconds (settle_ms));

    const int duration_s =
      std::max (1, perf::single::resolve_single_duration_seconds ());
    const int recv_timeout = perf::single::resolve_single_recv_timeout_ms ();

    {
        std::lock_guard<std::mutex> lock (dispatch_state.mutex);
        dispatch_state.collect_active = true;
        dispatch_state.active_received = 0;
        dispatch_state.latency = perf::single::latency_stats_builder_t (
          perf::single::resolve_single_latency_sample_cap ());
        dispatch_state.active_deadline_ns =
          perf_single_metric::now_ns ()
          + static_cast<uint64_t> (duration_s) * 1000000000ULL;
        dispatch_state.last_recv_at = std::chrono::steady_clock::now ();
    }

    std::atomic<bool> sender_ok (true);
    std::thread sender_thread ([&]() {
        uint64_t seq = 1;
        const auto deadline =
          std::chrono::steady_clock::now () + std::chrono::seconds (duration_s);
        while (std::chrono::steady_clock::now () < deadline) {
            const uint64_t send_ready_epoch =
              current_send_ready_epoch (&dispatch_state);
            bool sent = false;
            if (!stamp_and_publish (pub_spot,
                                    pub_service_name,
                                    payload,
                                    dispatch_state.run_id,
                                    msg_size,
                                    seq++,
                                    perf_single_metric::phase_active,
                                    &sent)) {
                sender_ok.store (false, std::memory_order_release);
                break;
            }
            if (!sent) {
                const auto remaining =
                  std::chrono::duration_cast<std::chrono::milliseconds> (
                    deadline - std::chrono::steady_clock::now ());
                (void) wait_for_send_ready (
                  &dispatch_state,
                  send_ready_epoch,
                  remaining < std::chrono::milliseconds (50)
                    ? remaining
                    : std::chrono::milliseconds (50));
                continue;
            }
        }
    });

    sender_thread.join ();
    {
        std::lock_guard<std::mutex> lock (dispatch_state.mutex);
        dispatch_state.collect_active = false;
    }
    dispatch_state.cv.notify_all ();

    if (!sender_ok.load (std::memory_order_acquire)
        || !wait_for_idle_drain (&dispatch_state, recv_timeout)) {
        if (perf_debug_enabled ())
            std::cerr << "spot: active phase failed" << std::endl;
        perf::single::print_fail_result (lib_name, "SPOT", transport, msg_size);
        return false;
    }

    perf::single::latency_stats_t latency;
    unsigned long long received = 0;
    {
        std::lock_guard<std::mutex> lock (dispatch_state.mutex);
        if (dispatch_state.fatal) {
            perf::single::print_fail_result (lib_name, "SPOT", transport, msg_size);
            return false;
        }
        received = dispatch_state.active_received;
        if (received == 0 || dispatch_state.latency.count () == 0) {
            perf::single::print_fail_result (
              lib_name, "SPOT", transport, msg_size);
            return false;
        }
        latency = dispatch_state.latency.snapshot ();
    }

    const double throughput =
      static_cast<double> (received) / static_cast<double> (duration_s);
    perf::single::print_result (lib_name,
                                "SPOT",
                                transport,
                                msg_size,
                                throughput,
                                latency.mean_ns,
                                latency.p95_ns,
                                latency.p99_ns);
    fast_exit_process (0);
}

int main (int argc, char **argv)
{
    return perf::single::run_standard_bench_main (argc, argv, run_pattern_spot);
}
