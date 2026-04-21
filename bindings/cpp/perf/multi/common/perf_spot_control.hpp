#ifndef PERF_SPOT_CONTROL_HPP
#define PERF_SPOT_CONTROL_HPP

#include "perf_spot_handshake.hpp"
#include "perf_tls.hpp"
#include "../../common/perf_socket_compat.hpp"

#include <cerrno>
#include <chrono>
#include <condition_variable>
#include <cstdlib>
#include <mutex>
#include <string>
#include <thread>

namespace perf {
namespace multi {

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
                    const std::string &service_name_,
                    const std::string &topic_,
                    zlink::message_t &outbound)
{
    try {
        spot_.publish (
          service_name_, topic_, outbound, zlink::send_flags_t::dontwait);
        return zlink::send_result_t::sent;
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
inline zlink::maybe_t<zlink::topic_message_t>
try_subscribe_nowait (SpotHandle &spot_)
{
    std::optional<zlink::topic_message_t> message =
      spot_.subscribe (zlink::recv_flags_t::dontwait);
    if (!message.has_value ())
        return zlink::maybe_t<zlink::topic_message_t> ();
    return zlink::maybe_t<zlink::topic_message_t> (std::move (*message));
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
            node_.bind (requested_endpoint);
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

template<typename SpotHandle, typename WaitFn>
inline bool publish_control_message (SpotHandle &spot_,
                                     const std::string &service_name_,
                                     const std::string &topic_,
                                     const std::string &payload_,
                                     int timeout_ms_,
                                     WaitFn wait_fn_)
{
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
          try_publish_nowait (spot_, service_name_, topic_, outbound);
        if (result == zlink::send_result_t::sent)
            return true;
        if (result != zlink::send_result_t::backpressured
            && result != zlink::send_result_t::not_ready) {
            errno = EFAULT;
            return false;
        }
        if (!wait_fn_ ())
            return false;
    }

    errno = ETIMEDOUT;
    return false;
}

template<typename SpotHandle, typename ParseFn, typename IdleFn>
inline bool wait_for_ready_counts (SpotHandle &spot_,
                                   const std::string &service_name_,
                                   const std::string &topic_,
                                   size_t msg_size_,
                                   size_t expected_ready_count_,
                                   int timeout_ms_,
                                   ParseFn parse_fn_,
                                   IdleFn idle_fn_)
{
    if (expected_ready_count_ == 0)
        return true;

    size_t ready_count = 0;
    const auto deadline = std::chrono::steady_clock::now ()
                          + std::chrono::milliseconds (
                            std::max (1, timeout_ms_));
    while (std::chrono::steady_clock::now () < deadline) {
        const zlink::maybe_t<zlink::topic_message_t> maybe_received =
          try_subscribe_nowait (spot_);
        if (!maybe_received) {
            if (!idle_fn_ ())
                return false;
            continue;
        }

        const zlink::topic_message_t &received = *maybe_received;
        if (!service_name_.empty ()
            && (!received.service_name ()
                || *received.service_name () != service_name_))
            continue;
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
  const std::string &service_name_,
  const std::string &control_topic_,
  const multi_bench_settings_t &settings_,
  std::unique_ptr<SpotNode> *control_node_out_,
  std::unique_ptr<zlink::service::discovery_t> *control_discovery_out_,
  std::unique_ptr<SpotHandle> *control_spot_out_,
  std::string *local_control_endpoint_out_)
{
    if (!control_node_out_ || !control_discovery_out_ || !control_spot_out_
        || !local_control_endpoint_out_ || remote_control_endpoint_.empty ()
        || control_topic_.empty () || service_name_.empty ()) {
        errno = EINVAL;
        return false;
    }

    std::unique_ptr<SpotNode> control_node (new SpotNode (ctx_));
    if (!control_node->valid ())
        return false;

    std::unique_ptr<zlink::service::discovery_t> control_discovery (
      new zlink::service::discovery_t (
        ctx_, zlink::service_type::spot, service_name_));
    if (!control_discovery->valid ())
        return false;
    control_node->attach_discovery (*control_discovery);

    std::unique_ptr<SpotHandle> control_spot (
      new SpotHandle (control_node->create_spot ()));
    if (!control_spot->valid ())
        return false;

    if (!configure_spot_control_tls (*control_node, transport_))
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

    control_spot->options ().send_hwm (settings_.sndhwm);
    control_spot->options ().recv_hwm (settings_.rcvhwm);
    control_spot->options ().send_timeout (settings_.sndtimeo_ms);
    control_spot->options ().recv_timeout (settings_.rcvtimeo_ms);
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
  SlotT *slot_)
{
    if (!slot_ || !topic_ || !*topic_ || server_endpoint_.empty ()) {
        errno = EINVAL;
        return false;
    }

    slot_->node.reset (new SpotNode (ctx_));
    if (!slot_->node->valid ())
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

    slot_->spot->options ().recv_hwm (settings_.rcvhwm);
    slot_->spot->options ().recv_timeout (settings_.rcvtimeo_ms);
    slot_->spot->set_subscription (topic_);

    return true;
}

} // namespace multi
} // namespace perf

#endif
