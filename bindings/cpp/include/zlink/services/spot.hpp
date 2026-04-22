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
#include <string>
#include <type_traits>
#include <vector>

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

class service_monitor_handle_t;

namespace service
{

class spot_t;

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
        parts_[moved].move_to (&native_[moved]);
        if (parts_[moved].valid ())
            break;
    }

    if (moved == parts_.size ())
        return 0;

    for (size_t i = 0; i < moved; ++i) {
        parts_[i].init ();
        if (parts_[i].valid ())
            (void) zlink_msg_move (parts_[i].handle (), &native_[i]);
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
            (void) zlink_msg_move (parts_[i].handle (), &native_[i]);
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
        if (zlink_msg_move (parts_[i].handle (), &parts_native_[i]) != 0) {
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
        if (zlink_msg_move (parts_[i].handle (), &parts_native_[i]) != 0) {
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
    std::promise<std::vector<message_t>> promise;
    std::function<void(request_result_t, std::vector<message_t>)> on_complete;
};

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
        (void) zlink_msg_move (parts[i].handle (), &parts_[i]);
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
        holder->promise.set_exception (
          std::make_exception_ptr (
            request_error_t (static_cast<request_result_t> (result_))));
        return;
    }
    std::vector<message_t> parts = take_parts (parts_, part_count_);
    if (holder->on_complete)
        holder->on_complete (request_result_t::ok, parts);
    holder->promise.set_value (std::move (parts));
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
        : _node (zlink_spot_node_new (ctx_.handle ())), _last_error (0)
    {
        if (!_node)
            _last_error = errno != 0 ? errno : EFAULT;
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

    int last_error () const noexcept { return _last_error; }

    void bind (const std::string &endpoint_)
    {
        validate_bounded_c_string (endpoint_, 255u, "endpoint");
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
        validate_bounded_c_string (endpoint_, 255u, "endpoint");
        detail::throw_if_failed<connect_error_t> (
          static_cast<connect_result_t> (
            zlink_spot_node_connect_peer (_node, endpoint_.c_str ())));
    }

    void disconnect_peer (const std::string &endpoint_)
    {
        validate_bounded_c_string (endpoint_, 255u, "endpoint");
        detail::throw_if_failed<connect_error_t> (
          static_cast<connect_result_t> (
            zlink_spot_node_disconnect_peer (_node, endpoint_.c_str ())));
    }

    void attach_discovery (discovery_t &discovery_)
    {
        detail::throw_if_failed<config_error_t> (
          static_cast<config_result_t> (
            zlink_spot_node_attach_discovery (_node, discovery_.handle ())));
    }

    template<typename DealerT>
    void attach_channel_dealer (discovery_t &discovery_, DealerT &dealer_)
    {
        detail::throw_if_failed<config_error_t> (
          static_cast<config_result_t> (
            zlink_spot_node_attach_channel_dealer (
              _node, discovery_.handle (), dealer_.handle ())));
    }

    template<typename DealerT>
    void attach_channel_dealer_manual (const std::string &channel_name_,
                                       DealerT &dealer_)
    {
        validate_bounded_c_string (channel_name_, 255u, "channel_name");
        if (channel_name_.empty ()) {
            errno = EINVAL;
            throw config_error_t (config_result_t::invalid_argument, EINVAL);
        }
        detail::throw_if_failed<config_error_t> (
          static_cast<config_result_t> (
            zlink_spot_node_attach_channel_dealer_manual (
              _node, channel_name_.c_str (), dealer_.handle ())));
    }

    template<typename PubT>
    void attach_pub_ingress (PubT &pub_)
    {
        detail::throw_if_failed<config_error_t> (
          static_cast<config_result_t> (
            zlink_spot_node_attach_pub_ingress (_node, pub_.handle ())));
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
        out_ = routing_id_t (native);
    }

    routing_id_t routing_id () const
    {
        routing_id_t value;
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

    pub_socket_options_t publisher_options ()
    {
        return pub_socket_options_t (_node);
    }

    sub_socket_options_t subscriber_options ()
    {
        return sub_socket_options_t (_node);
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
        std::snprintf (
          native_filter.peer_endpoint, sizeof (native_filter.peer_endpoint),
          "%s", filter_.peer_endpoint.c_str ());
        native_filter.source =
          static_cast<zlink_spot_peer_source_t> (filter_.source);
        native_filter.state =
          static_cast<zlink_spot_peer_state_t> (filter_.state);

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
            native_filter.role = static_cast<zlink_spot_role_t> (filter_->role);
            native_filter.subject_kind =
              static_cast<uint32_t> (filter_->subject_kind);
            std::snprintf (
              native_filter.subject, sizeof (native_filter.subject), "%s",
              filter_->subject.c_str ());
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

    spot_t create_spot ();

    void close ()
    {
        if (!_node)
            return;

        void *tmp = _node;
        detail::throw_if_failed<close_error_t> (
          static_cast<close_result_t> (zlink_spot_node_destroy (&tmp)));
        _node = NULL;
    }

    void *handle () const { return _node; }

  private:
    void *_node;
    int _last_error;
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
        : _spot (other._spot), _last_error (other._last_error)
    {
        other._spot = NULL;
        other._last_error = 0;
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
        other._spot = NULL;
        other._last_error = 0;
        return *this;
    }

    spot_t (const spot_t &) = delete;
    spot_t &operator= (const spot_t &) = delete;

    bool valid () const noexcept { return _spot != NULL; }

    int last_error () const noexcept { return _last_error; }

    bool publish (const std::string &service_name_,
                  const std::string &topic_,
                  std::vector<message_t> &parts_,
                  send_flags_t flags_ = send_flags_t::none)
    {
        validate_service_name (service_name_);
        validate_no_embedded_null (topic_, "topic");
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
        validate_no_embedded_null (topic_, "topic");
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
                  _spot, routing_id_native (dest_node_rid_),
                  routing_id_native (dest_spot_rid_), part_out_,
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
        detail::request_state_t *state = new detail::request_state_t ();
        std::future<std::vector<message_t>> future = state->promise.get_future ();
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
                  is_final_ ? static_cast<uint32_t> (timeout_.count ()) : 0u);
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
        detail::request_state_t *state = new detail::request_state_t ();
        state->on_complete = std::move (callback_);
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
                  is_final_ ? static_cast<uint32_t> (timeout_.count ()) : 0u);
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

    std::optional<topic_message_t> subscribe (recv_flags_t flags_ = recv_flags_t::none)
    {
        std::vector<message_t> parts;
        std::string service_name;
        std::string topic;
        routing_id_t source_rid;
        const recv_result_t rc = static_cast<recv_result_t> (subscribe_impl (
          parts, service_name, topic, flags_, &source_rid));
        if (rc == recv_result_t::no_data && flags_ == recv_flags_t::dontwait)
            return std::nullopt;
        if (rc != recv_result_t::ok)
            throw recv_error_t (rc, zlink_errno ());
        return std::optional<topic_message_t> (topic_message_t (
          source_rid.empty () ? std::nullopt
                              : std::optional<routing_id_t> (source_rid),
          service_name.empty () ? std::nullopt
                                : std::optional<std::string> (service_name),
          std::move (topic), std::move (parts)));
    }

    subscription_event_t
    receive_subscription_event (recv_flags_t flags_ = recv_flags_t::none)
    {
        subscription_event_t event;
        std::string service_name;
        std::string topic;
        routing_id_t source_rid;
        bool subscribed = false;
        const recv_result_t rc = static_cast<recv_result_t> (
          subscription_event_impl (
            source_rid, subscribed, service_name, topic, flags_));
        if (rc != recv_result_t::ok)
            throw recv_error_t (rc, zlink_errno ());
        if (!source_rid.empty ())
            event.routing_id = source_rid;
        event.service_name =
          service_name.empty () ? std::nullopt
                                : std::optional<std::string> (service_name);
        event.topic = std::move (topic);
        event.subscribed = subscribed;
        return event;
    }

    void set_subscription (const std::string &filter_)
    {
        validate_no_embedded_null (filter_, "filter");
        detail::throw_if_failed<config_error_t> (
          static_cast<config_result_t> (
            zlink_set_subscription (_spot, filter_.c_str ())));
    }

    void set_admission_state (admission_state_t state)
    {
        const int rc = zlink_set_admission_state (
          _spot, static_cast<zlink_admission_state_t> (state));
        if (rc != ZLINK_CONFIG_OK)
            throw config_error_t (static_cast<config_result_t> (rc));
    }

    admission_state_t get_admission_state () const
    {
        zlink_admission_state_t state = ZLINK_ADMISSION_SERVING;
        const int rc = zlink_get_admission_state (_spot, &state);
        if (rc != ZLINK_CONFIG_OK)
            throw config_error_t (static_cast<config_result_t> (rc));
        return static_cast<admission_state_t> (state);
    }

    void unset_subscription (const std::string &filter_)
    {
        validate_no_embedded_null (filter_, "filter");
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

    void on_send_ready (zlink_send_ready_handler_fn handler_,
                        void *userdata_ = NULL)
    {
        const handler_result_t rc = static_cast<handler_result_t> (
          zlink_send_ready_handler (_spot, handler_, userdata_));
        if (rc != handler_result_t::ok)
            throw handler_error_t (rc, zlink_errno ());
    }

    void *handle () const { return _spot; }

  private:
    static void validate_channel_name (const std::string &channel_name_)
    {
        validate_bounded_c_string (channel_name_, 255u, "channel_name");
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
        part_.move_to (&native);
        if (part_.valid ())
            return -1;

        const int rc = zlink_spot_publish_part (
          _spot, service_name_, topic_, &native,
          static_cast<zlink_send_flags_t> (flags_), ZLINK_PART_FINAL);
        if (rc != 0) {
            const int err = errno;
            part_.init ();
            if (part_.valid ())
                (void) zlink_msg_move (part_.handle (), &native);
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
        part_.move_to (&native);
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
                    (void) zlink_msg_move (part_.handle (), &native);
                (void) zlink_msg_close (&native);
            }
            errno = err;
            return 0;
        }

        part_.init ();
        if (part_.valid ())
            (void) zlink_msg_move (part_.handle (), &native);
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
                *source_rid_out_ = routing_id_t (source_rid);
            else
                *source_rid_out_ = routing_id_t ();
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
            source_rid_out_ = routing_id_t (*source_rid);
        else
            source_rid_out_ = routing_id_t ();

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
        part_.move_to (&native);
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
                    (void) zlink_msg_move (part_.handle (), &native);
                (void) zlink_msg_close (&native);
            }
            return 0;
        }

        part_.init ();
        if (part_.valid ())
            (void) zlink_msg_move (part_.handle (), &native);
        (void) zlink_msg_close (&native);
        errno = err;
        return -1;
    }

  private:
    explicit spot_t (spot_node_t &node_)
        : _spot (zlink_spot_new (node_.handle ())), _last_error (0)
    {
        if (!_spot)
            _last_error = errno != 0 ? errno : EFAULT;
    }

    friend class spot_node_t;

  public:
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
        out_ = routing_id_t (native);
    }

    routing_id_t routing_id () const
    {
        routing_id_t value;
        get_routing_id (value);
        return value;
    }

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
                  _spot, routing_id_native (dest_node_rid_),
                  routing_id_native (dest_spot_rid_), request_seq_, part_out_,
                  part_flag_);
            }));
        if (rc != submit_result_t::ok) {
            detail::close_native_parts (native, failed_index);
            throw submit_error_t (rc, zlink_errno ());
        }
    }

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
                  _spot, routing_id_native (peer_rid_), request_seq_, part_out_,
                  part_flag_);
            }));
        if (rc != submit_result_t::ok) {
            detail::close_native_parts (native, failed_index);
            throw submit_error_t (rc, zlink_errno ());
        }
    }

    received_t recv_routed (recv_flags_t flags_ = recv_flags_t::none)
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
        if (rc != recv_result_t::ok)
            throw recv_error_t (rc, zlink_errno ());
        std::vector<message_t> parts;
        if (detail::assign_parts_from_native (parts_native, part_count, parts) != 0)
            throw last_error ();

        std::function<void(std::vector<message_t> &, send_flags_t)> reply_fn;
        if (request_seq != 0u) {
            const std::optional<routing_id_t> node_rid =
              (source_node_rid && source_node_rid->size > 0)
                ? std::optional<routing_id_t> (routing_id_t (*source_node_rid))
                : std::nullopt;
            const std::optional<routing_id_t> spot_rid =
              (source_spot_rid && source_spot_rid->size > 0)
                ? std::optional<routing_id_t> (routing_id_t (*source_spot_rid))
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
                          _spot, routing_id_native (*node_rid),
                          routing_id_native (*spot_rid), request_seq,
                          part_out_, part_flag_);
                    }));
                if (reply_rc != submit_result_t::ok) {
                    detail::restore_parts_from_native (
                      reply_parts_, native_reply, failed_index);
                    throw submit_error_t (reply_rc, zlink_errno ());
                }
            };
        }

        return received_t (
          (source_node_rid && source_node_rid->size > 0)
            ? std::optional<routing_id_t> (routing_id_t (*source_node_rid))
            : std::nullopt,
          (source_spot_rid && source_spot_rid->size > 0)
            ? std::optional<routing_id_t> (routing_id_t (*source_spot_rid))
            : std::nullopt,
          request_seq != 0u ? std::optional<uint64_t> (request_seq)
                            : std::nullopt,
          std::move (parts), std::move (reply_fn));
    }

    void on_routed_receive (zlink_spot_handler_fn handler_,
                            void *userdata_ = NULL)
    {
        const handler_result_t rc = static_cast<handler_result_t> (
          zlink_spot_handler (_spot, handler_, userdata_));
        if (rc != handler_result_t::ok)
            throw handler_error_t (rc, zlink_errno ());
    }

    void on_dispatch_event (zlink_spot_dispatch_event_handler_fn handler_,
                            void *userdata_ = NULL)
    {
        const handler_result_t rc = static_cast<handler_result_t> (
          zlink_spot_dispatch_event_handler (_spot, handler_, userdata_));
        if (rc != handler_result_t::ok)
            throw handler_error_t (rc, zlink_errno ());
    }

    common_socket_options_t options () { return common_socket_options_t (_spot); }
    pub_socket_options_t publisher_options () { return pub_socket_options_t (_spot); }
    sub_socket_options_t subscriber_options () { return sub_socket_options_t (_spot); }

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
};

inline spot_t spot_node_t::create_spot ()
{
    return spot_t (*this);
}

} // namespace service
} // namespace zlink

#endif
