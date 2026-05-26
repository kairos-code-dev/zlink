#ifndef PERF_SPOT_CONTROL_HPP
#define PERF_SPOT_CONTROL_HPP

#include "perf_common_multi.hpp"
#include "perf_spot_handshake.hpp"
#include "perf_tls.hpp"
#include "../../common/perf_socket_compat.hpp"

#include <cerrno>
#include <chrono>
#include <condition_variable>
#include <cstdlib>
#include <cstdint>
#include <mutex>
#include <optional>
#include <string>
#include <thread>

#include <zlink.hpp>

#if defined(_WIN32)
#include <process.h>
#else
#include <unistd.h>
#endif

namespace perf {
namespace multi {

inline bool apply_spot_auto_hwm_msg_unit (zlink::context_t &ctx_,
                                          size_t msg_size_)
{
    if (msg_size_ == 0)
        return true;
    try {
        zlink::context_options_t options = ctx_.options ();
        options.auto_hwm_msg_unit_bytes (
          zlink::byte_size_t::bytes (static_cast<int64_t> (msg_size_)));
        return true;
    }
    catch (const zlink::config_error_t &err) {
        errno = err.internal_errno ();
        return false;
    }
}

template<typename SpotNode>
inline bool apply_spot_node_admission_hwm (SpotNode &node_,
                                           int pubsub_hwm_,
                                           int router_hwm_)
{
    if (!manual_socket_overrides_enabled ())
        return true;

    const int pubsub = pubsub_hwm_ > 0 ? pubsub_hwm_ : 1;
    const int router = router_hwm_ > 0 ? router_hwm_ : 1;
    try {
        node_.pubsub_admission_hwm (zlink::message_count_t::value (pubsub));
        node_.router_admission_hwm (zlink::message_count_t::value (router));
        return true;
    }
    catch (const zlink::config_error_t &err) {
        errno = err.internal_errno ();
        return false;
    }
}

struct control_connect_gate_t
{
    control_connect_gate_t () : requested (false), stopped (false), endpoint () {}

    bool requested;
    bool stopped;
    std::string endpoint;
    std::mutex mutex;
    std::condition_variable cv;
};

inline void reset_control_connect_gate (control_connect_gate_t *gate_)
{
    if (!gate_)
        return;
    std::lock_guard<std::mutex> lock (gate_->mutex);
    gate_->requested = false;
    gate_->stopped = false;
    gate_->endpoint.clear ();
}

inline void signal_control_connect (control_connect_gate_t *gate_,
                                    const std::string &endpoint_)
{
    if (!gate_ || endpoint_.empty ())
        return;
    {
        std::lock_guard<std::mutex> lock (gate_->mutex);
        gate_->requested = true;
        gate_->stopped = false;
        gate_->endpoint = endpoint_;
    }
    gate_->cv.notify_all ();
}

inline void stop_control_connect_gate (control_connect_gate_t *gate_)
{
    if (!gate_)
        return;
    {
        std::lock_guard<std::mutex> lock (gate_->mutex);
        gate_->requested = false;
        gate_->stopped = true;
        gate_->endpoint.clear ();
    }
    gate_->cv.notify_all ();
}

template<typename SpotHandle>
inline zlink::send_result_t
try_publish_nowait (SpotHandle &spot_,
                    const std::string &topic_,
                    zlink::message_t &outbound)
{
    try {
        return spot_.publish (topic_)
            .message (outbound)
            .flags (ZLINK_DONTWAIT)
            .submit ()
          ? zlink::send_result_t::sent
          : zlink::send_result_t::backpressured;
    }
    catch (const zlink::submit_error_t &err) {
        switch (err.result ()) {
        case zlink::submit_result_t::backpressured:
            return zlink::send_result_t::backpressured;
        case zlink::submit_result_t::not_connected:
        case zlink::submit_result_t::not_found:
            return zlink::send_result_t::not_ready;
        default:
            errno = err.internal_errno ();
            return zlink::send_result_t::not_ready;
        }
    }
}

template<typename SpotHandle>
inline std::optional<zlink::topic_message_t>
try_subscribe_nowait (SpotHandle &spot_)
{
    zlink::topic_message_t message;
    const int rc = spot_.subscribe (message, ZLINK_DONTWAIT);
    if (rc == static_cast<int> (zlink::recv_result_t::no_data))
        return std::nullopt;
    if (rc != static_cast<int> (zlink::recv_result_t::ok))
        return std::nullopt;
    return std::optional<zlink::topic_message_t> (std::move (message));
}

template<typename Clock, typename Duration>
inline std::chrono::milliseconds remaining_milliseconds_until (
  const std::chrono::time_point<Clock, Duration> &deadline_)
{
    const auto now = Clock::now ();
    if (now >= deadline_)
        return std::chrono::milliseconds (0);
    return std::chrono::duration_cast<std::chrono::milliseconds> (
      deadline_ - now);
}

template<typename SpotHandle, typename Clock, typename Duration>
inline bool wait_for_spot_control_event (
  SpotHandle &spot_,
  zlink::poll_event_flag_t event_,
  const std::chrono::time_point<Clock, Duration> &deadline_)
{
    const std::chrono::milliseconds remaining =
      remaining_milliseconds_until (deadline_);
    if (remaining.count () <= 0) {
        errno = ETIMEDOUT;
        return false;
    }

    try {
        zlink::poller_t poller;
        poller.add (spot_, event_, 0);
        zlink::poll_event_t event;
        const size_t count = poller.wait (&event, 1, remaining);
        if (count == 0) {
            errno = ETIMEDOUT;
            return false;
        }
        return true;
    }
    catch (const zlink::recv_error_t &err) {
        errno = err.internal_errno ();
        return false;
    }
    catch (const zlink::config_error_t &err) {
        errno = err.internal_errno ();
        return false;
    }
}

inline void start_client_start_watcher (start_signal_state_t *start_gate_)
{
    if (!start_gate_)
        return;

    std::thread stdin_watcher ([start_gate_]() {
        std::string line;
        while (std::getline (std::cin, line)) {
            size_t start_size = 0;
            if (parse_size_command_line (line, "START,", &start_size)) {
                signal_start (start_gate_, start_size);
                continue;
            }
            if (line == "STOP" || line == "QUIT") {
                signal_stop (start_gate_);
                return;
            }
        }
    });
    stdin_watcher.detach ();
}

inline void start_server_control_watcher (control_connect_gate_t *gate_,
                                          start_signal_state_t *start_gate_)
{
    if (!gate_ || !start_gate_)
        return;

    std::thread stdin_watcher ([gate_, start_gate_]() {
        std::string line;
        while (std::getline (std::cin, line)) {
            std::string control_peer_endpoint;
            size_t start_size = 0;
            if (parse_endpoint_command_line (
                  line, "CONNECT_CONTROL,", &control_peer_endpoint)) {
                signal_control_connect (gate_, control_peer_endpoint);
                continue;
            }
            if (parse_size_command_line (line, "START,", &start_size)) {
                signal_start (start_gate_, start_size);
                continue;
            }
            if (line == "STOP" || line == "QUIT") {
                stop_control_connect_gate (gate_);
                signal_stop (start_gate_);
                return;
            }
        }
    });
    stdin_watcher.detach ();
}

inline std::string make_transport_endpoint (const std::string &transport_,
                                            int port_)
{
    const std::string suffix = std::to_string (port_);
    if (transport_ == "ws")
        return std::string ("ws://127.0.0.1:") + suffix;
    if (transport_ == "wss")
        return std::string ("wss://127.0.0.1:") + suffix;
    if (transport_ == "tls")
        return std::string ("tls://127.0.0.1:") + suffix;
    return std::string ("tcp://127.0.0.1:") + suffix;
}

inline int bench_process_id ()
{
#if defined(_WIN32)
    return _getpid ();
#else
    return getpid ();
#endif
}

inline int bench_port_base (int base_)
{
    return base_ + (bench_process_id () % 80) * 64;
}

inline std::string normalize_spot_endpoint_host (const std::string &endpoint_)
{
    std::string out = endpoint_;
    const std::string any_v4 = "://0.0.0.0:";
    const std::string any_v6 = "://[::]:";
    size_t pos = out.find (any_v4);
    if (pos != std::string::npos)
        out.replace (pos, any_v4.size (), "://127.0.0.1:");
    pos = out.find (any_v6);
    if (pos != std::string::npos)
        out.replace (pos, any_v6.size (), "://127.0.0.1:");
    return out;
}

template<typename SpotNode>
inline std::string bind_spot_endpoint (SpotNode &node_,
                                       const std::string &transport_,
                                       int base_port_)
{
    for (int i = 0; i < 64; ++i) {
        const std::string requested_endpoint =
          make_transport_endpoint (transport_, base_port_ + i);
        try {
            node_.set_pub_bind (requested_endpoint);
        }
        catch (const std::exception &) {
            continue;
        }

        std::string resolved_endpoint = node_.last_endpoint ();
        if (!resolved_endpoint.empty ())
            return normalize_spot_endpoint_host (resolved_endpoint);
        return requested_endpoint;
    }
    return std::string ();
}

template<typename SpotNode>
inline std::string bind_routed_spot_endpoint (SpotNode &node_,
                                              const std::string &transport_,
                                              int base_port_)
{
    for (int i = 0; i < 64; ++i) {
        const std::string requested_endpoint =
          make_transport_endpoint (transport_, base_port_ + i);
        const std::string requested_router_endpoint =
          make_transport_endpoint (transport_, base_port_ + 10000 + i);
        try {
            node_.set_router_bind (requested_router_endpoint);
            node_.set_pub_bind (requested_endpoint);
        }
        catch (const std::exception &) {
            continue;
        }

        std::string resolved_endpoint = node_.last_endpoint ();
        if (!resolved_endpoint.empty ())
            return normalize_spot_endpoint_host (resolved_endpoint);
        return requested_endpoint;
    }
    return std::string ();
}

template<typename SpotNode>
inline bool configure_spot_client_tls (SpotNode &node_,
                                       const std::string &transport_)
{
    if (transport_ != "tls" && transport_ != "wss")
        return true;

    std::string cert;
    std::string key;
    std::string ca;
    if (!try_resolve_tls_paths (cert, key, ca))
        return false;

    try {
        node_.set_tls_client (ca, "localhost", false);
        return true;
    }
    catch (const std::exception &) {
        return false;
    }
}

template<typename SpotNode>
inline bool configure_spot_server_tls (SpotNode &node_,
                                       const std::string &transport_)
{
    if (transport_ != "tls" && transport_ != "wss")
        return true;

    std::string cert;
    std::string key;
    std::string ca;
    if (!try_resolve_tls_paths (cert, key, ca))
        return false;

    try {
        node_.set_tls_server (cert, key);
        return true;
    }
    catch (const std::exception &) {
        return false;
    }
}

template<typename SpotNode>
inline bool configure_spot_control_tls (SpotNode &node_,
                                        const std::string &transport_)
{
    if (transport_ != "tls" && transport_ != "wss")
        return true;

    std::string cert;
    std::string key;
    std::string ca;
    if (!try_resolve_tls_paths (cert, key, ca))
        return false;

    try {
        node_.set_tls_server (cert, key, false);
        node_.set_tls_client (ca, "localhost", false);
        return true;
    }
    catch (const std::exception &) {
        return false;
    }
}

template<typename SpotNode>
inline bool wait_for_control_connect (control_connect_gate_t *gate_,
                                      SpotNode &control_node_,
                                      int timeout_ms_,
                                      std::string *endpoint_out_ = NULL)
{
    if (!gate_) {
        errno = EINVAL;
        return false;
    }

    std::unique_lock<std::mutex> lock (gate_->mutex);
    const bool signaled = gate_->cv.wait_for (
      lock,
      std::chrono::milliseconds (std::max (1, timeout_ms_)),
      [gate_]() { return gate_->stopped || gate_->requested; });
    if (!signaled || gate_->stopped) {
        errno = gate_->stopped ? ECANCELED : ETIMEDOUT;
        return false;
    }

    const std::string endpoint = gate_->endpoint;
    gate_->requested = false;
    gate_->endpoint.clear ();
    lock.unlock ();

    if (endpoint.empty ()) {
        errno = EINVAL;
        return false;
    }
    try {
        control_node_.connect_peer (endpoint);
    }
    catch (const std::exception &) {
        return false;
    }
    if (endpoint_out_)
        *endpoint_out_ = endpoint;
    return true;
}

template<typename SpotHandle>
inline bool publish_control_message (SpotHandle &spot_,
                                     const std::string &channel_name_,
                                     const std::string &topic_,
                                     const std::string &payload_,
                                     int timeout_ms_)
{
    (void) channel_name_;
    const auto deadline = std::chrono::steady_clock::now ()
                          + std::chrono::milliseconds (
                            std::max (1, timeout_ms_));
    while (std::chrono::steady_clock::now () < deadline) {
        zlink::message_t outbound =
          zlink::message_t::from_bytes (payload_.data (), payload_.size ());
        if (!outbound.valid ()) {
            errno = EINVAL;
            return false;
        }

        const zlink::send_result_t result =
          try_publish_nowait (spot_, topic_, outbound);
        if (result == zlink::send_result_t::sent)
            return true;
        if (result != zlink::send_result_t::backpressured
            && result != zlink::send_result_t::not_ready) {
            errno = EFAULT;
            return false;
        }
        if (!wait_for_spot_control_event (
              spot_, zlink::poll_event_flag_t::pollout, deadline))
            return false;
    }

    errno = ETIMEDOUT;
    return false;
}

template<typename SpotHandle>
inline bool publish_control_payload (SpotHandle &spot_,
                                     const std::string &topic_,
                                     const std::string &payload_,
                                     int timeout_ms_)
{
    return publish_control_message (
      spot_, std::string (), topic_, payload_, timeout_ms_);
}

template<typename SpotHandle>
inline bool wait_for_control_start (SpotHandle &spot_,
                                    const std::string &topic_,
                                    size_t msg_size_,
                                    int timeout_ms_)
{
    const auto deadline = std::chrono::steady_clock::now ()
                          + std::chrono::milliseconds (
                            std::max (1, timeout_ms_));
    while (std::chrono::steady_clock::now () < deadline) {
        try {
            const std::optional<zlink::topic_message_t> received =
              try_subscribe_nowait (spot_);
            if (!received) {
                if (!wait_for_spot_control_event (
                      spot_, zlink::poll_event_flag_t::pollin, deadline))
                    return false;
                continue;
            }
            if (received->topic () != topic_ || received->parts ().empty ())
                continue;

            const zlink::message_t &part = received->parts ()[0];
            const std::string payload (
              static_cast<const char *> (part.data ()), part.size ());
            size_t start_size = 0;
            if (parse_size_command_line (payload, "START,", &start_size)
                && start_size == msg_size_)
                return true;
        }
        catch (const zlink::recv_error_t &err) {
            const int err_no = err.internal_errno ();
            if (err_no != EAGAIN && err_no != EWOULDBLOCK
                && err_no != ETIMEDOUT && err_no != EINTR) {
                errno = err_no;
                return false;
            }
        }
    }
    errno = ETIMEDOUT;
    return false;
}

template<typename SpotHandle, typename SpotNode>
inline bool wait_ready_count_and_data_endpoint (SpotHandle &control_sub_,
                                                SpotNode *data_node_,
                                                const std::string &topic_,
                                                size_t msg_size_,
                                                size_t expected_ready_count_,
                                                int timeout_ms_)
{
    size_t ready_count = 0;
    bool data_endpoint_seen = false;
    const auto deadline = std::chrono::steady_clock::now ()
                          + std::chrono::milliseconds (
                            std::max (1, timeout_ms_));
    while (std::chrono::steady_clock::now () < deadline) {
        try {
            const std::optional<zlink::topic_message_t> received =
              try_subscribe_nowait (control_sub_);
            if (!received) {
                if (!wait_for_spot_control_event (
                      control_sub_, zlink::poll_event_flag_t::pollin, deadline))
                    return false;
                continue;
            }
            if (received->topic () != topic_ || received->parts ().empty ())
                continue;

            const zlink::message_t &part = received->parts ()[0];
            const std::string payload (
              static_cast<const char *> (part.data ()), part.size ());
            std::string endpoint;
            if (parse_endpoint_command_line (
                  payload, "DATA_ENDPOINT,", &endpoint)) {
                if (data_node_ && !endpoint.empty ()) {
                    try {
                        data_node_->connect_peer (endpoint);
                        data_endpoint_seen = true;
                    }
                    catch (const std::exception &) {
                        return false;
                    }
                }
                continue;
            }

            size_t ready_size = 0;
            size_t increment = 0;
            if (parse_size_count_command_line (
                  payload, "READY_COUNT,", &ready_size, &increment)
                && ready_size == msg_size_) {
                ready_count += increment;
                if (data_endpoint_seen
                    && ready_count >= expected_ready_count_)
                    return true;
            }
            if (data_endpoint_seen && ready_count >= expected_ready_count_)
                return true;
        }
        catch (const zlink::recv_error_t &err) {
            const int err_no = err.internal_errno ();
            if (err_no != EAGAIN && err_no != EWOULDBLOCK
                && err_no != ETIMEDOUT && err_no != EINTR) {
                errno = err_no;
                return false;
            }
        }
    }
    errno = ETIMEDOUT;
    return false;
}

template<typename SpotHandle, typename ParseFn>
inline bool wait_for_ready_counts (SpotHandle &spot_,
                                   const std::string &channel_name_,
                                   const std::string &topic_,
                                   size_t msg_size_,
                                   size_t expected_ready_count_,
                                   int timeout_ms_,
                                   ParseFn parse_fn_)
{
    (void) channel_name_;
    if (expected_ready_count_ == 0)
        return true;

    size_t ready_count = 0;
    const auto deadline = std::chrono::steady_clock::now ()
                          + std::chrono::milliseconds (
                            std::max (1, timeout_ms_));
    while (std::chrono::steady_clock::now () < deadline) {
        const std::optional<zlink::topic_message_t> maybe_received =
          try_subscribe_nowait (spot_);
        if (!maybe_received) {
            if (!wait_for_spot_control_event (
                  spot_, zlink::poll_event_flag_t::pollin, deadline))
                return false;
            continue;
        }

        const zlink::topic_message_t &received = *maybe_received;
        if (received.topic () != topic_ || received.parts ().size () != 1)
            continue;

        const std::string payload (
          static_cast<const char *> (received.parts ()[0].data ()),
          received.parts ()[0].size ());
        size_t ready_size = 0;
        size_t increment = 0;
        if (!parse_fn_ (payload, &ready_size, &increment)
            || ready_size != msg_size_) {
            continue;
        }

        ready_count += increment;
        if (ready_count >= expected_ready_count_)
            return true;
    }

    errno = ETIMEDOUT;
    return false;
}

template<typename SpotNode, typename SpotHandle>
inline bool initialize_client_control_session (
  zlink::context_t &ctx_,
  const std::string &transport_,
  const std::string &remote_control_endpoint_,
  const std::string &channel_name_,
  const std::string &control_topic_,
  const multi_bench_settings_t &settings_,
  size_t msg_size_,
  std::unique_ptr<SpotNode> *control_node_out_,
  std::unique_ptr<zlink::service::discovery_t> *control_discovery_out_,
  std::unique_ptr<SpotHandle> *control_spot_out_,
  std::string *local_control_endpoint_out_)
{
    if (!control_node_out_ || !control_discovery_out_ || !control_spot_out_
        || !local_control_endpoint_out_ || remote_control_endpoint_.empty ()
        || control_topic_.empty () || channel_name_.empty ()) {
        errno = EINVAL;
        return false;
    }

    std::unique_ptr<SpotNode> control_node (new SpotNode (ctx_));
    if (!control_node->valid ())
        return false;

    std::unique_ptr<zlink::service::discovery_t> control_discovery (
      new zlink::service::discovery_t (
        ctx_, zlink::auto_connect_type::spot_mesh, channel_name_));
    if (!control_discovery->valid ())
        return false;
    control_node->attach_discovery (*control_discovery);

    if (!configure_spot_control_tls (*control_node, transport_))
        return false;
    if (!apply_spot_auto_hwm_msg_unit (ctx_, msg_size_))
        return false;

    const int base_port =
      45500 + static_cast<int> (perf_metric::now_ns () % 1000) * 4;
    const std::string local_control_endpoint =
      bind_spot_endpoint (*control_node, transport_, base_port);
    if (local_control_endpoint.empty ())
        return false;
    try {
        control_node->connect_peer (remote_control_endpoint_);
    }
    catch (const std::exception &) {
        return false;
    }

    if (!apply_spot_node_admission_hwm (
          *control_node, settings_.sndhwm, settings_.rcvhwm))
        return false;

    std::unique_ptr<SpotHandle> control_spot (
      new SpotHandle (control_node->create_spot ()));
    if (!control_spot->valid ())
        return false;
    control_spot->options ().send_timeout (
      std::chrono::milliseconds (settings_.sndtimeo_ms));
    control_spot->options ().recv_timeout (
      std::chrono::milliseconds (settings_.rcvtimeo_ms));
    control_spot->set_subscription (control_topic_.c_str ());

    *local_control_endpoint_out_ = local_control_endpoint;
    *control_node_out_ = std::move (control_node);
    *control_discovery_out_ = std::move (control_discovery);
    *control_spot_out_ = std::move (control_spot);
    return true;
}

template<typename SlotT, typename SpotNode, typename SpotHandle>
inline bool initialize_client_slot (
  zlink::context_t &ctx_,
  const std::string &transport_,
  const std::string &server_endpoint_,
  const char *topic_,
  const multi_bench_settings_t &settings_,
  size_t msg_size_,
  SlotT *slot_)
{
    if (!slot_ || !topic_ || !*topic_ || server_endpoint_.empty ()) {
        errno = EINVAL;
        return false;
    }

    slot_->node.reset (new SpotNode (ctx_));
    if (!slot_->node->valid ())
        return false;

    if (!apply_spot_node_admission_hwm (
          *slot_->node, settings_.sndhwm, settings_.rcvhwm))
        return false;
    if (!apply_spot_auto_hwm_msg_unit (ctx_, msg_size_))
        return false;

    slot_->spot.reset (new SpotHandle (slot_->node->create_spot ()));
    if (!slot_->spot->valid ())
        return false;
    if (!configure_spot_client_tls (*slot_->node, transport_))
        return false;
    try {
        slot_->node->connect_peer (server_endpoint_);
    }
    catch (const std::exception &) {
        return false;
    }

    slot_->spot->request_timeout (
      std::chrono::milliseconds (settings_.rcvtimeo_ms));
    slot_->spot->set_subscription (topic_);

    return true;
}

} // namespace multi
} // namespace perf

#endif
