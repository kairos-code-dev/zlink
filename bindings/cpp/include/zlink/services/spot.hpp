/* SPDX-License-Identifier: MPL-2.0 */
#ifndef ZLINK_CPP_SERVICES_SPOT_HPP_INCLUDED
#define ZLINK_CPP_SERVICES_SPOT_HPP_INCLUDED

#include "../context.hpp"
#include "../async_result.hpp"
#include "../message.hpp"
#include "../socket_types.hpp"
#include "../types.hpp"
#include "discovery.hpp"

#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <future>
#include <optional>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

// Compatibility aggregate for SPOT service classes. Public class entrypoint
// headers are zlink/services/spot_node.hpp, spot.hpp, and actor.hpp.

zlink_recv_result_t spot_subscribe_impl (void *spot_,
                                         zlink_routing_id_t *source_rid_out_,
                                         zlink_msg_t **parts_out_,
                                         size_t *part_count_out_,
                                         char *service_name_out_,
                                         size_t *service_name_len_out_,
                                         char *topic_id_out_,
                                         size_t *topic_id_len_out_,
                                         zlink_recv_flags_t flags_);

zlink_recv_result_t spot_recv_impl (void *spot_,
                                    const zlink_routing_id_t **source_rid_out_,
                                    const zlink_routing_id_t **spot_rid_out_,
                                    uint64_t *request_seq_out_,
                                    zlink_msg_t **parts_out_,
                                    size_t *part_count_out_,
                                    zlink_recv_flags_t flags_);

namespace zlink
{
extern "C" zlink_recv_result_t zlink_spot_subscription_event_part (
  void *spot_,
  const zlink_routing_id_t **source_rid_out_,
  int *subscribed_out_,
  char *service_name_buf_,
  size_t service_name_capacity_,
  size_t *service_name_len_out_,
  char *topic_id_buf_,
  size_t topic_id_capacity_,
  size_t *topic_id_len_out_,
  zlink_recv_flags_t flags_);
namespace service
{

class spot_node_t;
class spot_t;
class actor_t;
class send_op_t;
class send_ready_op_t;
class request_op_t;
class request_ready_op_t;
class request_callback_ready_op_t;
class reply_op_t;
class reply_ready_op_t;

} // namespace service
namespace detail
{
inline void *native_handle (service::spot_node_t &node_) noexcept;
inline const void *native_handle (const service::spot_node_t &node_) noexcept;
inline void *native_handle (service::spot_t &spot_) noexcept;
inline const void *native_handle (const service::spot_t &spot_) noexcept;
} // namespace detail
namespace service
{

namespace detail
{

extern "C" int zlink_spot_request_progress_internal (void *spot_);
extern "C" int zlink_spot_request_channel_progress_internal (
  void *spot_, const char *channel_name_);

using zlink::detail::last_error;
using zlink::detail::throw_if_failed;

inline void close_message_array (zlink_msg_t *parts_, size_t part_count_) noexcept
{
    if (!parts_)
        return;
    zlink_multipart_close (parts_, part_count_);
}

inline void close_native_parts (std::vector<zlink_msg_t> &parts_,
                                size_t start_index_ = 0) noexcept
{
    if (start_index_ >= parts_.size ())
        return;

    for (size_t i = start_index_; i < parts_.size (); ++i)
        (void) zlink_msg_close (&parts_[i]);
}

inline int move_parts_to_native (std::vector<message_t> &parts_,
                                 std::vector<zlink_msg_t> &native_)
{
    native_.clear ();
    native_.resize (parts_.size ());

    size_t moved = 0;
    for (; moved < parts_.size (); ++moved) {
        if (!parts_[moved].valid ()) {
            errno = EINVAL;
            break;
        }
        zlink::detail::move_to_native (parts_[moved], &native_[moved]);
        if (parts_[moved].valid ())
            break;
    }

    if (moved == parts_.size ())
        return 0;

    for (size_t i = 0; i < moved; ++i) {
        parts_[i].init ();
        if (parts_[i].valid ())
            (void) zlink_msg_move (zlink::detail::native_handle (parts_[i]), &native_[i]);
        (void) zlink_msg_close (&native_[i]);
    }

    native_.clear ();
    return -1;
}

inline void restore_parts_from_native (std::vector<message_t> &parts_,
                                       std::vector<zlink_msg_t> &native_,
                                       size_t start_index_ = 0) noexcept
{
    const size_t count =
      native_.size () < parts_.size () ? native_.size () : parts_.size ();
    for (size_t i = start_index_; i < count; ++i) {
        parts_[i].init ();
        if (parts_[i].valid ())
            (void) zlink_msg_move (zlink::detail::native_handle (parts_[i]), &native_[i]);
        (void) zlink_msg_close (&native_[i]);
    }
    native_.clear ();
}

inline int assign_parts_from_native (zlink_msg_t *parts_native_,
                                     size_t part_count_,
                                     std::vector<message_t> &parts_)
{
    parts_.clear ();
    parts_.resize (part_count_);
    for (size_t i = 0; i < part_count_; ++i) {
        if (zlink_msg_move (zlink::detail::native_handle (parts_[i]), &parts_native_[i]) != 0) {
            parts_.clear ();
            close_message_array (parts_native_, part_count_);
            return -1;
        }
    }
    close_message_array (parts_native_, part_count_);
    return 0;
}

inline int assign_parts_from_native (std::vector<zlink_msg_t> &parts_native_,
                                     std::vector<message_t> &parts_)
{
    parts_.clear ();
    parts_.resize (parts_native_.size ());
    for (size_t i = 0; i < parts_native_.size (); ++i) {
        if (zlink_msg_move (zlink::detail::native_handle (parts_[i]), &parts_native_[i]) != 0) {
            parts_.clear ();
            close_native_parts (parts_native_, i);
            parts_native_.clear ();
            return -1;
        }
    }
    parts_native_.clear ();
    return 0;
}

template<typename SubmitFn>
inline int submit_native_parts (std::vector<zlink_msg_t> &parts_native_,
                                size_t &failed_index_out_,
                                SubmitFn submit_)
{
    failed_index_out_ = 0;
    if (parts_native_.empty ()) {
        errno = EFAULT;
        return ZLINK_SUBMIT_INVALID_HANDLE;
    }

    for (size_t i = 0; i < parts_native_.size (); ++i) {
        const bool is_final = i + 1u == parts_native_.size ();
        const zlink_part_flag_t part_flag =
          is_final ? ZLINK_PART_FINAL : ZLINK_PART_MORE;
        const int rc = submit_ (&parts_native_[i], part_flag, is_final);
        if (rc != ZLINK_SUBMIT_OK) {
            failed_index_out_ = i;
            return rc;
        }
    }

    return ZLINK_SUBMIT_OK;
}

template<typename RecvFn>
inline int collect_parts_from_recv (RecvFn recv_, std::vector<message_t> &parts_out_)
{
    std::vector<zlink_msg_t> native_parts;

    for (;;) {
        native_parts.emplace_back ();
        zlink_msg_t &native_part = native_parts.back ();
        if (zlink_msg_init (&native_part) != 0) {
            native_parts.pop_back ();
            close_native_parts (native_parts);
            return -1;
        }

        zlink_part_flag_t has_more = ZLINK_PART_FINAL;
        const int rc = recv_ (&native_part, &has_more, native_parts.size () == 1u);
        if (rc != ZLINK_RECV_OK) {
            (void) zlink_msg_close (&native_part);
            native_parts.pop_back ();
            close_native_parts (native_parts);
            return rc;
        }

        if (!has_more)
            break;
    }

    return assign_parts_from_native (native_parts, parts_out_);
}

template<typename Getter, typename Option>
inline int get_string_option (Getter getter_,
                              void *handle_,
                              Option option_,
                              size_t initial_capacity_,
                              std::string &value_)
{
    size_t capacity = initial_capacity_;
    const size_t max_capacity = 64u * 1024u;

    while (capacity <= max_capacity) {
        std::vector<char> buffer (capacity);
        size_t size = capacity;
        if (getter_ (
              handle_, option_, buffer.data (), &size)
            == 0) {
            const size_t bounded = size <= buffer.size () ? size : buffer.size ();
            size_t out_size = bounded;
            if (out_size > 0 && buffer[out_size - 1] == '\0')
                --out_size;
            value_.assign (buffer.data (), out_size);
            return 0;
        }

        if (errno != EINVAL || capacity == max_capacity)
            return -1;

        capacity *= 2u;
        if (capacity > max_capacity)
            capacity = max_capacity;
    }

    errno = EINVAL;
    return -1;
}

inline send_result_t to_send_result (int result_) noexcept
{
    switch (result_) {
    case ZLINK_SUBMIT_OK:
        return send_result_t::sent;
    case ZLINK_SUBMIT_BACKPRESSURED:
        return send_result_t::backpressured;
    case ZLINK_SUBMIT_NOT_CONNECTED:
        return send_result_t::not_ready;
    default:
        return send_result_t::sent;
    }
}

inline bool classify_nonblocking_send_errno (int err_,
                                             send_result_t &result_) noexcept
{
    switch (err_) {
    case EAGAIN:
        result_ = send_result_t::backpressured;
        return true;
    case ENOTCONN:
    case EHOSTUNREACH:
        result_ = send_result_t::not_ready;
        return true;
    default:
        return false;
    }
}

struct request_state_t
{
    std::unique_ptr<std::promise<std::vector<message_t>>> promise;
    std::function<void(request_result_t, std::vector<message_t>)> on_complete;
};

inline request_state_t *make_future_request_state ()
{
    request_state_t *state = new request_state_t ();
    state->promise.reset (new std::promise<std::vector<message_t>> ());
    return state;
}

inline request_state_t *
make_callback_request_state (
  std::function<void(request_result_t, std::vector<message_t>)> callback_)
{
    request_state_t *state = new request_state_t ();
    state->on_complete = std::move (callback_);
    return state;
}

inline std::function<void()> make_spot_request_progress (void *spot_)
{
    return [spot_]() { (void) zlink_spot_request_progress_internal (spot_); };
}

inline std::function<void()> make_spot_request_progress (void *spot_,
                                                         const std::string &channel_name_)
{
    return [spot_, channel_name_]() {
        (void) zlink_spot_request_channel_progress_internal (
          spot_, channel_name_.c_str ());
    };
}

inline std::vector<message_t> take_parts (zlink_msg_t *parts_, size_t part_count_)
{
    std::vector<message_t> parts;
    parts.resize (part_count_);
    for (size_t i = 0; i < part_count_; ++i)
        (void) zlink_msg_move (zlink::detail::native_handle (parts[i]), &parts_[i]);
    return parts;
}

inline void complete_request_state (request_state_t *state_,
                                    zlink_request_result_t result_,
                                    zlink_msg_t *parts_,
                                    size_t part_count_)
{
    if (!state_)
        return;
    std::unique_ptr<request_state_t> holder (state_);
    if (result_ != ZLINK_REQUEST_OK) {
        if (holder->on_complete)
            holder->on_complete (
              static_cast<request_result_t> (result_),
              std::vector<message_t> ());
        if (holder->promise) {
            holder->promise->set_exception (
              std::make_exception_ptr (
                request_error_t (static_cast<request_result_t> (result_))));
        }
        return;
    }
    std::vector<message_t> parts = take_parts (parts_, part_count_);
    if (holder->on_complete) {
        holder->on_complete (request_result_t::ok, std::move (parts));
        return;
    }
    if (holder->promise)
        holder->promise->set_value (std::move (parts));
}

inline void request_callback_trampoline (zlink_request_result_t result_,
                                         zlink_msg_t *parts_,
                                         size_t part_count_,
                                         void *userdata_)
{
    complete_request_state (
      static_cast<request_state_t *> (userdata_), result_, parts_, part_count_);
}

} // namespace detail

class spot_node_t
{
  public:
    explicit spot_node_t (context_t &ctx_)
        : _node (zlink_spot_node_new (zlink::detail::native_handle (ctx_), NULL)), _last_error (0)
    {
        if (!_node)
            _last_error = errno != 0 ? errno : EFAULT;
    }

    spot_node_t (context_t &ctx_, spot_node_mode_t mode_)
        : spot_node_t (ctx_, native_options (mode_))
    {
    }

    ~spot_node_t ()
    {
        try {
            close ();
        } catch (...) {
        }
    }

    spot_node_t (spot_node_t &&other) noexcept
        : _node (other._node), _last_error (other._last_error)
    {
        other._node = NULL;
        other._last_error = 0;
    }

    spot_node_t &operator= (spot_node_t &&other) noexcept
    {
        if (this == &other)
            return *this;

        try {
            close ();
        } catch (...) {
        }
        _node = other._node;
        _last_error = other._last_error;
        other._node = NULL;
        other._last_error = 0;
        return *this;
    }

    spot_node_t (const spot_node_t &) = delete;
    spot_node_t &operator= (const spot_node_t &) = delete;

    bool valid () const noexcept { return _node != NULL; }

    void bind (const std::string &endpoint_)
    {
        zlink::detail::validate_bounded_c_string (endpoint_, 255u, "endpoint");
        detail::throw_if_failed<bind_error_t> (
          static_cast<bind_result_t> (
            zlink_spot_node_bind (_node, endpoint_.c_str ())));
    }

    std::string last_endpoint () const
    {
        zlink_spot_node_status_t status;
        detail::throw_if_failed<config_error_t> (
          static_cast<config_result_t> (
            zlink_spot_node_status_snapshot (_node, &status)));
        return fixed_string_to_string (status.local_endpoint);
    }

    void connect_peer (const std::string &endpoint_)
    {
        zlink::detail::validate_bounded_c_string (endpoint_, 255u, "endpoint");
        detail::throw_if_failed<connect_error_t> (
          static_cast<connect_result_t> (
            zlink_spot_node_connect_peer (_node, endpoint_.c_str ())));
    }

    void disconnect_peer (const std::string &endpoint_)
    {
        zlink::detail::validate_bounded_c_string (endpoint_, 255u, "endpoint");
        detail::throw_if_failed<connect_error_t> (
          static_cast<connect_result_t> (
            zlink_spot_node_disconnect_peer (_node, endpoint_.c_str ())));
    }

    void disconnect_peer_rid (const routing_id_t &target_node_rid_)
    {
        const zlink_routing_id_t native =
          *zlink::detail::routing_id_native (target_node_rid_);
        detail::throw_if_failed<connect_error_t> (
          static_cast<connect_result_t> (
            zlink_spot_node_disconnect_peer_rid (_node, &native)));
    }

    void attach_discovery (discovery_t &discovery_)
    {
        detail::throw_if_failed<config_error_t> (
          static_cast<config_result_t> (
            zlink_spot_node_attach_discovery (_node, zlink::detail::native_handle (discovery_))));
    }

    template<typename DealerT>
    void attach_channel_dealer (discovery_t &discovery_, DealerT &dealer_)
    {
        detail::throw_if_failed<config_error_t> (
          static_cast<config_result_t> (
            zlink_spot_node_attach_channel_dealer (
              _node, zlink::detail::native_handle (discovery_), zlink::detail::native_handle (dealer_))));
    }

    template<typename DealerT>
    void attach_channel_dealer_manual (const std::string &channel_name_,
                                       DealerT &dealer_)
    {
        zlink::detail::validate_bounded_c_string (channel_name_, 255u, "channel_name");
        if (channel_name_.empty ()) {
            errno = EINVAL;
            throw config_error_t (config_result_t::invalid_argument, EINVAL);
        }
        detail::throw_if_failed<config_error_t> (
          static_cast<config_result_t> (
            zlink_spot_node_attach_channel_dealer_manual (
              _node, channel_name_.c_str (), zlink::detail::native_handle (dealer_))));
    }

    template<typename PubT>
    void attach_pub_ingress (PubT &pub_)
    {
        detail::throw_if_failed<config_error_t> (
          static_cast<config_result_t> (
            zlink_spot_node_attach_pub_ingress (_node, zlink::detail::native_handle (pub_))));
    }

    void set_routing_id (const routing_id_t &routing_id_)
    {
        detail::throw_if_failed<config_error_t> (
          static_cast<config_result_t> (zlink_set_routing_id (
            _node, routing_id_.data (), routing_id_.size ())));
    }

    void get_routing_id (routing_id_t &out_) const
    {
        zlink_routing_id_t native;
        std::memset (&native, 0, sizeof (native));
        detail::throw_if_failed<config_error_t> (
          static_cast<config_result_t> (zlink_get_routing_id (_node, &native)));
        out_ = zlink::detail::native_routing_id (native);
    }

    routing_id_t routing_id () const
    {
        routing_id_t value = zlink::detail::unchecked_empty_routing_id ();
        get_routing_id (value);
        return value;
    }

    void set_tls_server (const std::string &cert_,
                         const std::string &key_,
                         bool require_client_cert_ = false)
    {
        detail::throw_if_failed<config_error_t> (
          static_cast<config_result_t> (zlink_set_tls_server (
            _node, cert_.c_str (), key_.c_str (),
            require_client_cert_ ? 1 : 0)));
    }

    void set_tls_client (const std::string &ca_cert_,
                         const std::string &hostname_ = std::string (),
                         bool trust_system_ = false)
    {
        const char *ca = ca_cert_.empty () ? NULL : ca_cert_.c_str ();
        const char *hostname =
          hostname_.empty () ? NULL : hostname_.c_str ();
        detail::throw_if_failed<config_error_t> (
          static_cast<config_result_t> (
            zlink_set_tls_client (
              _node, ca, hostname, trust_system_ ? 1 : 0)));
    }

    auto_hwm_profile router_admission_hwm_profile () const
    {
        return static_cast<auto_hwm_profile> (
          get_spot_node_option_int (ZLINK_SPOT_NODE_OPT_ROUTER_HWM_PROFILE));
    }

    void router_admission_hwm_profile (auto_hwm_profile profile_)
    {
        set_spot_node_option_int (
          ZLINK_SPOT_NODE_OPT_ROUTER_HWM_PROFILE,
          static_cast<int> (profile_));
    }

    message_count_t router_admission_hwm () const
    {
        return message_count_t::value (
          get_spot_node_option_int (ZLINK_SPOT_NODE_OPT_ROUTER_HWM));
    }

    void router_admission_hwm (message_count_t value_)
    {
        set_spot_node_option_int (
          ZLINK_SPOT_NODE_OPT_ROUTER_HWM, value_.value ());
    }

    auto_hwm_profile pubsub_admission_hwm_profile () const
    {
        return static_cast<auto_hwm_profile> (
          get_spot_node_option_int (ZLINK_SPOT_NODE_OPT_PUBSUB_HWM_PROFILE));
    }

    void pubsub_admission_hwm_profile (auto_hwm_profile profile_)
    {
        set_spot_node_option_int (
          ZLINK_SPOT_NODE_OPT_PUBSUB_HWM_PROFILE,
          static_cast<int> (profile_));
    }

    message_count_t pubsub_admission_hwm () const
    {
        return message_count_t::value (
          get_spot_node_option_int (ZLINK_SPOT_NODE_OPT_PUBSUB_HWM));
    }

    void pubsub_admission_hwm (message_count_t value_)
    {
        set_spot_node_option_int (
          ZLINK_SPOT_NODE_OPT_PUBSUB_HWM, value_.value ());
    }

    worker_count_t dispatch_workers_min () const
    {
        return worker_count_t::value (
          get_spot_node_option_int (
            ZLINK_SPOT_NODE_OPT_DISPATCH_WORKERS_MIN));
    }

    void dispatch_workers_min (worker_count_t value_)
    {
        set_spot_node_option_int (
          ZLINK_SPOT_NODE_OPT_DISPATCH_WORKERS_MIN, value_.value ());
    }

    worker_count_t dispatch_workers_max () const
    {
        return worker_count_t::value (
          get_spot_node_option_int (
            ZLINK_SPOT_NODE_OPT_DISPATCH_WORKERS_MAX));
    }

    void dispatch_workers_max (worker_count_t value_)
    {
        set_spot_node_option_int (
          ZLINK_SPOT_NODE_OPT_DISPATCH_WORKERS_MAX, value_.value ());
    }

    spot_node_status_t status_snapshot () const
    {
        zlink_spot_node_status_t native;
        detail::throw_if_failed<config_error_t> (
          static_cast<config_result_t> (
            zlink_spot_node_status_snapshot (_node, &native)));
        return spot_node_status_t (native);
    }

    std::vector<spot_node_peer_entry_t> peers_snapshot () const
    {
        size_t count = 0;
        detail::throw_if_failed<config_error_t> (
          static_cast<config_result_t> (
            zlink_spot_node_peers_snapshot (_node, NULL, &count)));
        std::vector<zlink_spot_node_peer_entry_t> native (count);
        if (count > 0) {
            detail::throw_if_failed<config_error_t> (
              static_cast<config_result_t> (
                zlink_spot_node_peers_snapshot (_node, native.data (), &count)));
            native.resize (count);
        }
        std::vector<spot_node_peer_entry_t> entries;
        entries.reserve (native.size ());
        for (size_t i = 0; i < native.size (); ++i)
            entries.push_back (spot_node_peer_entry_t (native[i]));
        return entries;
    }

    std::vector<spot_node_peer_entry_t>
    peers_query (const spot_node_peer_filter_t &filter_) const
    {
        zlink_spot_node_peer_filter_t native_filter;
        std::memset (&native_filter, 0, sizeof (native_filter));
        if (filter_.peer_endpoint ())
            std::snprintf (
              native_filter.peer_endpoint, sizeof (native_filter.peer_endpoint),
              "%s", filter_.peer_endpoint ()->c_str ());
        if (filter_.source ())
            native_filter.source =
              static_cast<zlink_spot_peer_source_t> (*filter_.source ());
        if (filter_.state ())
            native_filter.state =
              static_cast<zlink_spot_peer_state_t> (*filter_.state ());

        size_t count = 0;
        detail::throw_if_failed<config_error_t> (
          static_cast<config_result_t> (
            zlink_spot_node_peers_query (_node, &native_filter, NULL, &count)));
        std::vector<zlink_spot_node_peer_entry_t> native (count);
        if (count > 0) {
            detail::throw_if_failed<config_error_t> (
              static_cast<config_result_t> (zlink_spot_node_peers_query (
                _node, &native_filter, native.data (), &count)));
            native.resize (count);
        }
        std::vector<spot_node_peer_entry_t> entries;
        entries.reserve (native.size ());
        for (size_t i = 0; i < native.size (); ++i)
            entries.push_back (spot_node_peer_entry_t (native[i]));
        return entries;
    }

    std::vector<spot_node_subject_entry_t>
    subjects_snapshot (const spot_node_subject_filter_t *filter_ = NULL) const
    {
        zlink_spot_node_subject_filter_t native_filter;
        const zlink_spot_node_subject_filter_t *filter_ptr = NULL;
        if (filter_) {
            std::memset (&native_filter, 0, sizeof (native_filter));
            if (filter_->role ())
                native_filter.role =
                  static_cast<zlink_spot_role_t> (*filter_->role ());
            if (filter_->subject_kind ())
                native_filter.subject_kind =
                  static_cast<uint32_t> (*filter_->subject_kind ());
            if (filter_->subject ())
                std::snprintf (
                  native_filter.subject, sizeof (native_filter.subject), "%s",
                  filter_->subject ()->c_str ());
            filter_ptr = &native_filter;
        }

        size_t count = 0;
        detail::throw_if_failed<config_error_t> (
          static_cast<config_result_t> (
            zlink_spot_node_subjects_snapshot (_node, filter_ptr, NULL, &count)));
        std::vector<zlink_spot_node_subject_entry_t> native (count);
        if (count > 0) {
            detail::throw_if_failed<config_error_t> (
              static_cast<config_result_t> (
                zlink_spot_node_subjects_snapshot (
                  _node, filter_ptr, native.data (), &count)));
            native.resize (count);
        }
        std::vector<spot_node_subject_entry_t> entries;
        entries.reserve (native.size ());
        for (size_t i = 0; i < native.size (); ++i)
            entries.push_back (spot_node_subject_entry_t (native[i]));
        return entries;
    }

    std::vector<spot_node_subject_entry_t>
    subjects_snapshot (const spot_node_subject_filter_t &filter_) const
    {
        return subjects_snapshot (&filter_);
    }

    std::vector<spot_node_socket_snapshot_entry_t>
    internal_sockets_snapshot (
      const spot_node_socket_snapshot_filter_t *filter_ = NULL) const
    {
        zlink_spot_node_socket_snapshot_filter_t native_filter;
        const zlink_spot_node_socket_snapshot_filter_t *filter_ptr = NULL;
        if (filter_) {
            std::memset (&native_filter, 0, sizeof (native_filter));
            if (filter_->owner ())
                native_filter.owner =
                  static_cast<zlink_spot_node_socket_owner_t> (*filter_->owner ());
            if (filter_->socket_type ())
                native_filter.socket_type =
                  static_cast<zlink_socket_type_t> (*filter_->socket_type ());
            if (filter_->socket_name ())
                std::snprintf (
                  native_filter.socket_name, sizeof (native_filter.socket_name),
                  "%s", filter_->socket_name ()->c_str ());
            filter_ptr = &native_filter;
        }

        size_t count = 0;
        detail::throw_if_failed<config_error_t> (
          static_cast<config_result_t> (
            zlink_spot_node_internal_sockets_snapshot (
              _node, filter_ptr, NULL, &count)));
        std::vector<zlink_spot_node_socket_snapshot_entry_t> native (count);
        if (count > 0) {
            detail::throw_if_failed<config_error_t> (
              static_cast<config_result_t> (
                zlink_spot_node_internal_sockets_snapshot (
                  _node, filter_ptr, native.data (), &count)));
            native.resize (count);
        }
        std::vector<spot_node_socket_snapshot_entry_t> entries;
        entries.reserve (native.size ());
        for (size_t i = 0; i < native.size (); ++i)
            entries.push_back (spot_node_socket_snapshot_entry_t (native[i]));
        return entries;
    }

    std::vector<spot_node_socket_snapshot_entry_t>
    internal_sockets_snapshot (
      const spot_node_socket_snapshot_filter_t &filter_) const
    {
        return internal_sockets_snapshot (&filter_);
    }

    actor_t create_actor (const std::string &actor_id_);

    actor_ref_t actor_lookup (const std::string &actor_id_) const
    {
        zlink::detail::validate_bounded_c_string (actor_id_, ZLINK_ACTOR_ID_MAX - 1u,
                                   "actor_id");
        zlink_actor_ref_t native;
        std::memset (&native, 0, sizeof (native));
        detail::throw_if_failed<config_error_t> (
          static_cast<config_result_t> (
            zlink_spot_node_actor_lookup (_node, actor_id_.c_str (), &native)));
        return actor_ref_t (native);
    }

    static actor_ref_t remote_actor_ref (const routing_id_t &target_node_rid_,
                                         const std::string &actor_id_)
    {
        zlink::detail::validate_bounded_c_string (actor_id_, ZLINK_ACTOR_ID_MAX - 1u,
                                   "actor_id");
        zlink_actor_ref_t native;
        std::memset (&native, 0, sizeof (native));
        detail::throw_if_failed<config_error_t> (
          static_cast<config_result_t> (
            zlink_remote_actor_get_ref (
              zlink::detail::routing_id_native (target_node_rid_), actor_id_.c_str (),
              &native)));
        return actor_ref_t (native);
    }

    static actor_ref_t remote_actor_ref (const routing_id_t &target_node_rid_,
                                         const std::string &actor_id_,
                                         uint64_t)
    {
        return remote_actor_ref (target_node_rid_, actor_id_);
    }

    actor_create_result_t create_remote_actor (
      const routing_id_t &target_node_rid_,
      const std::string &actor_id_,
      message_t &message_,
      std::chrono::milliseconds timeout_ = {})
    {
        zlink::detail::validate_bounded_c_string (actor_id_, ZLINK_ACTOR_ID_MAX - 1u,
                                   "actor_id");
        zlink_msg_t native_message;
        zlink::detail::move_to_native (message_, &native_message);
        zlink_actor_create_result_t native_result;
        std::memset (&native_result, 0, sizeof (native_result));
        const request_result_t rc = static_cast<request_result_t> (
          zlink_spot_node_create_remote_actor (
            _node, zlink::detail::routing_id_native (target_node_rid_), actor_id_.c_str (),
            &native_message, &native_result,
            static_cast<uint32_t> (timeout_.count ())));
        if (rc != request_result_t::ok) {
            message_.init ();
            (void) zlink_msg_move (zlink::detail::native_handle (message_), &native_message);
            throw request_error_t (rc, zlink_errno ());
        }
        return actor_create_result_t (native_result);
    }

    void destroy_actor (const actor_ref_t &actor_,
                        std::chrono::milliseconds timeout_ = {})
    {
        detail::throw_if_failed<request_error_t> (
          static_cast<request_result_t> (
            zlink_spot_node_actor_destroy (
              _node, zlink::detail::actor_ref_native (actor_),
              static_cast<uint32_t> (timeout_.count ()))));
    }

    void on_actor_admission (
      std::function<actor_admission_result_t(const std::string &, const message_t &)>
        handler_)
    {
        _actor_admission_handler = std::move (handler_);
        detail::throw_if_failed<handler_error_t> (
          static_cast<handler_result_t> (
            zlink_spot_node_actor_admission_handler (
              _node, &spot_node_t::actor_admission_trampoline, this)));
    }

    bool join_actor (const actor_ref_t &actor_,
                     const routing_id_t &dest_node_rid_,
                     const routing_id_t &dest_spot_rid_,
                     message_t &message_,
                     std::function<void(request_result_t, std::vector<message_t>)> callback_,
                     send_flags_t flags_ = send_flags_t::none,
                     std::chrono::milliseconds timeout_ = {})
    {
        zlink_msg_t native;
        zlink::detail::move_to_native (message_, &native);
        detail::request_state_t *state =
          detail::make_callback_request_state (std::move (callback_));
        const submit_result_t rc = static_cast<submit_result_t> (
          zlink_spot_node_actor_join_spot (
            _node, zlink::detail::actor_ref_native (actor_), zlink::detail::routing_id_native (dest_node_rid_),
            zlink::detail::routing_id_native (dest_spot_rid_), &native,
            &detail::request_callback_trampoline, state,
            static_cast<zlink_send_flags_t> (flags_),
            static_cast<uint32_t> (timeout_.count ())));
        if (rc != submit_result_t::ok) {
            delete state;
            message_.init ();
            (void) zlink_msg_move (zlink::detail::native_handle (message_), &native);
            if (flags_ == send_flags_t::dontwait
                && rc == submit_result_t::backpressured)
                return false;
            throw submit_error_t (rc, zlink_errno ());
        }
        return true;
    }

    bool join_actor (const actor_ref_t &actor_,
                     const routing_id_t &dest_spot_rid_,
                     message_t &message_,
                     std::function<void(request_result_t, std::vector<message_t>)> callback_,
                     send_flags_t flags_ = send_flags_t::none,
                     std::chrono::milliseconds timeout_ = {})
    {
        return join_actor (actor_, routing_id (), dest_spot_rid_, message_,
                           std::move (callback_), flags_, timeout_);
    }

    void leave_actor (const actor_ref_t &actor_,
                      const routing_id_t &current_spot_rid_,
                      std::chrono::milliseconds timeout_ = {})
    {
        detail::throw_if_failed<request_error_t> (
          static_cast<request_result_t> (
            zlink_spot_node_actor_leave_spot (
              _node, zlink::detail::actor_ref_native (actor_),
              zlink::detail::routing_id_native (current_spot_rid_),
              static_cast<uint32_t> (timeout_.count ()))));
    }

    std::vector<spot_node_spot_entry_t> spots_snapshot () const
    {
        size_t count = 0;
        detail::throw_if_failed<config_error_t> (
          static_cast<config_result_t> (
            zlink_spot_node_spots_snapshot (_node, NULL, &count)));
        std::vector<zlink_spot_node_spot_entry_t> native (count);
        if (count > 0) {
            detail::throw_if_failed<config_error_t> (
              static_cast<config_result_t> (
                zlink_spot_node_spots_snapshot (
                  _node, native.data (), &count)));
            native.resize (count);
        }
        std::vector<spot_node_spot_entry_t> entries;
        entries.reserve (native.size ());
        for (size_t i = 0; i < native.size (); ++i)
            entries.push_back (spot_node_spot_entry_t (native[i]));
        return entries;
    }

    std::vector<spot_node_actor_entry_t> actors_snapshot () const
    {
        size_t count = 0;
        detail::throw_if_failed<config_error_t> (
          static_cast<config_result_t> (
            zlink_spot_node_actors_snapshot (_node, NULL, &count)));
        std::vector<zlink_spot_node_actor_entry_t> native (count);
        if (count > 0) {
            detail::throw_if_failed<config_error_t> (
              static_cast<config_result_t> (
                zlink_spot_node_actors_snapshot (
                  _node, native.data (), &count)));
            native.resize (count);
        }
        std::vector<spot_node_actor_entry_t> entries;
        entries.reserve (native.size ());
        for (size_t i = 0; i < native.size (); ++i)
            entries.push_back (spot_node_actor_entry_t (native[i]));
        return entries;
    }

    spot_t create_spot ();
    spot_t entry_spot ();
    std::optional<spot_t> spot_lookup (const routing_id_t &spot_rid_);

    void close ()
    {
        if (!_node)
            return;

        void *tmp = _node;
        detail::throw_if_failed<close_error_t> (
          static_cast<close_result_t> (zlink_spot_node_destroy (&tmp)));
        _node = NULL;
    }

  private:
    friend void *zlink::detail::native_handle (spot_node_t &node_) noexcept;
    friend const void *
    zlink::detail::native_handle (const spot_node_t &node_) noexcept;

    spot_node_t (context_t &ctx_,
                 const zlink_spot_node_options_t &options_)
        : _node (zlink_spot_node_new (zlink::detail::native_handle (ctx_), &options_)),
          _last_error (0)
    {
        if (!_node)
            _last_error = errno != 0 ? errno : EFAULT;
    }

    static zlink_spot_node_options_t native_options (spot_node_mode_t mode_)
    {
        zlink_spot_node_options_t options;
        std::memset (&options, 0, sizeof (options));
        options.mode = static_cast<zlink_spot_node_mode_t> (mode_);
        return options;
    }

    int get_spot_node_option_int (zlink_spot_node_option_t option_) const
    {
        int value = 0;
        size_t size = sizeof (value);
        detail::throw_if_failed<config_error_t> (
          static_cast<config_result_t> (
            zlink_get_spot_node_option (_node, option_, &value, &size)));
        return value;
    }

    void set_spot_node_option_int (zlink_spot_node_option_t option_, int value_)
    {
        detail::throw_if_failed<config_error_t> (
          static_cast<config_result_t> (
            zlink_set_spot_node_option (
              _node, option_, &value_, sizeof (value_))));
    }

    static zlink_actor_admission_result_t
    actor_admission_trampoline (void *,
                                const char *actor_id_,
                                const zlink_msg_t *message_,
                                void *userdata_)
    {
        spot_node_t *self = static_cast<spot_node_t *> (userdata_);
        if (!self || !self->_actor_admission_handler)
            return ZLINK_ACTOR_ADMISSION_REJECT;

        message_t message;
        if (message_) {
            zlink_msg_t *dest = zlink::detail::native_handle (message);
            if (zlink_msg_copy (
                  dest, const_cast<zlink_msg_t *> (message_)) != 0) {
                return ZLINK_ACTOR_ADMISSION_REJECT;
            }
        }

        const actor_admission_result_t result =
          self->_actor_admission_handler (
            actor_id_ ? std::string (actor_id_) : std::string (), message);
        return static_cast<zlink_actor_admission_result_t> (result);
    }

    void *_node;
    int _last_error;
    std::function<actor_admission_result_t(const std::string &, const message_t &)>
      _actor_admission_handler;
};

namespace detail
{
enum class spot_op_kind_t
{
    publish,
    send_channel,
    send_to_spot,
    request_channel,
    request_to_spot,
    request_to_router,
    reply_to_spot,
    reply_to_router
};

struct spot_op_state_t
{
    spot_t *spot = NULL;
    spot_op_kind_t kind = spot_op_kind_t::publish;
    std::string service_name;
    std::string topic;
    std::string channel_name;
    std::optional<routing_id_t> first_rid;
    std::optional<routing_id_t> second_rid;
    uint64_t request_seq = 0;
    std::vector<message_t> parts;
    send_flags_t flags = send_flags_t::none;
    std::chrono::milliseconds timeout {};
};
} // namespace detail

class send_ready_op_t
{
  public:
    send_ready_op_t (send_ready_op_t &&) noexcept = default;
    send_ready_op_t &operator= (send_ready_op_t &&) noexcept = default;

    send_ready_op_t &&message (message_t &part_) &&
    {
        _state.parts.push_back (std::move (part_));
        return std::move (*this);
    }

    send_ready_op_t &&flags (int flags_) &&
    {
        _state.flags = send_flags_t (flags_);
        return std::move (*this);
    }

    bool submit () &&;

  private:
    explicit send_ready_op_t (detail::spot_op_state_t state_)
        : _state (std::move (state_))
    {
    }

    detail::spot_op_state_t _state;
    friend class send_op_t;
};

class send_op_t
{
  public:
    send_op_t (send_op_t &&) noexcept = default;
    send_op_t &operator= (send_op_t &&) noexcept = default;

    send_ready_op_t message (message_t &part_) &&
    {
        _state.parts.push_back (std::move (part_));
        return send_ready_op_t (std::move (_state));
    }

  private:
    explicit send_op_t (detail::spot_op_state_t state_)
        : _state (std::move (state_))
    {
    }

    detail::spot_op_state_t _state;
    friend class spot_t;
};

class request_callback_ready_op_t;

class request_ready_op_t
{
  public:
    request_ready_op_t (request_ready_op_t &&) noexcept = default;
    request_ready_op_t &operator= (request_ready_op_t &&) noexcept = default;

    request_ready_op_t &&message (message_t &part_) &&
    {
        _state.parts.push_back (std::move (part_));
        return std::move (*this);
    }

    request_ready_op_t &&timeout (std::chrono::milliseconds timeout_) &&
    {
        _state.timeout = timeout_;
        return std::move (*this);
    }

    request_callback_ready_op_t flags (int flags_) &&;
    async_result_t<std::vector<message_t>> submit_async () &&;
    bool submit (request_callback_t callback_) &&;

  private:
    explicit request_ready_op_t (detail::spot_op_state_t state_)
        : _state (std::move (state_))
    {
    }

    detail::spot_op_state_t _state;
    friend class request_op_t;
    friend class request_callback_ready_op_t;
};

class request_op_t
{
  public:
    request_op_t (request_op_t &&) noexcept = default;
    request_op_t &operator= (request_op_t &&) noexcept = default;

    request_ready_op_t message (message_t &part_) &&
    {
        _state.parts.push_back (std::move (part_));
        return request_ready_op_t (std::move (_state));
    }

  private:
    explicit request_op_t (detail::spot_op_state_t state_)
        : _state (std::move (state_))
    {
    }

    detail::spot_op_state_t _state;
    friend class spot_t;
};

class request_callback_ready_op_t
{
  public:
    request_callback_ready_op_t (request_callback_ready_op_t &&) noexcept =
      default;
    request_callback_ready_op_t &
    operator= (request_callback_ready_op_t &&) noexcept = default;

    request_callback_ready_op_t &&message (message_t &part_) &&
    {
        _state.parts.push_back (std::move (part_));
        return std::move (*this);
    }

    request_callback_ready_op_t &&timeout (std::chrono::milliseconds timeout_) &&
    {
        _state.timeout = timeout_;
        return std::move (*this);
    }

    request_callback_ready_op_t &&flags (int flags_) &&
    {
        _state.flags = send_flags_t (flags_);
        return std::move (*this);
    }

    bool submit (request_callback_t callback_) &&;

  private:
    explicit request_callback_ready_op_t (detail::spot_op_state_t state_)
        : _state (std::move (state_))
    {
    }

    detail::spot_op_state_t _state;
    friend class request_ready_op_t;
};

class reply_ready_op_t
{
  public:
    reply_ready_op_t (reply_ready_op_t &&) noexcept = default;
    reply_ready_op_t &operator= (reply_ready_op_t &&) noexcept = default;

    reply_ready_op_t &&message (message_t &part_) &&
    {
        _state.parts.push_back (std::move (part_));
        return std::move (*this);
    }

    reply_ready_op_t &&flags (int flags_) &&
    {
        _state.flags = send_flags_t (flags_);
        return std::move (*this);
    }

    void submit () &&;

  private:
    explicit reply_ready_op_t (detail::spot_op_state_t state_)
        : _state (std::move (state_))
    {
    }

    detail::spot_op_state_t _state;
    friend class reply_op_t;
};

class reply_op_t
{
  public:
    reply_op_t (reply_op_t &&) noexcept = default;
    reply_op_t &operator= (reply_op_t &&) noexcept = default;

    reply_ready_op_t message (message_t &part_) &&
    {
        _state.parts.push_back (std::move (part_));
        return reply_ready_op_t (std::move (_state));
    }

  private:
    explicit reply_op_t (detail::spot_op_state_t state_)
        : _state (std::move (state_))
    {
    }

    detail::spot_op_state_t _state;
    friend class spot_t;
};

class spot_t
{
  public:
    ~spot_t ()
    {
        try {
            close ();
        } catch (...) {
        }
    }

    spot_t (spot_t &&other) noexcept
        : _spot (other._spot), _last_error (other._last_error),
          _default_request_timeout (other._default_request_timeout)
    {
        other._spot = NULL;
        other._last_error = 0;
        other._default_request_timeout = std::chrono::milliseconds ();
    }

    spot_t &operator= (spot_t &&other) noexcept
    {
        if (this == &other)
            return *this;

        try {
            close ();
        } catch (...) {
        }
        _spot = other._spot;
        _last_error = other._last_error;
        _default_request_timeout = other._default_request_timeout;
        other._spot = NULL;
        other._last_error = 0;
        other._default_request_timeout = std::chrono::milliseconds ();
        return *this;
    }

    spot_t (const spot_t &) = delete;
    spot_t &operator= (const spot_t &) = delete;

    bool valid () const noexcept { return _spot != NULL; }

    void request_timeout (std::chrono::milliseconds timeout_)
    {
        const int value = static_cast<int> (timeout_.count ());
        detail::throw_if_failed<config_error_t> (
          static_cast<config_result_t> (
            zlink_set_spot_option (
              _spot, ZLINK_SPOT_OPT_REQUEST_TIMEOUT_MS, &value,
              sizeof (value))));
        _default_request_timeout = timeout_;
    }

    std::chrono::milliseconds request_timeout () const
    {
        int value = 0;
        size_t size = sizeof (value);
        detail::throw_if_failed<config_error_t> (
          static_cast<config_result_t> (
            zlink_get_spot_option (
              _spot, ZLINK_SPOT_OPT_REQUEST_TIMEOUT_MS, &value, &size)));
        return std::chrono::milliseconds (value);
    }

    send_op_t publish (const std::string &service_name_,
                       const std::string &topic_)
    {
        validate_service_name (service_name_);
        zlink::detail::validate_no_embedded_null (topic_, "topic");
        detail::spot_op_state_t state;
        state.spot = this;
        state.kind = detail::spot_op_kind_t::publish;
        state.service_name = service_name_;
        state.topic = topic_;
        return send_op_t (std::move (state));
    }

    send_op_t send_channel (const std::string &channel_name_)
    {
        validate_channel_name (channel_name_);
        detail::spot_op_state_t state;
        state.spot = this;
        state.kind = detail::spot_op_kind_t::send_channel;
        state.channel_name = channel_name_;
        return send_op_t (std::move (state));
    }

    request_op_t request_channel (const std::string &channel_name_)
    {
        validate_channel_name (channel_name_);
        detail::spot_op_state_t state;
        state.spot = this;
        state.kind = detail::spot_op_kind_t::request_channel;
        state.channel_name = channel_name_;
        return request_op_t (std::move (state));
    }

  private:
    bool publish (const std::string &service_name_,
                  const std::string &topic_,
                  std::vector<message_t> &parts_,
                  send_flags_t flags_ = send_flags_t::none)
    {
        validate_service_name (service_name_);
        zlink::detail::validate_no_embedded_null (topic_, "topic");
        const int rc = publish_impl (
          service_name_.c_str (), topic_.c_str (), parts_, flags_);
        if (rc != 0)
        {
            if (flags_ == send_flags_t::dontwait
                && static_cast<submit_result_t> (rc)
                    == submit_result_t::backpressured)
                return false;
            throw submit_error_t (
              static_cast<submit_result_t> (rc), zlink_errno ());
        }
        return true;
    }

    bool publish (const std::string &service_name_,
                  const std::string &topic_,
                  message_t &part_,
                  send_flags_t flags_ = send_flags_t::none)
    {
        validate_service_name (service_name_);
        zlink::detail::validate_no_embedded_null (topic_, "topic");
        const int rc = publish_impl (
          service_name_.c_str (), topic_.c_str (), part_, flags_);
        if (rc != 0)
        {
            if (flags_ == send_flags_t::dontwait
                && static_cast<submit_result_t> (rc)
                    == submit_result_t::backpressured)
                return false;
            throw submit_error_t (
              static_cast<submit_result_t> (rc), zlink_errno ());
        }
        return true;
    }

    bool send_channel (const std::string &channel_name_,
                       message_t &part_,
                       send_flags_t flags_ = send_flags_t::none)
    {
        validate_channel_name (channel_name_);
        if (flags_ == send_flags_t::dontwait) {
            send_result_t result = send_result_t::sent;
            if (send_channel_no_wait_result_impl (
                  result, channel_name_.c_str (), part_)
                != 0) {
                const int err = zlink_errno ();
                throw submit_error_t (
                  zlink::detail::submit_result_from_errno (err), err);
            }
            if (result == send_result_t::not_ready)
                throw submit_error_t (
                  submit_result_t::not_connected, zlink_errno ());
            return result == send_result_t::sent;
        }

        std::vector<message_t> parts;
        parts.push_back (std::move (part_));
        const bool submitted = send_channel (channel_name_, parts, flags_);
        if (!submitted && !parts.empty ())
            part_ = std::move (parts.front ());
        return submitted;
    }

    bool send_channel (const std::string &channel_name_,
                       std::vector<message_t> &parts_,
                       send_flags_t flags_ = send_flags_t::none)
    {
        validate_channel_name (channel_name_);
        const int rc = send_channel_impl (
          channel_name_.c_str (), parts_, flags_);
        if (rc != 0)
        {
            if (flags_ == send_flags_t::dontwait
                && static_cast<submit_result_t> (rc)
                    == submit_result_t::backpressured)
                return false;
            throw submit_error_t (
              static_cast<submit_result_t> (rc), zlink_errno ());
        }
        return true;
    }

    bool send_to_spot (const routing_id_t &dest_node_rid_,
                       const routing_id_t &dest_spot_rid_,
                       message_t message_,
                       send_flags_t flags_ = send_flags_t::none)
    {
        std::vector<message_t> parts;
        parts.push_back (std::move (message_));
        return send_to_spot (dest_node_rid_, dest_spot_rid_, parts, flags_);
    }

    bool send_to_spot (const routing_id_t &dest_node_rid_,
                       const routing_id_t &dest_spot_rid_,
                       std::vector<message_t> &parts_,
                       send_flags_t flags_ = send_flags_t::none)
    {
        std::vector<zlink_msg_t> native;
        if (detail::move_parts_to_native (parts_, native) != 0)
            throw last_error ();
        size_t failed_index = 0;
        const submit_result_t rc = static_cast<submit_result_t> (
          detail::submit_native_parts (
            native, failed_index,
            [&] (zlink_msg_t *part_out_, zlink_part_flag_t part_flag_, bool) {
                return zlink_spot_send_spot_part (
                  _spot, zlink::detail::routing_id_native (dest_node_rid_),
                  zlink::detail::routing_id_native (dest_spot_rid_), part_out_,
                  static_cast<zlink_send_flags_t> (flags_), part_flag_);
            }));
        if (rc != submit_result_t::ok) {
            detail::close_native_parts (native, failed_index);
            if (flags_ == send_flags_t::dontwait
                && rc == submit_result_t::backpressured)
                return false;
            throw submit_error_t (rc, zlink_errno ());
        }
        return true;
    }

  public:
    send_op_t send_to_spot (const routing_id_t &dest_node_rid_,
                            const routing_id_t &dest_spot_rid_)
    {
        detail::spot_op_state_t state;
        state.spot = this;
        state.kind = detail::spot_op_kind_t::send_to_spot;
        state.first_rid = dest_node_rid_;
        state.second_rid = dest_spot_rid_;
        return send_op_t (std::move (state));
    }

    request_op_t request_to_spot (const routing_id_t &dest_node_rid_,
                                  const routing_id_t &dest_spot_rid_)
    {
        detail::spot_op_state_t state;
        state.spot = this;
        state.kind = detail::spot_op_kind_t::request_to_spot;
        state.first_rid = dest_node_rid_;
        state.second_rid = dest_spot_rid_;
        return request_op_t (std::move (state));
    }

  private:
    async_result_t<std::vector<message_t>>
    request_to_spot (const routing_id_t &dest_node_rid_,
                     const routing_id_t &dest_spot_rid_,
                     message_t message_,
                     std::chrono::milliseconds timeout_ = {})
    {
        detail::request_state_t *state = detail::make_future_request_state ();
        std::future<std::vector<message_t>> future =
          state->promise->get_future ();
        std::vector<message_t> parts;
        parts.push_back (std::move (message_));
        std::vector<zlink_msg_t> native;
        if (detail::move_parts_to_native (parts, native) != 0) {
            delete state;
            throw last_error ();
        }
        size_t failed_index = 0;
        const submit_result_t rc = static_cast<submit_result_t> (
          detail::submit_native_parts (
            native, failed_index,
            [&] (zlink_msg_t *part_out_, zlink_part_flag_t part_flag_,
                 bool is_final_) {
                return zlink_spot_request_spot_part (
                  _spot, zlink::detail::routing_id_native (dest_node_rid_),
                  zlink::detail::routing_id_native (dest_spot_rid_), part_out_,
                  is_final_ ? &detail::request_callback_trampoline : NULL,
                  is_final_ ? state : NULL, ZLINK_SEND_FLAGS_NONE, part_flag_,
                  is_final_
                    ? static_cast<uint32_t> (
                        zlink::detail::resolve_timeout (
                          timeout_, _default_request_timeout).count ())
                    : 0u);
            }));
        if (rc != submit_result_t::ok) {
            detail::close_native_parts (native, failed_index);
            delete state;
            throw submit_error_t (rc, zlink_errno ());
        }
        return async_result_t<std::vector<message_t>> (
          std::move (future), detail::make_spot_request_progress (_spot));
    }

    bool request_to_spot (
      const routing_id_t &dest_node_rid_,
      const routing_id_t &dest_spot_rid_,
      message_t message_,
      std::function<void(request_result_t, std::vector<message_t>)> callback_,
      send_flags_t flags_ = send_flags_t::none,
      std::chrono::milliseconds timeout_ = {})
    {
        detail::request_state_t *state =
          detail::make_callback_request_state (std::move (callback_));
        std::vector<message_t> parts;
        parts.push_back (std::move (message_));
        std::vector<zlink_msg_t> native;
        if (detail::move_parts_to_native (parts, native) != 0) {
            delete state;
            throw last_error ();
        }
        size_t failed_index = 0;
        const submit_result_t rc = static_cast<submit_result_t> (
          detail::submit_native_parts (
            native, failed_index,
            [&] (zlink_msg_t *part_out_, zlink_part_flag_t part_flag_,
                 bool is_final_) {
                return zlink_spot_request_spot_part (
                  _spot, zlink::detail::routing_id_native (dest_node_rid_),
                  zlink::detail::routing_id_native (dest_spot_rid_), part_out_,
                  is_final_ ? &detail::request_callback_trampoline : NULL,
                  is_final_ ? state : NULL,
                  static_cast<zlink_send_flags_t> (flags_), part_flag_,
                  is_final_
                    ? static_cast<uint32_t> (
                        zlink::detail::resolve_timeout (
                          timeout_, _default_request_timeout).count ())
                    : 0u);
            }));
        if (rc != submit_result_t::ok) {
            detail::close_native_parts (native, failed_index);
            delete state;
            if (flags_ == send_flags_t::dontwait
                && rc == submit_result_t::backpressured)
                return false;
            throw submit_error_t (rc, zlink_errno ());
        }
        return true;
    }

  public:
    request_op_t request_to_router (const routing_id_t &peer_rid_)
    {
        detail::spot_op_state_t state;
        state.spot = this;
        state.kind = detail::spot_op_kind_t::request_to_router;
        state.first_rid = peer_rid_;
        return request_op_t (std::move (state));
    }

  private:
    async_result_t<std::vector<message_t>>
    request_to_router (const routing_id_t &peer_rid_,
                       message_t message_,
                       std::chrono::milliseconds timeout_ = {})
    {
        detail::request_state_t *state = detail::make_future_request_state ();
        std::future<std::vector<message_t>> future =
          state->promise->get_future ();
        std::vector<message_t> parts;
        parts.push_back (std::move (message_));
        std::vector<zlink_msg_t> native;
        if (detail::move_parts_to_native (parts, native) != 0) {
            delete state;
            throw last_error ();
        }
        size_t failed_index = 0;
        const submit_result_t rc = static_cast<submit_result_t> (
          detail::submit_native_parts (
            native, failed_index,
            [&] (zlink_msg_t *part_out_, zlink_part_flag_t part_flag_,
                 bool is_final_) {
                return zlink_spot_request_router_part (
                  _spot, zlink::detail::routing_id_native (peer_rid_), part_out_,
                  is_final_ ? &detail::request_callback_trampoline : NULL,
                  is_final_ ? state : NULL, ZLINK_SEND_FLAGS_NONE, part_flag_,
                  is_final_
                    ? static_cast<uint32_t> (
                        zlink::detail::resolve_timeout (
                          timeout_, _default_request_timeout).count ())
                    : 0u);
            }));
        if (rc != submit_result_t::ok) {
            detail::close_native_parts (native, failed_index);
            delete state;
            throw submit_error_t (rc, zlink_errno ());
        }
        return async_result_t<std::vector<message_t>> (
          std::move (future), detail::make_spot_request_progress (_spot));
    }

    bool request_to_router (
      const routing_id_t &peer_rid_,
      message_t message_,
      std::function<void(request_result_t, std::vector<message_t>)> callback_,
      send_flags_t flags_ = send_flags_t::none,
      std::chrono::milliseconds timeout_ = {})
    {
        detail::request_state_t *state =
          detail::make_callback_request_state (std::move (callback_));
        std::vector<message_t> parts;
        parts.push_back (std::move (message_));
        std::vector<zlink_msg_t> native;
        if (detail::move_parts_to_native (parts, native) != 0) {
            delete state;
            throw last_error ();
        }
        size_t failed_index = 0;
        const submit_result_t rc = static_cast<submit_result_t> (
          detail::submit_native_parts (
            native, failed_index,
            [&] (zlink_msg_t *part_out_, zlink_part_flag_t part_flag_,
                 bool is_final_) {
                return zlink_spot_request_router_part (
                  _spot, zlink::detail::routing_id_native (peer_rid_), part_out_,
                  is_final_ ? &detail::request_callback_trampoline : NULL,
                  is_final_ ? state : NULL,
                  static_cast<zlink_send_flags_t> (flags_), part_flag_,
                  is_final_
                    ? static_cast<uint32_t> (
                        zlink::detail::resolve_timeout (
                          timeout_, _default_request_timeout).count ())
                    : 0u);
            }));
        if (rc != submit_result_t::ok) {
            detail::close_native_parts (native, failed_index);
            delete state;
            if (flags_ == send_flags_t::dontwait
                && rc == submit_result_t::backpressured)
                return false;
            throw submit_error_t (rc, zlink_errno ());
        }
        return true;
    }

    async_result_t<std::vector<message_t>>
    request_channel (const std::string &channel_name_,
                     message_t &part_,
                     std::chrono::milliseconds timeout_ = {})
    {
        std::vector<message_t> parts;
        parts.push_back (std::move (part_));
        return request_channel (channel_name_, parts, timeout_);
    }

    async_result_t<std::vector<message_t>>
    request_channel (const std::string &channel_name_,
                     std::vector<message_t> &parts_,
                     std::chrono::milliseconds timeout_ = {})
    {
        validate_channel_name (channel_name_);
        detail::request_state_t *state = detail::make_future_request_state ();
        std::future<std::vector<message_t>> future =
          state->promise->get_future ();
        std::vector<zlink_msg_t> native;
        if (detail::move_parts_to_native (parts_, native) != 0) {
            delete state;
            throw last_error ();
        }
        size_t failed_index = 0;
        const submit_result_t rc = static_cast<submit_result_t> (
          detail::submit_native_parts (
            native, failed_index,
            [&] (zlink_msg_t *part_out_, zlink_part_flag_t part_flag_, bool is_final_) {
                return zlink_spot_request_channel_part (
                  _spot, channel_name_.c_str (), part_out_,
                  is_final_ ? &detail::request_callback_trampoline : NULL,
                  is_final_ ? state : NULL, ZLINK_SEND_FLAGS_NONE, part_flag_,
                  is_final_
                    ? static_cast<uint32_t> (
                        zlink::detail::resolve_timeout (
                          timeout_, _default_request_timeout).count ())
                    : 0u);
            }));
        if (rc != submit_result_t::ok) {
            detail::close_native_parts (native, failed_index);
            delete state;
            throw submit_error_t (rc, zlink_errno ());
        }
        return async_result_t<std::vector<message_t>> (
          std::move (future),
          detail::make_spot_request_progress (_spot, channel_name_));
    }

    bool request_channel (
      const std::string &channel_name_,
      message_t &part_,
      std::function<void(request_result_t, std::vector<message_t>)> callback_,
      send_flags_t flags_ = send_flags_t::none,
      std::chrono::milliseconds timeout_ = {})
    {
        std::vector<message_t> parts;
        parts.push_back (std::move (part_));
        const bool submitted = request_channel (
          channel_name_, parts, std::move (callback_), flags_, timeout_);
        if (!submitted && !parts.empty ())
            part_ = std::move (parts.front ());
        return submitted;
    }

    bool request_channel (
      const std::string &channel_name_,
      std::vector<message_t> &parts_,
      std::function<void(request_result_t, std::vector<message_t>)> callback_,
      send_flags_t flags_ = send_flags_t::none,
      std::chrono::milliseconds timeout_ = {})
    {
        validate_channel_name (channel_name_);
        detail::request_state_t *state =
          detail::make_callback_request_state (std::move (callback_));
        std::vector<zlink_msg_t> native;
        if (detail::move_parts_to_native (parts_, native) != 0) {
            delete state;
            throw last_error ();
        }
        size_t failed_index = 0;
        const submit_result_t rc = static_cast<submit_result_t> (
          detail::submit_native_parts (
            native, failed_index,
            [&] (zlink_msg_t *part_out_, zlink_part_flag_t part_flag_, bool is_final_) {
                return zlink_spot_request_channel_part (
                  _spot, channel_name_.c_str (), part_out_,
                  is_final_ ? &detail::request_callback_trampoline : NULL,
                  is_final_ ? state : NULL,
                  static_cast<zlink_send_flags_t> (flags_), part_flag_,
                  is_final_
                    ? static_cast<uint32_t> (
                        zlink::detail::resolve_timeout (
                          timeout_, _default_request_timeout).count ())
                    : 0u);
            }));
        if (rc != submit_result_t::ok) {
            detail::close_native_parts (native, failed_index);
            delete state;
            if (flags_ == send_flags_t::dontwait
                && rc == submit_result_t::backpressured)
                return false;
            throw submit_error_t (rc, zlink_errno ());
        }
        return true;
    }

  public:
    std::optional<topic_message_t> subscribe (recv_flags_t flags_ = recv_flags_t::none)
    {
        std::vector<message_t> parts;
        std::string service_name;
        std::string topic;
        routing_id_t source_rid = zlink::detail::unchecked_empty_routing_id ();
        const recv_result_t rc = static_cast<recv_result_t> (subscribe_impl (
          parts, service_name, topic, flags_, &source_rid));
        if (rc == recv_result_t::no_data && flags_ == recv_flags_t::dontwait)
            return std::nullopt;
        if (rc != recv_result_t::ok)
            throw recv_error_t (rc, zlink_errno ());
        return std::optional<topic_message_t> (topic_message_t (
          zlink::detail::routing_id_empty (source_rid) ? std::nullopt
                              : std::optional<routing_id_t> (source_rid),
          service_name.empty () ? std::nullopt
                                : std::optional<std::string> (service_name),
          std::move (topic), std::move (parts)));
    }

    std::optional<subscription_event_t>
    receive_subscription_event (recv_flags_t flags_ = recv_flags_t::none)
    {
        subscription_event_t event;
        std::string service_name;
        std::string topic;
        routing_id_t source_rid = zlink::detail::unchecked_empty_routing_id ();
        bool subscribed = false;
        const recv_result_t rc = static_cast<recv_result_t> (
          subscription_event_impl (
            source_rid, subscribed, service_name, topic, flags_));
        if (rc == recv_result_t::no_data && flags_ == recv_flags_t::dontwait)
            return std::nullopt;
        if (rc != recv_result_t::ok)
            throw recv_error_t (rc, zlink_errno ());
        if (!zlink::detail::routing_id_empty (source_rid))
            event.routing_id = source_rid;
        event.service_name =
          service_name.empty () ? std::nullopt
                                : std::optional<std::string> (service_name);
        event.topic = std::move (topic);
        event.subscribed = subscribed;
        return std::optional<subscription_event_t> (std::move (event));
    }

    void set_subscription (const std::string &filter_)
    {
        zlink::detail::validate_no_embedded_null (filter_, "filter");
        detail::throw_if_failed<config_error_t> (
          static_cast<config_result_t> (
            zlink_set_subscription (_spot, filter_.c_str ())));
    }

    void unset_subscription (const std::string &filter_)
    {
        zlink::detail::validate_no_embedded_null (filter_, "filter");
        detail::throw_if_failed<config_error_t> (
          static_cast<config_result_t> (
            zlink_unset_subscription (_spot, filter_.c_str ())));
    }

    void subscription_at (size_t index_,
                          std::string &filter_out_,
                          bool *is_pattern_out_ = NULL) const
    {
        size_t capacity = 256u;
        const size_t max_capacity = 64u * 1024u;

        while (capacity <= max_capacity) {
            std::vector<char> buffer (capacity);
            size_t length = capacity;
            int is_pattern = 0;
            const config_result_t rc = static_cast<config_result_t> (
              zlink_subscription_at (
                _spot, index_, buffer.data (), &length, &is_pattern));
            if (rc == config_result_t::ok) {
                const size_t bounded = length <= buffer.size () ? length
                                                                : buffer.size ();
                size_t out_size = bounded;
                if (out_size > 0 && buffer[out_size - 1] == '\0')
                    --out_size;
                filter_out_.assign (buffer.data (), out_size);
                if (is_pattern_out_)
                    *is_pattern_out_ = is_pattern != 0;
                return;
            }

            if (errno != EINVAL || capacity == max_capacity)
                throw config_error_t (rc, zlink_errno ());

            capacity *= 2u;
            if (capacity > max_capacity)
                capacity = max_capacity;
        }

        throw config_error_t (config_result_t::invalid_argument, EINVAL);
    }

    subscription_filter_t subscription_at (size_t index_) const
    {
        subscription_filter_t filter;
        subscription_at (index_, filter.filter, &filter.is_pattern);
        return filter;
    }

    void on_send_ready (std::function<void()> handler_)
    {
        _send_ready_handler = std::move (handler_);
        const handler_result_t rc = static_cast<handler_result_t> (
          zlink_send_ready_handler (
            _spot, &spot_t::send_ready_trampoline, this));
        if (rc != handler_result_t::ok)
            throw handler_error_t (rc, zlink_errno ());
    }

  private:
    static void validate_channel_name (const std::string &channel_name_)
    {
        zlink::detail::validate_bounded_c_string (channel_name_, 255u, "channel_name");
        if (channel_name_.empty ()) {
            errno = EINVAL;
            throw submit_error_t (submit_result_t::invalid_argument, EINVAL);
        }
    }

    static void validate_service_name (const std::string &service_name_)
    {
        validate_channel_name (service_name_);
    }

    ZLINK_CPP_NODISCARD int
    publish_impl (const char *service_name_,
                  const char *topic_,
                  std::vector<message_t> &parts_,
                  send_flags_t flags_)
    {
        if (!_spot) {
            errno = _last_error != 0 ? _last_error : EFAULT;
            return -1;
        }

        if (parts_.empty ()) {
            errno = EINVAL;
            return -1;
        }

        std::vector<zlink_msg_t> native;
        if (detail::move_parts_to_native (parts_, native) != 0)
            return -1;

        size_t failed_index = 0;
        const int rc = detail::submit_native_parts (
          native, failed_index,
          [&] (zlink_msg_t *part_out_, zlink_part_flag_t part_flag_, bool) {
              return zlink_spot_publish_part (
                _spot, service_name_, topic_, part_out_,
                static_cast<zlink_send_flags_t> (flags_), part_flag_);
          });
        if (rc != 0) {
            detail::restore_parts_from_native (parts_, native, failed_index);
        }
        return rc;
    }

    ZLINK_CPP_NODISCARD int
    publish_impl (const char *service_name_,
                  const char *topic_,
                  message_t &part_,
                  send_flags_t flags_)
    {
        if (!_spot) {
            errno = _last_error != 0 ? _last_error : EFAULT;
            return -1;
        }

        if (!part_.valid ()) {
            errno = EINVAL;
            return -1;
        }

        zlink_msg_t native;
        zlink::detail::move_to_native (part_, &native);
        if (part_.valid ())
            return -1;

        const int rc = zlink_spot_publish_part (
          _spot, service_name_, topic_, &native,
          static_cast<zlink_send_flags_t> (flags_), ZLINK_PART_FINAL);
        if (rc != 0) {
            const int err = errno;
            part_.init ();
            if (part_.valid ())
                (void) zlink_msg_move (zlink::detail::native_handle (part_), &native);
            (void) zlink_msg_close (&native);
            errno = err;
        }
        return rc;
    }

    ZLINK_CPP_NODISCARD int
    send_channel_impl (const char *channel_name_,
                       std::vector<message_t> &parts_,
                       send_flags_t flags_)
    {
        if (!_spot) {
            errno = _last_error != 0 ? _last_error : EFAULT;
            return -1;
        }

        if (parts_.empty ()) {
            errno = EINVAL;
            return -1;
        }

        std::vector<zlink_msg_t> native;
        if (detail::move_parts_to_native (parts_, native) != 0)
            return -1;

        size_t failed_index = 0;
        const int rc = detail::submit_native_parts (
          native, failed_index,
          [&] (zlink_msg_t *part_out_, zlink_part_flag_t part_flag_, bool) {
              return zlink_spot_send_channel_part (
                _spot, channel_name_, part_out_,
                static_cast<zlink_send_flags_t> (flags_), part_flag_);
          });
        if (rc != 0) {
            detail::restore_parts_from_native (parts_, native, failed_index);
        }
        return rc;
    }

    ZLINK_CPP_NODISCARD int
    send_channel_no_wait_result_impl (send_result_t &result_out_,
                                      const char *channel_name_,
                                      message_t &part_)
    {
        if (!_spot) {
            errno = _last_error != 0 ? _last_error : EFAULT;
            return -1;
        }

        if (!part_.valid ()) {
            errno = EINVAL;
            return -1;
        }

        zlink_msg_t native;
        zlink::detail::move_to_native (part_, &native);
        if (part_.valid ())
            return -1;

        const int rc = zlink_spot_send_channel_part (
          _spot, channel_name_, &native, ZLINK_DONTWAIT, ZLINK_PART_FINAL);
        if (rc == 0) {
            result_out_ = send_result_t::sent;
            return 0;
        }

        const int err = errno;
        if (detail::classify_nonblocking_send_errno (err, result_out_)) {
            if (result_out_ != send_result_t::sent) {
                part_.init ();
                if (part_.valid ())
                    (void) zlink_msg_move (zlink::detail::native_handle (part_), &native);
                (void) zlink_msg_close (&native);
            }
            errno = err;
            return 0;
        }

        part_.init ();
        if (part_.valid ())
            (void) zlink_msg_move (zlink::detail::native_handle (part_), &native);
        (void) zlink_msg_close (&native);
        errno = err;
        return -1;
    }

    ZLINK_CPP_NODISCARD int
    subscribe_impl (std::vector<message_t> &parts_,
                    std::string &service_name_,
                    std::string &topic_,
                    recv_flags_t flags_ = recv_flags_t::none,
                    routing_id_t *source_rid_out_ = NULL)
    {
        if (!_spot) {
            errno = _last_error != 0 ? _last_error : EFAULT;
            return -1;
        }

        char service_name_buffer[256];
        char topic_buffer[256];
        size_t service_name_length = sizeof (service_name_buffer);
        size_t topic_length = sizeof (topic_buffer);
        zlink_routing_id_t source_rid;
        std::memset (&source_rid, 0, sizeof (source_rid));
        zlink_msg_t *parts_native = NULL;
        size_t part_count = 0;

        const int rc = spot_subscribe_impl (
          _spot, &source_rid, &parts_native, &part_count, service_name_buffer,
          &service_name_length, topic_buffer, &topic_length,
          static_cast<zlink_recv_flags_t> (flags_));
        if (rc != ZLINK_RECV_OK)
            return rc;
        if (detail::assign_parts_from_native (parts_native, part_count, parts_) != 0)
            return -1;

        if (source_rid_out_) {
            if (source_rid.size > 0)
                *source_rid_out_ = zlink::detail::native_routing_id (source_rid);
            else
                *source_rid_out_ = zlink::detail::unchecked_empty_routing_id ();
        }

        const size_t service_name_size =
          service_name_length < sizeof (service_name_buffer)
            ? service_name_length
            : sizeof (service_name_buffer) - 1u;
        const size_t topic_size =
          topic_length < sizeof (topic_buffer) ? topic_length
                                               : sizeof (topic_buffer) - 1u;
        service_name_.assign (service_name_buffer, service_name_size);
        topic_.assign (topic_buffer, topic_size);
        return 0;
    }

    ZLINK_CPP_NODISCARD int
    subscription_event_impl (routing_id_t &source_rid_out_,
                             bool &subscribed_out_,
                             std::string &service_name_,
                             std::string &topic_,
                             recv_flags_t flags_ = recv_flags_t::none)
    {
        if (!_spot) {
            errno = _last_error != 0 ? _last_error : EFAULT;
            return -1;
        }

        char service_name_buffer[256];
        char topic_buffer[256];
        size_t service_name_length = 0;
        size_t topic_length = 0;
        int subscribed = 0;
        const zlink_routing_id_t *source_rid = NULL;
        const int rc = zlink_spot_subscription_event_part (
          _spot, &source_rid, &subscribed, service_name_buffer,
          sizeof (service_name_buffer), &service_name_length, topic_buffer,
          sizeof (topic_buffer), &topic_length,
          static_cast<zlink_recv_flags_t> (flags_));
        if (rc != 0)
            return rc;

        if (source_rid && source_rid->size > 0)
            source_rid_out_ = zlink::detail::native_routing_id (*source_rid);
        else
            source_rid_out_ = zlink::detail::unchecked_empty_routing_id ();

        const size_t service_name_size =
          service_name_length < sizeof (service_name_buffer)
            ? service_name_length
            : sizeof (service_name_buffer) - 1u;
        const size_t topic_size =
          topic_length < sizeof (topic_buffer) ? topic_length
                                               : sizeof (topic_buffer) - 1u;
        service_name_.assign (service_name_buffer, service_name_size);
        topic_.assign (topic_buffer, topic_size);
        subscribed_out_ = subscribed != 0;
        return 0;
    }

    ZLINK_CPP_NODISCARD int
    publish_no_wait_result_impl (send_result_t &result_out_,
                      const char *service_name_,
                      const char *topic_,
                      std::vector<message_t> &parts_)
    {
        if (!_spot) {
            errno = _last_error != 0 ? _last_error : EFAULT;
            return -1;
        }

        if (parts_.empty ()) {
            errno = EINVAL;
            return -1;
        }

        std::vector<zlink_msg_t> native;
        if (detail::move_parts_to_native (parts_, native) != 0)
            return -1;

        size_t failed_index = 0;
        const int rc = detail::submit_native_parts (
          native, failed_index,
          [&] (zlink_msg_t *part_out_, zlink_part_flag_t part_flag_, bool) {
              return zlink_spot_publish_part (
                _spot, service_name_, topic_, part_out_, ZLINK_DONTWAIT,
                part_flag_);
          });
        if (rc == 0) {
            result_out_ = send_result_t::sent;
            return 0;
        }

        const int err = errno;
        if (detail::classify_nonblocking_send_errno (err, result_out_)) {
            if (result_out_ != send_result_t::sent)
                detail::restore_parts_from_native (parts_, native, failed_index);
            return 0;
        }

        detail::restore_parts_from_native (parts_, native, failed_index);
        errno = err;
        return -1;
    }

    ZLINK_CPP_NODISCARD int
    publish_no_wait_result_impl (send_result_t &result_out_,
                      const char *service_name_,
                      const char *topic_,
                      message_t &part_)
    {
        if (!_spot) {
            errno = _last_error != 0 ? _last_error : EFAULT;
            return -1;
        }

        if (!part_.valid ()) {
            errno = EINVAL;
            return -1;
        }

        zlink_msg_t native;
        zlink::detail::move_to_native (part_, &native);
        if (part_.valid ())
            return -1;

        const int rc = zlink_spot_publish_part (
          _spot, service_name_, topic_, &native, ZLINK_DONTWAIT,
          ZLINK_PART_FINAL);
        if (rc == 0) {
            result_out_ = send_result_t::sent;
            return 0;
        }

        const int err = errno;
        if (detail::classify_nonblocking_send_errno (err, result_out_)) {
            if (result_out_ != send_result_t::sent) {
                part_.init ();
                if (part_.valid ())
                    (void) zlink_msg_move (zlink::detail::native_handle (part_), &native);
                (void) zlink_msg_close (&native);
            }
            return 0;
        }

        part_.init ();
        if (part_.valid ())
            (void) zlink_msg_move (zlink::detail::native_handle (part_), &native);
        (void) zlink_msg_close (&native);
        errno = err;
        return -1;
    }

  private:
    explicit spot_t (spot_node_t &node_)
        : _spot (zlink_spot_new (zlink::detail::native_handle (node_))),
          _last_error (0),
          _default_request_timeout (std::chrono::milliseconds ())
    {
        if (!_spot)
            _last_error = errno != 0 ? errno : EFAULT;
    }

    explicit spot_t (void *handle_) noexcept
        : _spot (handle_), _last_error (handle_ ? 0 : (errno != 0 ? errno : EFAULT)),
          _default_request_timeout (std::chrono::milliseconds ())
    {
    }

    friend class spot_node_t;
    friend class send_ready_op_t;
    friend class request_ready_op_t;
    friend class request_callback_ready_op_t;
    friend class reply_ready_op_t;
    friend void *zlink::detail::native_handle (spot_t &spot_) noexcept;
    friend const void *
    zlink::detail::native_handle (const spot_t &spot_) noexcept;

  public:
    static void send_ready_trampoline (void *, void *userdata_)
    {
        spot_t *self = static_cast<spot_t *> (userdata_);
        if (self && self->_send_ready_handler)
            self->_send_ready_handler ();
    }

    static void routed_receive_trampoline (
      const zlink_routing_id_t *source_rid_,
      const zlink_routing_id_t *spot_rid_,
      uint64_t request_seq_,
      zlink_msg_t *parts_,
      size_t part_count_,
      void *userdata_)
    {
        spot_t *self = static_cast<spot_t *> (userdata_);
        if (!self || !self->_routed_receive_handler)
            return;
        self->_routed_receive_handler (
          zlink::detail::make_received (
            source_rid_, spot_rid_, request_seq_, request_seq_ != 0u,
            parts_, part_count_));
    }

    static void dispatch_event_trampoline (
      void *,
      const zlink_spot_dispatch_info_t *info_,
      void *userdata_)
    {
        spot_t *self = static_cast<spot_t *> (userdata_);
        if (!self || !self->_dispatch_event_handler || !info_)
            return;
        const spot_dispatch_info_t info (*info_);
        self->_dispatch_event_handler (*self, info);
    }

    void set_routing_id (const routing_id_t &routing_id_)
    {
        detail::throw_if_failed<config_error_t> (
          static_cast<config_result_t> (zlink_set_routing_id (
            _spot, routing_id_.data (), routing_id_.size ())));
    }

    void get_routing_id (routing_id_t &out_) const
    {
        zlink_routing_id_t native;
        std::memset (&native, 0, sizeof (native));
        detail::throw_if_failed<config_error_t> (
          static_cast<config_result_t> (zlink_get_routing_id (_spot, &native)));
        out_ = zlink::detail::native_routing_id (native);
    }

    routing_id_t routing_id () const
    {
        routing_id_t value = zlink::detail::unchecked_empty_routing_id ();
        get_routing_id (value);
        return value;
    }

  private:
    void reply_to_spot (const routing_id_t &dest_node_rid_,
                        const routing_id_t &dest_spot_rid_,
                        uint64_t request_seq_,
                        message_t message_,
                        send_flags_t flags_ = send_flags_t::none)
    {
        zlink::detail::throw_if_reply_flags_unsupported (flags_);
        std::vector<message_t> parts;
        parts.push_back (std::move (message_));
        std::vector<zlink_msg_t> native;
        if (detail::move_parts_to_native (parts, native) != 0)
            throw last_error ();
        size_t failed_index = 0;
        const submit_result_t rc = static_cast<submit_result_t> (
          detail::submit_native_parts (
            native, failed_index,
            [&] (zlink_msg_t *part_out_, zlink_part_flag_t part_flag_, bool) {
                return zlink_spot_reply_spot_part (
                  _spot, zlink::detail::routing_id_native (dest_node_rid_),
                  zlink::detail::routing_id_native (dest_spot_rid_), request_seq_, part_out_,
                  part_flag_);
            }));
        if (rc != submit_result_t::ok) {
            detail::close_native_parts (native, failed_index);
            throw submit_error_t (rc, zlink_errno ());
        }
    }

  public:
    reply_op_t reply_to_spot (const routing_id_t &dest_node_rid_,
                              const routing_id_t &dest_spot_rid_,
                              uint64_t request_seq_)
    {
        detail::spot_op_state_t state;
        state.spot = this;
        state.kind = detail::spot_op_kind_t::reply_to_spot;
        state.first_rid = dest_node_rid_;
        state.second_rid = dest_spot_rid_;
        state.request_seq = request_seq_;
        return reply_op_t (std::move (state));
    }

  private:
    void reply_to_router (const routing_id_t &peer_rid_,
                          uint64_t request_seq_,
                          message_t message_,
                          send_flags_t flags_ = send_flags_t::none)
    {
        zlink::detail::throw_if_reply_flags_unsupported (flags_);
        std::vector<message_t> parts;
        parts.push_back (std::move (message_));
        std::vector<zlink_msg_t> native;
        if (detail::move_parts_to_native (parts, native) != 0)
            throw last_error ();
        size_t failed_index = 0;
        const submit_result_t rc = static_cast<submit_result_t> (
          detail::submit_native_parts (
            native, failed_index,
            [&] (zlink_msg_t *part_out_, zlink_part_flag_t part_flag_, bool) {
                return zlink_spot_reply_router_part (
                  _spot, zlink::detail::routing_id_native (peer_rid_), request_seq_, part_out_,
                  part_flag_);
            }));
        if (rc != submit_result_t::ok) {
            detail::close_native_parts (native, failed_index);
            throw submit_error_t (rc, zlink_errno ());
        }
    }

  public:
    reply_op_t reply_to_router (const routing_id_t &peer_rid_,
                                uint64_t request_seq_)
    {
        detail::spot_op_state_t state;
        state.spot = this;
        state.kind = detail::spot_op_kind_t::reply_to_router;
        state.first_rid = peer_rid_;
        state.request_seq = request_seq_;
        return reply_op_t (std::move (state));
    }

    std::optional<received_t> recv_routed (recv_flags_t flags_ = recv_flags_t::none)
    {
        const zlink_routing_id_t *source_node_rid = NULL;
        const zlink_routing_id_t *source_spot_rid = NULL;
        uint64_t request_seq = 0;
        zlink_msg_t *parts_native = NULL;
        size_t part_count = 0;
        const recv_result_t rc = static_cast<recv_result_t> (
          spot_recv_impl (_spot, &source_node_rid, &source_spot_rid,
                          &request_seq, &parts_native, &part_count,
                          static_cast<zlink_recv_flags_t> (flags_)));
        if (rc == recv_result_t::no_data && flags_ == recv_flags_t::dontwait)
            return std::nullopt;
        if (rc != recv_result_t::ok)
            throw recv_error_t (rc, zlink_errno ());
        std::vector<message_t> parts;
        if (detail::assign_parts_from_native (parts_native, part_count, parts) != 0)
            throw last_error ();

        std::function<void(std::vector<message_t> &, send_flags_t)> reply_fn;
        if (request_seq != 0u) {
            const std::optional<routing_id_t> node_rid =
              (source_node_rid && source_node_rid->size > 0)
                ? std::optional<routing_id_t> (zlink::detail::native_routing_id (*source_node_rid))
                : std::nullopt;
            const std::optional<routing_id_t> spot_rid =
              (source_spot_rid && source_spot_rid->size > 0)
                ? std::optional<routing_id_t> (zlink::detail::native_routing_id (*source_spot_rid))
                : std::nullopt;
            reply_fn = [this, node_rid, spot_rid, request_seq] (
                         std::vector<message_t> &reply_parts_,
                         send_flags_t flags__) {
                if (!node_rid || !spot_rid)
                    throw submit_error_t (
                      submit_result_t::invalid_argument, EINVAL);
                zlink::detail::throw_if_reply_flags_unsupported (flags__);
                std::vector<zlink_msg_t> native_reply;
                if (detail::move_parts_to_native (reply_parts_, native_reply) != 0)
                    throw last_error ();
                size_t failed_index = 0;
                const submit_result_t reply_rc = static_cast<submit_result_t> (
                  detail::submit_native_parts (
                    native_reply, failed_index,
                    [&] (zlink_msg_t *part_out_,
                         zlink_part_flag_t part_flag_,
                         bool) {
                        return zlink_spot_reply_spot_part (
                          _spot, zlink::detail::routing_id_native (*node_rid),
                          zlink::detail::routing_id_native (*spot_rid), request_seq,
                          part_out_, part_flag_);
                    }));
                if (reply_rc != submit_result_t::ok) {
                    detail::restore_parts_from_native (
                      reply_parts_, native_reply, failed_index);
                    throw submit_error_t (reply_rc, zlink_errno ());
                }
            };
        }

        return std::optional<received_t> (received_t (
          (source_node_rid && source_node_rid->size > 0)
            ? std::optional<routing_id_t> (zlink::detail::native_routing_id (*source_node_rid))
            : std::nullopt,
          (source_spot_rid && source_spot_rid->size > 0)
            ? std::optional<routing_id_t> (zlink::detail::native_routing_id (*source_spot_rid))
            : std::nullopt,
          request_seq != 0u ? std::optional<uint64_t> (request_seq)
                            : std::nullopt,
          std::move (parts), std::move (reply_fn)));
    }

    void on_routed_receive (std::function<void(received_t)> handler_)
    {
        _routed_receive_handler = std::move (handler_);
        const handler_result_t rc = static_cast<handler_result_t> (
          zlink_spot_handler (
            _spot, &spot_t::routed_receive_trampoline, this));
        if (rc != handler_result_t::ok)
            throw handler_error_t (rc, zlink_errno ());
    }

    void on_dispatch_event (
      std::function<void(spot_t &, const spot_dispatch_info_t &)> handler_)
    {
        _dispatch_event_handler = std::move (handler_);
        const handler_result_t rc = static_cast<handler_result_t> (
          zlink_spot_dispatch_event_handler (
            _spot, &spot_t::dispatch_event_trampoline, this));
        if (rc != handler_result_t::ok)
            throw handler_error_t (rc, zlink_errno ());
    }

    void on_dispatch_event (
      std::function<void(const spot_dispatch_info_t &)> handler_)
    {
        on_dispatch_event (
          [handler = std::move (handler_)] (
            spot_t &, const spot_dispatch_info_t &info_) mutable {
              handler (info_);
          });
    }

    std::optional<actor_join_request_t>
    recv_actor_join (recv_flags_t flags_ = recv_flags_t::none)
    {
        zlink_actor_join_info_t native_info;
        std::memset (&native_info, 0, sizeof (native_info));
        zlink_msg_t native_message;
        std::memset (&native_message, 0, sizeof (native_message));
        const recv_result_t rc = static_cast<recv_result_t> (
          zlink_spot_actor_join_recv (
            _spot, &native_info, &native_message,
            static_cast<zlink_recv_flags_t> (flags_)));
        if (rc == recv_result_t::no_data && flags_ == recv_flags_t::dontwait)
            return std::nullopt;
        if (rc != recv_result_t::ok)
            throw recv_error_t (rc, zlink_errno ());
        message_t message;
        if (zlink_msg_move (zlink::detail::native_handle (message), &native_message) != 0) {
            (void) zlink_msg_close (&native_message);
            throw last_error ();
        }
        return std::optional<actor_join_request_t> (
          actor_join_request_t (actor_join_info_t (native_info),
                                std::move (message)));
    }

    void reply_actor_join (const actor_join_request_t &request_,
                           bool accepted_,
                           message_t &message_)
    {
        zlink_actor_join_info_t native_info = request_.info ().native ();
        zlink_msg_t native_message;
        zlink::detail::move_to_native (message_, &native_message);
        const submit_result_t rc = static_cast<submit_result_t> (
          zlink_spot_actor_join_reply (
            _spot, &native_info, accepted_ ? 1u : 0u, &native_message));
        if (rc != submit_result_t::ok) {
            message_.init ();
            (void) zlink_msg_move (zlink::detail::native_handle (message_), &native_message);
            throw submit_error_t (rc, zlink_errno ());
        }
    }

    void reply_actor_join (const actor_join_info_t &info_,
                           bool accepted_,
                           message_t &message_)
    {
        actor_join_request_t request (info_, message_t ());
        reply_actor_join (request, accepted_, message_);
    }

    std::vector<actor_ref_t> actors_snapshot () const
    {
        size_t count = 0;
        detail::throw_if_failed<config_error_t> (
          static_cast<config_result_t> (
            zlink_spot_actors_snapshot (_spot, NULL, &count)));
        std::vector<zlink_actor_ref_t> native (count);
        if (count > 0) {
            detail::throw_if_failed<config_error_t> (
              static_cast<config_result_t> (
                zlink_spot_actors_snapshot (_spot, native.data (), &count)));
            native.resize (count);
        }
        std::vector<actor_ref_t> entries;
        entries.reserve (native.size ());
        for (size_t i = 0; i < native.size (); ++i)
            entries.push_back (actor_ref_t (native[i]));
        return entries;
    }

    void close ()
    {
        if (!_spot)
            return;

        void *tmp = _spot;
        detail::throw_if_failed<close_error_t> (
          static_cast<close_result_t> (zlink_spot_destroy (&tmp)));
        _spot = NULL;
    }

  private:
    void *_spot;
    int _last_error;
    std::chrono::milliseconds _default_request_timeout;
    std::function<void()> _send_ready_handler;
    std::function<void(received_t)> _routed_receive_handler;
    std::function<void(spot_t &, const spot_dispatch_info_t &)>
      _dispatch_event_handler;
};

inline bool send_ready_op_t::submit () &&
{
    if (!_state.spot || _state.parts.empty ())
        throw submit_error_t (submit_result_t::invalid_argument, EINVAL);

    switch (_state.kind) {
    case detail::spot_op_kind_t::publish:
        return _state.parts.size () == 1u
          ? _state.spot->publish (
              _state.service_name, _state.topic, _state.parts.front (),
              _state.flags)
          : _state.spot->publish (
              _state.service_name, _state.topic, _state.parts, _state.flags);
    case detail::spot_op_kind_t::send_channel:
        return _state.parts.size () == 1u
          ? _state.spot->send_channel (
              _state.channel_name, _state.parts.front (), _state.flags)
          : _state.spot->send_channel (
              _state.channel_name, _state.parts, _state.flags);
    case detail::spot_op_kind_t::send_to_spot:
        if (!_state.first_rid || !_state.second_rid)
            throw submit_error_t (submit_result_t::invalid_argument, EINVAL);
        return _state.parts.size () == 1u
          ? _state.spot->send_to_spot (
              *_state.first_rid, *_state.second_rid,
              std::move (_state.parts.front ()), _state.flags)
          : _state.spot->send_to_spot (
              *_state.first_rid, *_state.second_rid, _state.parts,
              _state.flags);
    default:
        throw submit_error_t (submit_result_t::invalid_argument, EINVAL);
    }
}

inline request_callback_ready_op_t request_ready_op_t::flags (int flags_) &&
{
    _state.flags = send_flags_t (flags_);
    return request_callback_ready_op_t (std::move (_state));
}

inline async_result_t<std::vector<message_t>>
request_ready_op_t::submit_async () &&
{
    if (!_state.spot || _state.parts.empty ())
        throw submit_error_t (submit_result_t::invalid_argument, EINVAL);

    switch (_state.kind) {
    case detail::spot_op_kind_t::request_channel:
        return _state.spot->request_channel (
          _state.channel_name, _state.parts, _state.timeout);
    case detail::spot_op_kind_t::request_to_spot:
        if (!_state.first_rid || !_state.second_rid)
            throw submit_error_t (submit_result_t::invalid_argument, EINVAL);
        return _state.spot->request_to_spot (
          *_state.first_rid, *_state.second_rid,
          std::move (_state.parts.front ()), _state.timeout);
    case detail::spot_op_kind_t::request_to_router:
        if (!_state.first_rid)
            throw submit_error_t (submit_result_t::invalid_argument, EINVAL);
        return _state.spot->request_to_router (
          *_state.first_rid, std::move (_state.parts.front ()),
          _state.timeout);
    default:
        throw submit_error_t (submit_result_t::invalid_argument, EINVAL);
    }
}

inline bool request_ready_op_t::submit (request_callback_t callback_) &&
{
    request_callback_ready_op_t ready (std::move (_state));
    return std::move (ready).submit (std::move (callback_));
}

inline bool
request_callback_ready_op_t::submit (request_callback_t callback_) &&
{
    if (!_state.spot || _state.parts.empty ())
        throw submit_error_t (submit_result_t::invalid_argument, EINVAL);

    switch (_state.kind) {
    case detail::spot_op_kind_t::request_channel:
        return _state.spot->request_channel (
          _state.channel_name, _state.parts, std::move (callback_),
          _state.flags, _state.timeout);
    case detail::spot_op_kind_t::request_to_spot:
        if (!_state.first_rid || !_state.second_rid)
            throw submit_error_t (submit_result_t::invalid_argument, EINVAL);
        return _state.spot->request_to_spot (
          *_state.first_rid, *_state.second_rid,
          std::move (_state.parts.front ()), std::move (callback_),
          _state.flags, _state.timeout);
    case detail::spot_op_kind_t::request_to_router:
        if (!_state.first_rid)
            throw submit_error_t (submit_result_t::invalid_argument, EINVAL);
        return _state.spot->request_to_router (
          *_state.first_rid, std::move (_state.parts.front ()),
          std::move (callback_), _state.flags, _state.timeout);
    default:
        throw submit_error_t (submit_result_t::invalid_argument, EINVAL);
    }
}

inline void reply_ready_op_t::submit () &&
{
    if (!_state.spot || _state.parts.empty ())
        throw submit_error_t (submit_result_t::invalid_argument, EINVAL);

    switch (_state.kind) {
    case detail::spot_op_kind_t::reply_to_spot:
        if (!_state.first_rid || !_state.second_rid)
            throw submit_error_t (submit_result_t::invalid_argument, EINVAL);
        _state.spot->reply_to_spot (
          *_state.first_rid, *_state.second_rid, _state.request_seq,
          std::move (_state.parts.front ()), _state.flags);
        return;
    case detail::spot_op_kind_t::reply_to_router:
        if (!_state.first_rid)
            throw submit_error_t (submit_result_t::invalid_argument, EINVAL);
        _state.spot->reply_to_router (
          *_state.first_rid, _state.request_seq,
          std::move (_state.parts.front ()), _state.flags);
        return;
    default:
        throw submit_error_t (submit_result_t::invalid_argument, EINVAL);
    }
}

} // namespace service

namespace detail
{
inline void *native_handle (service::spot_node_t &node_) noexcept
{
    return node_._node;
}

inline const void *native_handle (const service::spot_node_t &node_) noexcept
{
    return node_._node;
}

inline void *native_handle (service::spot_t &spot_) noexcept
{
    return spot_._spot;
}

inline const void *native_handle (const service::spot_t &spot_) noexcept
{
    return spot_._spot;
}
} // namespace detail

namespace service
{

inline spot_t spot_node_t::create_spot ()
{
    return spot_t (*this);
}

inline spot_t spot_node_t::entry_spot ()
{
    void *handle = NULL;
    detail::throw_if_failed<config_error_t> (
      static_cast<config_result_t> (
        zlink_spot_node_entry_spot (_node, &handle)));
    return spot_t (handle);
}

inline std::optional<spot_t> spot_node_t::spot_lookup (
  const routing_id_t &spot_rid_)
{
    void *handle = NULL;
    const config_result_t rc = static_cast<config_result_t> (
      zlink_spot_node_spot_lookup (
        _node, zlink::detail::routing_id_native (spot_rid_), &handle));
    if (rc == config_result_t::not_found)
        return std::nullopt;
    detail::throw_if_failed<config_error_t> (rc);
    return std::optional<spot_t> (spot_t (handle));
}

} // namespace service
} // namespace zlink

#endif
