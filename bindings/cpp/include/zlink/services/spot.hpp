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
        if (parts_[moved].move_to (&native_[moved]) != 0)
            break;
    }

    if (moved == parts_.size ())
        return 0;

    for (size_t i = 0; i < moved; ++i) {
        if (parts_[i].init () == 0)
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
    std::vector<message_t> tmp;
    tmp.resize (part_count_);

    for (size_t i = 0; i < part_count_; ++i) {
        if (zlink_msg_move (tmp[i].handle (), &parts_native_[i]) != 0) {
            close_message_array (parts_native_, part_count_);
            return -1;
        }
    }

    parts_.swap (tmp);
    return 0;
}

inline int get_string_option (int (*getter_) (void *, zlink_option_t, void *, size_t *),
                              void *handle_,
                              socket_option option_,
                              size_t initial_capacity_,
                              std::string &value_)
{
    size_t capacity = initial_capacity_;
    const size_t max_capacity = 64u * 1024u;

    while (capacity <= max_capacity) {
        std::vector<char> buffer (capacity);
        size_t size = capacity;
        if (getter_ (
              handle_, static_cast<zlink_option_t> (option_), buffer.data (),
              &size)
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

inline int get_string_option (int (*getter_) (void *, zlink_pub_option_t, void *, size_t *),
                              void *handle_,
                              pub_option option_,
                              size_t initial_capacity_,
                              std::string &value_)
{
    size_t capacity = initial_capacity_;
    const size_t max_capacity = 64u * 1024u;

    while (capacity <= max_capacity) {
        std::vector<char> buffer (capacity);
        size_t size = capacity;
        if (getter_ (
              handle_, static_cast<zlink_pub_option_t> (option_), buffer.data (),
              &size)
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
    case ZLINK_SEND_RESULT_SENT:
        return send_result_t::sent;
    case ZLINK_SEND_RESULT_BACKPRESSURED:
        return send_result_t::backpressured;
    case ZLINK_SEND_RESULT_NOT_READY:
        return send_result_t::not_ready;
    default:
        return send_result_t::sent;
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

    ~spot_node_t () { (void) destroy (); }

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

        (void) destroy ();
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
        return zlink_spot_node_bind (_node, endpoint_.c_str ());
    }

    ZLINK_CPP_NODISCARD int connect_peer (const std::string &endpoint_)
    {
        return zlink_spot_node_connect_peer (_node, endpoint_.c_str ());
    }

    ZLINK_CPP_NODISCARD int disconnect_peer (const std::string &endpoint_)
    {
        return zlink_spot_node_disconnect_peer (_node, endpoint_.c_str ());
    }

    ZLINK_CPP_NODISCARD int attach_discovery (discovery_t &discovery_)
    {
        return zlink_spot_node_attach_discovery (_node, discovery_.handle ());
    }

    ZLINK_CPP_NODISCARD int set_routing_id (const void *data_, size_t size_)
    {
        return zlink_set_routing_id (_node, data_, size_);
    }

    ZLINK_CPP_NODISCARD int set_routing_id (const std::string &data_)
    {
        return set_routing_id (data_.data (), data_.size ());
    }

    ZLINK_CPP_NODISCARD int get_routing_id (zlink_routing_id_t *out_) const
    {
        return zlink_get_routing_id (_node, out_);
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
          &zlink_get_option, _node, key_.option, 256u, value_);
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

    ZLINK_CPP_NODISCARD int destroy ()
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

    ~spot_t () { (void) destroy (); }

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

        (void) destroy ();
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

    void publish (const std::string &topic_, std::vector<message_t> &parts_)
    {
        const int rc = publish (topic_, parts_, send_flag::none);
        throw_on_error (rc);
    }

    void publish (const std::string &topic_, message_t &part_)
    {
        const int rc = publish (topic_, part_, send_flag::none);
        throw_on_error (rc);
    }

    ZLINK_CPP_NODISCARD send_result_t
    try_publish (const std::string &topic_, std::vector<message_t> &parts_)
    {
        send_result_t result = send_result_t::sent;
        const int rc = try_publish (result, topic_, parts_);
        throw_on_error (rc);
        return result;
    }

    ZLINK_CPP_NODISCARD send_result_t
    try_publish (const std::string &topic_, message_t &part_)
    {
        send_result_t result = send_result_t::sent;
        const int rc = try_publish (result, topic_, part_);
        throw_on_error (rc);
        return result;
    }

    ZLINK_CPP_NODISCARD int
    publish (const std::string &topic_,
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
          _spot, topic_.c_str (), native.data (), native.size (),
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
    publish (const std::string &topic_,
             message_t &part_,
             send_flag flags_)
    {
        if (!_spot) {
            errno = _last_error != 0 ? _last_error : EFAULT;
            return -1;
        }

        zlink_msg_t native;
        if (zlink_msg_init_size (&native, part_.size ()) != 0)
            return -1;
        if (part_.size () > 0 && part_.data ())
            std::memcpy (zlink_msg_data (&native), part_.data (), part_.size ());

        const int rc = zlink_publish (
          _spot, topic_.c_str (), &native, 1,
          static_cast<zlink_send_flags_t> (flags_));
        if (rc != 0) {
            const int err = errno;
            (void) zlink_msg_close (&native);
            errno = err;
        }
        return rc;
    }

    ZLINK_CPP_NODISCARD int
    try_publish (send_result_t &result_out_,
                 const std::string &topic_,
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

        zlink_send_result_t native_result = ZLINK_SEND_RESULT_SENT;
        const int rc = zlink_try_publish (
          _spot, topic_.c_str (), native.data (), native.size (),
          &native_result);
        if (rc == 0) {
            result_out_ = detail::to_send_result (native_result);
            if (native_result != ZLINK_SEND_RESULT_SENT) {
                for (size_t i = 0; i < native.size (); ++i) {
                    if (parts_[i].init () == 0)
                        (void) zlink_msg_move (parts_[i].handle (), &native[i]);
                    (void) zlink_msg_close (&native[i]);
                }
            }
            return 0;
        }

        const int err = errno;
        for (size_t i = 0; i < native.size (); ++i)
            (void) zlink_msg_close (&native[i]);
        errno = err;
        return -1;
    }

    ZLINK_CPP_NODISCARD int
    try_publish (send_result_t &result_out_,
                 const std::string &topic_,
                 message_t &part_)
    {
        if (!_spot) {
            errno = _last_error != 0 ? _last_error : EFAULT;
            return -1;
        }

        zlink_msg_t native;
        if (zlink_msg_init_size (&native, part_.size ()) != 0)
            return -1;
        if (part_.size () > 0 && part_.data ())
            std::memcpy (zlink_msg_data (&native), part_.data (), part_.size ());

        zlink_send_result_t native_result = ZLINK_SEND_RESULT_SENT;
        const int rc =
          zlink_try_publish (_spot, topic_.c_str (), &native, 1, &native_result);
        if (rc == 0) {
            result_out_ = detail::to_send_result (native_result);
            if (native_result != ZLINK_SEND_RESULT_SENT) {
                if (part_.init () == 0)
                    (void) zlink_msg_move (part_.handle (), &native);
                (void) zlink_msg_close (&native);
            }
            return 0;
        }

        const int err = errno;
        (void) zlink_msg_close (&native);
        errno = err;
        return -1;
    }

    ZLINK_CPP_NODISCARD int
    publish (const std::string &topic_,
             const void *data_,
             size_t size_,
             send_flag flags_)
    {
        message_t part = message_t::from_bytes (data_, size_);
        if (!part.valid ())
            return -1;
        return publish (topic_, part, flags_);
    }

    ZLINK_CPP_NODISCARD int
    publish (const std::string &topic_,
             const std::string &text_,
             send_flag flags_)
    {
        return publish (topic_, text_.data (), text_.size (), flags_);
    }

    ZLINK_CPP_NODISCARD int
    publish_zero (const std::string &topic_,
                  void *data_,
                  size_t size_,
                  zlink_free_fn *ffn_,
                  void *hint_ = NULL,
                  send_flag flags_ = send_flag::none)
    {
        if (!_spot) {
            errno = _last_error != 0 ? _last_error : EFAULT;
            return -1;
        }
        if (size_ > 0 && !data_) {
            errno = EINVAL;
            return -1;
        }

        zlink_msg_t part;
        if (zlink_msg_init_data (&part, data_, size_, ffn_, hint_) != 0)
            return -1;

        const int rc = zlink_publish (
          _spot, topic_.c_str (), &part, 1, static_cast<zlink_send_flags_t> (flags_));
        if (rc != 0) {
            const int err = errno;
            (void) zlink_msg_close (&part);
            errno = err;
        }
        return rc;
    }

    ZLINK_CPP_NODISCARD int set_subscription (const std::string &filter_)
    {
        return zlink_set_subscription (_spot, filter_.c_str ());
    }

    ZLINK_CPP_NODISCARD int unset_subscription (const std::string &filter_)
    {
        return zlink_unset_subscription (_spot, filter_.c_str ());
    }

    ZLINK_CPP_NODISCARD int subscribe (const std::string &filter_)
    {
        return set_subscription (filter_);
    }

    ZLINK_CPP_NODISCARD int unsubscribe (const std::string &filter_)
    {
        return unset_subscription (filter_);
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
    subscribe_handler (zlink_subscribe_handler_fn handler_,
                       void *userdata_ = NULL)
    {
        return zlink_subscribe_handler (_spot, handler_, userdata_);
    }

    ZLINK_CPP_NODISCARD int
    send_ready_handler (zlink_send_ready_handler_fn handler_,
                        void *userdata_ = NULL)
    {
        return zlink_send_ready_handler (_spot, handler_, userdata_);
    }

    ZLINK_CPP_NODISCARD int
    recv (std::vector<message_t> &parts_,
          std::string &topic_,
          recv_flag flags_ = recv_flag::none,
          zlink_routing_id_t *source_rid_out_ = NULL,
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
        zlink_routing_id_t source_rid;
        zlink_routing_id_t *rid_ptr = source_rid_out_ ? source_rid_out_ : &source_rid;

        const int rc = zlink_subscribe (
          _spot, rid_ptr, &parts_native, &part_count, topic_buffer, &topic_length,
          static_cast<zlink_send_flags_t> (flags_));
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

    ZLINK_CPP_NODISCARD subscribed_t receive ()
    {
        subscribed_t subscribed;
        const int rc = recv (subscribed.parts, subscribed.topic, recv_flag::none,
                             &subscribed.routing_id);
        throw_on_error (rc);
        return subscribed;
    }

    ZLINK_CPP_NODISCARD maybe_t<subscribed_t> try_receive ()
    {
        subscribed_t subscribed;
        const int rc =
          recv (subscribed.parts, subscribed.topic, recv_flag::dontwait,
                &subscribed.routing_id);
        if (rc == 0)
            return maybe_t<subscribed_t> (std::move (subscribed));
        if (errno == EAGAIN)
            return maybe_t<subscribed_t> ();
        throw_on_error (rc);
        return maybe_t<subscribed_t> ();
    }

    ZLINK_CPP_NODISCARD int
    recv (message_t &part_,
          std::string &topic_,
          recv_flag flags_ = recv_flag::none,
          zlink_routing_id_t *source_rid_out_ = NULL,
          size_t *topic_len_out_ = NULL,
          bool *truncated_out_ = NULL)
    {
        std::vector<message_t> parts;
        if (recv (
              parts, topic_, flags_, source_rid_out_, topic_len_out_,
              truncated_out_)
            != 0)
            return -1;

        if (parts.size () != 1) {
            errno = EMSGSIZE;
            return -1;
        }

        part_ = std::move (parts[0]);
        return 0;
    }

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
          &zlink_get_option, _spot, key_.option, 256u, value_);
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
          &zlink_get_pub_option, _spot, key_.option, 256u, value_);
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

    ZLINK_CPP_NODISCARD int set_routing_id (const void *data_, size_t size_)
    {
        return zlink_set_routing_id (_spot, data_, size_);
    }

    ZLINK_CPP_NODISCARD int set_routing_id (const std::string &data_)
    {
        return set_routing_id (data_.data (), data_.size ());
    }

    ZLINK_CPP_NODISCARD int get_routing_id (zlink_routing_id_t *out_) const
    {
        return zlink_get_routing_id (_spot, out_);
    }

    ZLINK_CPP_NODISCARD int destroy ()
    {
        if (!_spot)
            return 0;

        void *tmp = _spot;
        const int rc = zlink_spot_destroy (&tmp);
        if (rc == 0)
            _spot = NULL;
        return rc;
    }

    void *handle () const { return _spot; }

  private:
    void *_spot;
    int _last_error;
};

} // namespace service
} // namespace zlink

#endif
