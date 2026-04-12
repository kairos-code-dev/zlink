/* SPDX-License-Identifier: MPL-2.0 */
#ifndef ZLINK_CPP_SERVICES_SPOT_HPP_INCLUDED
#define ZLINK_CPP_SERVICES_SPOT_HPP_INCLUDED

#include "../context.hpp"
#include "../message.hpp"
#include "../types.hpp"
#include "discovery.hpp"

#include <cerrno>
#include <cstdlib>
#include <cstring>
#include <string>
#include <type_traits>
#include <vector>

namespace zlink
{
class service_monitor_handle_t;

namespace service
{

namespace detail
{

inline void close_message_array (zlink_msg_t *parts_, size_t part_count_) noexcept
{
    if (!parts_)
        return;
    zlink_multipart_close (parts_, part_count_);
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

    ~spot_node_t () { (void) close (); }

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

        (void) close ();
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

    ZLINK_CPP_NODISCARD int bind (const std::string &endpoint_)
    {
        validate_bounded_c_string (endpoint_, 255u, "endpoint");
        return zlink_spot_node_bind (_node, endpoint_.c_str ());
    }

    std::string last_endpoint () const
    {
        zlink_spot_node_status_t status;
        if (zlink_spot_node_status_snapshot (_node, &status) == 0
            && status.local_endpoint[0] != '\0')
            return std::string (status.local_endpoint);
        return std::string ();
    }

    ZLINK_CPP_NODISCARD int connect_peer (const std::string &endpoint_)
    {
        validate_bounded_c_string (endpoint_, 255u, "endpoint");
        return zlink_spot_node_connect_peer (_node, endpoint_.c_str ());
    }

    ZLINK_CPP_NODISCARD int disconnect_peer (const std::string &endpoint_)
    {
        validate_bounded_c_string (endpoint_, 255u, "endpoint");
        return zlink_spot_node_disconnect_peer (_node, endpoint_.c_str ());
    }

    ZLINK_CPP_NODISCARD int attach_discovery (discovery_t &discovery_)
    {
        return zlink_spot_node_attach_discovery (_node, discovery_.handle ());
    }

    ZLINK_CPP_NODISCARD int set_routing_id (const routing_id_t &routing_id_)
    {
        return zlink_set_routing_id (
          _node, routing_id_.to_string ().data (), routing_id_.size ());
    }

    ZLINK_CPP_NODISCARD int get_routing_id (routing_id_t &out_) const
    {
        zlink_routing_id_t native;
        std::memset (&native, 0, sizeof (native));
        const int rc = zlink_get_routing_id (_node, &native);
        if (rc != 0)
            return rc;
        out_ = routing_id_t (native.data, native.size);
        return 0;
    }

    ZLINK_CPP_NODISCARD int set_tls_server (const std::string &cert_,
                                            const std::string &key_,
                                            bool require_client_cert_ = false)
    {
        return zlink_set_tls_server (
          _node, cert_.c_str (), key_.c_str (), require_client_cert_ ? 1 : 0);
    }

    ZLINK_CPP_NODISCARD int
    set_tls_client (const std::string &ca_cert_,
                    const std::string &hostname_ = std::string (),
                    bool trust_system_ = false)
    {
        const char *ca = ca_cert_.empty () ? NULL : ca_cert_.c_str ();
        const char *hostname =
          hostname_.empty () ? NULL : hostname_.c_str ();
        return zlink_set_tls_client (
          _node, ca, hostname, trust_system_ ? 1 : 0);
    }

    template<typename T>
    ZLINK_CPP_NODISCARD
    typename std::enable_if<!std::is_same<T, std::string>::value, int>::type
    set (socket_option_key_t<T> key_, const T &value_)
    {
        return zlink_set_option (
          _node, static_cast<zlink_option_t> (key_.option), &value_,
          sizeof (value_));
    }

    ZLINK_CPP_NODISCARD int
    set (socket_option_key_t<std::string> key_, const std::string &value_)
    {
        return zlink_set_option (
          _node, static_cast<zlink_option_t> (key_.option), value_.data (),
          value_.size ());
    }

    template<typename T>
    ZLINK_CPP_NODISCARD
    typename std::enable_if<!std::is_same<T, std::string>::value, int>::type
    get (socket_option_key_t<T> key_, T &value_) const
    {
        size_t size = sizeof (value_);
        return zlink_get_option (
          _node, static_cast<zlink_option_t> (key_.option), &value_, &size);
    }

    ZLINK_CPP_NODISCARD int
    get (socket_option_key_t<std::string> key_, std::string &value_) const
    {
        return detail::get_string_option (
          [](void *handle_, socket_option option_, void *value_, size_t *size_) {
              return zlink_get_option (
                handle_, static_cast<zlink_option_t> (option_), value_, size_);
          },
          _node, key_.option, 256u, value_);
    }

    ZLINK_CPP_NODISCARD int
    status_snapshot (zlink_spot_node_status_t &out_) const
    {
        return zlink_spot_node_status_snapshot (_node, &out_);
    }

    ZLINK_CPP_NODISCARD int
    peers_snapshot (zlink_spot_node_peer_entry_t *entries_, size_t *count_) const
    {
        return zlink_spot_node_peers_snapshot (_node, entries_, count_);
    }

    ZLINK_CPP_NODISCARD int
    peers_query (zlink_spot_node_peer_entry_t *entries_,
                 size_t *count_,
                 const zlink_spot_node_peer_filter_t *filter_) const
    {
        return zlink_spot_node_peers_query (_node, filter_, entries_, count_);
    }

    ZLINK_CPP_NODISCARD int
    subjects_snapshot (zlink_spot_node_subject_entry_t *entries_,
                       size_t *count_,
                       const zlink_spot_node_subject_filter_t *filter_ = NULL) const
    {
        return zlink_spot_node_subjects_snapshot (
          _node, filter_, entries_, count_);
    }

    ZLINK_CPP_NODISCARD int close ()
    {
        if (!_node)
            return 0;

        void *tmp = _node;
        const int rc = zlink_spot_node_destroy (&tmp);
        if (rc == 0)
            _node = NULL;
        return rc;
    }

    void *handle () const { return _node; }

  private:
    void *_node;
    int _last_error;
};

class spot_t
{
  public:
    explicit spot_t (spot_node_t &node_)
        : _spot (zlink_spot_new (node_.handle ())), _last_error (0)
    {
        if (!_spot)
            _last_error = errno != 0 ? errno : EFAULT;
    }

    ~spot_t () { (void) close (); }

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

        (void) close ();
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

    void publish (const std::string &topic_, std::vector<message_t> &parts_,
                  send_flags_t flags_ = send_flags_t::none)
    {
        validate_no_embedded_null (topic_, "topic");
        const int rc = publish_impl (
          topic_.c_str (), parts_,
          flags_ == send_flags_t::dontwait ? send_flag::dontwait
                                           : send_flag::none);
        throw_on_error (rc);
    }

    void publish (const std::string &topic_, message_t &part_,
                  send_flags_t flags_ = send_flags_t::none)
    {
        validate_no_embedded_null (topic_, "topic");
        const int rc = publish_impl (
          topic_.c_str (), part_,
          flags_ == send_flags_t::dontwait ? send_flag::dontwait
                                           : send_flag::none);
        throw_on_error (rc);
    }

    void publish (const char *topic_, std::vector<message_t> &parts_,
                  send_flags_t flags_ = send_flags_t::none)
    {
        if (!topic_) {
            errno = EINVAL;
            throw_on_error (-1);
        }
        const int rc = publish_impl (
          topic_, parts_,
          flags_ == send_flags_t::dontwait ? send_flag::dontwait
                                           : send_flag::none);
        throw_on_error (rc);
    }

    void publish (const char *topic_, message_t &part_,
                  send_flags_t flags_ = send_flags_t::none)
    {
        if (!topic_) {
            errno = EINVAL;
            throw_on_error (-1);
        }
        const int rc = publish_impl (
          topic_, part_,
          flags_ == send_flags_t::dontwait ? send_flag::dontwait
                                           : send_flag::none);
        throw_on_error (rc);
    }

    ZLINK_CPP_NODISCARD subscribed_t subscribe (
      recv_flags_t flags_ = recv_flags_t::none)
    {
        subscribed_t subscribed;
        const int rc = subscribe_impl (
          subscribed.parts, subscribed.topic,
          flags_ == recv_flags_t::dontwait ? recv_flag::dontwait
                                           : recv_flag::none,
          &subscribed.routing_id);
        throw_on_error (rc);
        return subscribed;
    }

    ZLINK_CPP_NODISCARD int set_subscription (const std::string &filter_)
    {
        validate_no_embedded_null (filter_, "filter");
        return zlink_set_subscription (_spot, filter_.c_str ());
    }

    ZLINK_CPP_NODISCARD int unset_subscription (const std::string &filter_)
    {
        validate_no_embedded_null (filter_, "filter");
        return zlink_unset_subscription (_spot, filter_.c_str ());
    }

    ZLINK_CPP_NODISCARD int
    subscription_at (size_t index_,
                     std::string &filter_out_,
                     bool *is_pattern_out_ = NULL) const
    {
        size_t capacity = 256u;
        const size_t max_capacity = 64u * 1024u;

        while (capacity <= max_capacity) {
            std::vector<char> buffer (capacity);
            size_t length = capacity;
            int is_pattern = 0;
            const int rc = zlink_subscription_at (
              _spot, index_, buffer.data (), &length, &is_pattern);
            if (rc == 0) {
                const size_t bounded = length <= buffer.size () ? length
                                                                : buffer.size ();
                size_t out_size = bounded;
                if (out_size > 0 && buffer[out_size - 1] == '\0')
                    --out_size;
                filter_out_.assign (buffer.data (), out_size);
                if (is_pattern_out_)
                    *is_pattern_out_ = is_pattern != 0;
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

    ZLINK_CPP_NODISCARD int
    on_subscribe (zlink_subscribe_handler_fn handler_,
                  void *userdata_ = NULL)
    {
        return zlink_subscribe_handler (_spot, handler_, userdata_);
    }

    ZLINK_CPP_NODISCARD int
    on_send_ready (zlink_send_ready_handler_fn handler_,
                   void *userdata_ = NULL)
    {
        return zlink_send_ready_handler (_spot, handler_, userdata_);
    }

    void *handle () const { return _spot; }

  private:
    ZLINK_CPP_NODISCARD int
    publish_impl (const char *topic_,
                  std::vector<message_t> &parts_,
                  send_flag flags_)
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

        const int rc = zlink_publish (
          _spot, topic_, native.data (), native.size (),
          static_cast<zlink_send_flags_t> (flags_));
        if (rc != 0) {
            const int err = errno;
            for (size_t i = 0; i < native.size (); ++i)
                (void) zlink_msg_close (&native[i]);
            errno = err;
        }
        return rc;
    }

    ZLINK_CPP_NODISCARD int
    publish_impl (const char *topic_,
                  message_t &part_,
                  send_flag flags_)
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

        const int rc = zlink_publish (
          _spot, topic_, &native, 1,
          static_cast<zlink_send_flags_t> (flags_));
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
    try_publish_impl (send_result_t &result_out_,
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

        const int rc =
          zlink_publish (_spot, topic_, native.data (), native.size (), ZLINK_DONTWAIT);
        if (rc == 0) {
            result_out_ = send_result_t::sent;
            return 0;
        }

        const int err = errno;
        if (detail::classify_nonblocking_send_errno (err, result_out_)) {
            if (result_out_ != send_result_t::sent) {
                for (size_t i = 0; i < native.size (); ++i) {
                    parts_[i].init ();
                    if (parts_[i].valid ())
                        (void) zlink_msg_move (parts_[i].handle (), &native[i]);
                    (void) zlink_msg_close (&native[i]);
                }
            }
            return 0;
        }

        for (size_t i = 0; i < native.size (); ++i)
            (void) zlink_msg_close (&native[i]);
        errno = err;
        return -1;
    }

    ZLINK_CPP_NODISCARD int
    try_publish_impl (send_result_t &result_out_,
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

        const int rc = zlink_publish (_spot, topic_, &native, 1, ZLINK_DONTWAIT);
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

    ZLINK_CPP_NODISCARD int
    subscribe_impl (std::vector<message_t> &parts_,
                    std::string &topic_,
                    recv_flag flags_ = recv_flag::none,
                    routing_id_t *source_rid_out_ = NULL,
               size_t *topic_len_out_ = NULL,
               bool *truncated_out_ = NULL)
    {
        if (!_spot) {
            errno = _last_error != 0 ? _last_error : EFAULT;
            return -1;
        }

        zlink_msg_t *parts_native = NULL;
        size_t part_count = 0;
        char topic_buffer[256];
        size_t topic_length = sizeof (topic_buffer);
        routing_id_t source_rid;
        zlink_routing_id_t *rid_ptr =
          source_rid_out_ ? routing_id_native (*source_rid_out_)
                          : routing_id_native (source_rid);

        const int rc = zlink_subscribe (
          _spot, rid_ptr, &parts_native, &part_count, topic_buffer, &topic_length,
          static_cast<zlink_recv_flags_t> (flags_));
        if (rc != 0)
            return rc;

        const bool truncated = topic_length > (sizeof (topic_buffer) - 1u);
        if (topic_len_out_)
            *topic_len_out_ = topic_length;
        if (truncated_out_)
            *truncated_out_ = truncated;

        const size_t topic_size =
          topic_length < sizeof (topic_buffer) ? topic_length
                                               : sizeof (topic_buffer) - 1u;
        topic_.assign (topic_buffer, topic_size);
        return detail::assign_parts_from_native (parts_native, part_count, parts_);
    }

    ZLINK_CPP_NODISCARD int
    subscribe_impl (message_t &part_,
                    std::string &topic_,
                    recv_flag flags_ = recv_flag::none,
                    routing_id_t *source_rid_out_ = NULL,
                    size_t *topic_len_out_ = NULL,
                    bool *truncated_out_ = NULL)
    {
        if (!_spot) {
            errno = _last_error != 0 ? _last_error : EFAULT;
            return -1;
        }

        zlink_msg_t *parts_native = NULL;
        size_t part_count = 0;
        char topic_buffer[256];
        size_t topic_length = sizeof (topic_buffer);
        routing_id_t source_rid;
        zlink_routing_id_t *rid_ptr =
          source_rid_out_ ? routing_id_native (*source_rid_out_)
                          : routing_id_native (source_rid);

        const int rc = zlink_subscribe (
          _spot, rid_ptr, &parts_native, &part_count, topic_buffer, &topic_length,
          static_cast<zlink_recv_flags_t> (flags_));
        if (rc != 0)
            return rc;

        if (part_count != 1 || !parts_native) {
            detail::close_message_array (parts_native, part_count);
            errno = EMSGSIZE;
            return -1;
        }

        const bool truncated = topic_length > (sizeof (topic_buffer) - 1u);
        if (topic_len_out_)
            *topic_len_out_ = topic_length;
        if (truncated_out_)
            *truncated_out_ = truncated;

        const size_t topic_size =
          topic_length < sizeof (topic_buffer) ? topic_length
                                               : sizeof (topic_buffer) - 1u;
        topic_.assign (topic_buffer, topic_size);

        message_t tmp;
        if (zlink_msg_move (tmp.handle (), &parts_native[0]) != 0) {
            detail::close_message_array (parts_native, part_count);
            return -1;
        }
        detail::close_message_array (parts_native, part_count);
        part_ = std::move (tmp);
        return 0;
    }

  public:

    template<typename T>
    ZLINK_CPP_NODISCARD
    typename std::enable_if<!std::is_same<T, std::string>::value, int>::type
    set (socket_option_key_t<T> key_, const T &value_)
    {
        return zlink_set_option (
          _spot, static_cast<zlink_option_t> (key_.option), &value_,
          sizeof (value_));
    }

    ZLINK_CPP_NODISCARD int
    set (socket_option_key_t<std::string> key_, const std::string &value_)
    {
        return zlink_set_option (
          _spot, static_cast<zlink_option_t> (key_.option), value_.data (),
          value_.size ());
    }

    template<typename T>
    ZLINK_CPP_NODISCARD
    typename std::enable_if<!std::is_same<T, std::string>::value, int>::type
    get (socket_option_key_t<T> key_, T &value_) const
    {
        size_t size = sizeof (value_);
        return zlink_get_option (
          _spot, static_cast<zlink_option_t> (key_.option), &value_, &size);
    }

    ZLINK_CPP_NODISCARD int
    get (socket_option_key_t<std::string> key_, std::string &value_) const
    {
        return detail::get_string_option (
          [](void *handle_, socket_option option_, void *value_, size_t *size_) {
              return zlink_get_option (
                handle_, static_cast<zlink_option_t> (option_), value_, size_);
          },
          _spot, key_.option, 256u, value_);
    }

    template<typename T>
    ZLINK_CPP_NODISCARD
    typename std::enable_if<!std::is_same<T, std::string>::value, int>::type
    set (pub_option_key_t<T> key_, const T &value_)
    {
        return zlink_set_pub_option (
          _spot, static_cast<zlink_pub_option_t> (key_.option), &value_,
          sizeof (value_));
    }

    ZLINK_CPP_NODISCARD int
    set (pub_option_key_t<std::string> key_, const std::string &value_)
    {
        return zlink_set_pub_option (
          _spot, static_cast<zlink_pub_option_t> (key_.option), value_.data (),
          value_.size ());
    }

    template<typename T>
    ZLINK_CPP_NODISCARD int get (pub_option_key_t<T> key_, T &value_) const
    {
        size_t size = sizeof (value_);
        return zlink_get_pub_option (
          _spot, static_cast<zlink_pub_option_t> (key_.option), &value_, &size);
    }

    ZLINK_CPP_NODISCARD int
    get (pub_option_key_t<std::string> key_, std::string &value_) const
    {
        return detail::get_string_option (
          [](void *handle_, pub_option option_, void *value_, size_t *size_) {
              return zlink_get_pub_option (
                handle_, static_cast<zlink_pub_option_t> (option_), value_,
                size_);
          },
          _spot, key_.option, 256u, value_);
    }

    template<typename T>
    ZLINK_CPP_NODISCARD int set (sub_option_key_t<T> key_, const T &value_)
    {
        return zlink_set_sub_option (
          _spot, static_cast<zlink_sub_option_t> (key_.option), &value_,
          sizeof (value_));
    }

    template<typename T>
    ZLINK_CPP_NODISCARD int get (sub_option_key_t<T> key_, T &value_) const
    {
        size_t size = sizeof (value_);
        return zlink_get_sub_option (
          _spot, static_cast<zlink_sub_option_t> (key_.option), &value_, &size);
    }

    ZLINK_CPP_NODISCARD int set_routing_id (const routing_id_t &routing_id_)
    {
        return zlink_set_routing_id (
          _spot, routing_id_.to_string ().data (), routing_id_.size ());
    }

    ZLINK_CPP_NODISCARD int get_routing_id (routing_id_t &out_) const
    {
        zlink_routing_id_t native;
        std::memset (&native, 0, sizeof (native));
        const int rc = zlink_get_routing_id (_spot, &native);
        if (rc != 0)
            return rc;
        out_ = routing_id_t (native.data, native.size);
        return 0;
    }

    ZLINK_CPP_NODISCARD int close ()
    {
        if (!_spot)
            return 0;

        void *tmp = _spot;
        const int rc = zlink_spot_destroy (&tmp);
        if (rc == 0)
            _spot = NULL;
        return rc;
    }

  private:
    void *_spot;
    int _last_error;
};

} // namespace service
} // namespace zlink

#endif
