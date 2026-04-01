/* SPDX-License-Identifier: MPL-2.0 */
#ifndef ZLINK_CPP_SOCKET_HPP_INCLUDED
#define ZLINK_CPP_SOCKET_HPP_INCLUDED

#include "context.hpp"
#include "message.hpp"
#include "types.hpp"

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
class discovery_t;
}

namespace detail
{

inline void close_message_array (zlink_msg_t *parts_, size_t part_count_) noexcept
{
    if (!parts_)
        return;
    zlink_multipart_close (parts_, part_count_);
}

inline bool is_common_string_option (socket_option option_) noexcept
{
    switch (option_) {
    case socket_option::last_endpoint:
    case socket_option::bindtodevice:
    case socket_option::tls_cert:
    case socket_option::tls_key:
    case socket_option::tls_ca:
    case socket_option::tls_hostname:
    case socket_option::tls_password:
        return true;
    default:
        return false;
    }
}

inline size_t initial_common_string_capacity (socket_option option_) noexcept
{
    switch (option_) {
    case socket_option::last_endpoint:
        return 1024;
    default:
        return 512;
    }
}

template<typename Getter, typename Option>
inline int get_string_option (Getter getter_,
                              void *handle_,
                              Option option_,
                              size_t initial_capacity_,
                              std::string &value_)
{
    size_t cap = initial_capacity_;
    const size_t max_cap = 64u * 1024u;

    while (cap <= max_cap) {
        std::vector<char> buffer (cap);
        size_t size = cap;
        const int rc = getter_ (handle_, option_, buffer.data (), &size);
        if (rc == 0) {
            const size_t bounded = size <= buffer.size () ? size : buffer.size ();
            size_t out_size = bounded;
            if (out_size > 0 && buffer[out_size - 1] == '\0')
                --out_size;
            value_.assign (buffer.data (), out_size);
            return 0;
        }

        if (errno != EINVAL || cap == max_cap)
            return -1;

        cap *= 2u;
        if (cap > max_cap)
            cap = max_cap;
    }

    errno = EINVAL;
    return -1;
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

inline void restore_parts_from_native (std::vector<message_t> &parts_,
                                       std::vector<zlink_msg_t> &native_) noexcept
{
    const size_t count =
      native_.size () < parts_.size () ? native_.size () : parts_.size ();
    for (size_t i = 0; i < count; ++i) {
        if (parts_[i].init () == 0)
            (void) zlink_msg_move (parts_[i].handle (), &native_[i]);
        (void) zlink_msg_close (&native_[i]);
    }
    native_.clear ();
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

inline int recv_parts (void *socket_,
                       zlink_routing_id_t *source_rid_out_,
                       recv_flag flags_,
                       std::vector<message_t> &parts_)
{
    zlink_msg_t *native_parts = NULL;
    size_t native_part_count = 0;
    const int rc = zlink_recv (
      socket_, source_rid_out_, &native_parts, &native_part_count,
      static_cast<zlink_send_flags_t> (flags_));
    if (rc != 0)
        return rc;

    if (assign_parts_from_native (native_parts, native_part_count, parts_) != 0)
        return -1;
    return 0;
}

inline int recv_single_part (void *socket_,
                             zlink_routing_id_t *source_rid_out_,
                             recv_flag flags_,
                             message_t &part_)
{
    std::vector<message_t> parts;
    if (recv_parts (socket_, source_rid_out_, flags_, parts) != 0)
        return -1;

    if (parts.size () != 1) {
        errno = EMSGSIZE;
        return -1;
    }

    part_ = std::move (parts[0]);
    return 0;
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

class socket_t
{
  public:
    socket_t () : _socket (NULL), _own (false) {}

    socket_t (context_t &ctx_, socket_type type_)
        : _socket (zlink_socket (ctx_.handle (),
                                 static_cast<zlink_socket_type_t> (type_))),
          _own (true)
    {
    }

    ~socket_t () { (void) close (); }

    socket_t (socket_t &&other) noexcept
        : _socket (other._socket), _own (other._own)
    {
        other._socket = NULL;
        other._own = false;
    }

    socket_t &operator= (socket_t &&other) noexcept
    {
        if (this == &other)
            return *this;

        (void) close ();
        _socket = other._socket;
        _own = other._own;
        other._socket = NULL;
        other._own = false;
        return *this;
    }

    socket_t (const socket_t &) = delete;
    socket_t &operator= (const socket_t &) = delete;

    static socket_t adopt (void *socket_)
    {
        socket_t socket;
        socket._socket = socket_;
        socket._own = true;
        return socket;
    }

    static socket_t wrap (void *socket_)
    {
        socket_t socket;
        socket._socket = socket_;
        socket._own = false;
        return socket;
    }

    bool valid () const noexcept { return _socket != NULL; }
    void *handle () noexcept { return _socket; }
    const void *handle () const noexcept { return _socket; }

    ZLINK_CPP_NODISCARD int bind (const std::string &endpoint_)
    {
        return zlink_bind (_socket, endpoint_.c_str ());
    }

    ZLINK_CPP_NODISCARD int connect (const std::string &endpoint_)
    {
        return zlink_connect (_socket, endpoint_.c_str ());
    }

    ZLINK_CPP_NODISCARD int unbind (const std::string &endpoint_)
    {
        return zlink_unbind (_socket, endpoint_.c_str ());
    }

    ZLINK_CPP_NODISCARD int disconnect (const std::string &endpoint_)
    {
        return zlink_disconnect (_socket, endpoint_.c_str ());
    }

    ZLINK_CPP_NODISCARD int close () noexcept
    {
        if (!_socket) {
            _own = false;
            return 0;
        }

        if (!_own) {
            _socket = NULL;
            return 0;
        }

        const int rc = zlink_close (_socket);
        if (rc == 0) {
            _socket = NULL;
            _own = false;
        }
        return rc;
    }

    ZLINK_CPP_NODISCARD int send (message_t &part_,
                                  send_flag flags_ = send_flag::none)
    {
        std::vector<message_t> parts (1);
        parts[0] = std::move (part_);
        const int rc = send (parts, flags_);
        if (rc != 0)
            part_ = std::move (parts[0]);
        return rc;
    }

    ZLINK_CPP_NODISCARD int send (std::vector<message_t> &parts_,
                                  send_flag flags_ = send_flag::none)
    {
        std::vector<zlink_msg_t> native_parts;
        if (detail::move_parts_to_native (parts_, native_parts) != 0)
            return -1;

        const int rc = zlink_send (
          _socket, native_parts.empty () ? NULL : &native_parts[0],
          native_parts.size (), static_cast<zlink_send_flags_t> (flags_));
        if (rc != 0)
            detail::restore_parts_from_native (parts_, native_parts);
        return rc;
    }

    ZLINK_CPP_NODISCARD send_result_t try_send (message_t &part_)
    {
        send_result_t result = send_result_t::sent;
        const int rc = try_send (result, part_);
        (void) rc;
        return result;
    }

    ZLINK_CPP_NODISCARD int try_send (send_result_t &result_, message_t &part_)
    {
        std::vector<message_t> parts (1);
        parts[0] = std::move (part_);
        const int rc = try_send (result_, parts);
        if (rc != 0 || result_ != send_result_t::sent)
            part_ = std::move (parts[0]);
        return rc;
    }

    ZLINK_CPP_NODISCARD send_result_t try_send (std::vector<message_t> &parts_)
    {
        send_result_t result = send_result_t::sent;
        const int rc = try_send (result, parts_);
        (void) rc;
        return result;
    }

    ZLINK_CPP_NODISCARD int
    try_send (send_result_t &result_, std::vector<message_t> &parts_)
    {
        std::vector<zlink_msg_t> native_parts;
        if (detail::move_parts_to_native (parts_, native_parts) != 0)
            return -1;

        zlink_send_result_t native_result = ZLINK_SEND_RESULT_SENT;
        const int rc = zlink_try_send (
          _socket, native_parts.empty () ? NULL : &native_parts[0],
          native_parts.size (), &native_result);
        if (rc == 0) {
            result_ = detail::to_send_result (native_result);
            if (native_result != ZLINK_SEND_RESULT_SENT)
                detail::restore_parts_from_native (parts_, native_parts);
            return 0;
        }

        detail::restore_parts_from_native (parts_, native_parts);
        return -1;
    }

    ZLINK_CPP_NODISCARD int send (const zlink_routing_id_t &target_rid_,
                                  message_t &part_,
                                  send_flag flags_ = send_flag::none)
    {
        std::vector<message_t> parts (1);
        parts[0] = std::move (part_);
        const int rc = send (target_rid_, parts, flags_);
        if (rc != 0)
            part_ = std::move (parts[0]);
        return rc;
    }

    ZLINK_CPP_NODISCARD int
    send (const zlink_routing_id_t &target_rid_,
          std::vector<message_t> &parts_,
          send_flag flags_ = send_flag::none)
    {
        std::vector<zlink_msg_t> native_parts;
        if (detail::move_parts_to_native (parts_, native_parts) != 0)
            return -1;

        const int rc = zlink_send_rid (
          _socket, &target_rid_, native_parts.empty () ? NULL : &native_parts[0],
          native_parts.size (), static_cast<zlink_send_flags_t> (flags_));
        if (rc != 0)
            detail::restore_parts_from_native (parts_, native_parts);
        return rc;
    }

    ZLINK_CPP_NODISCARD send_result_t try_send (
      const zlink_routing_id_t &target_rid_,
      message_t &part_)
    {
        send_result_t result = send_result_t::sent;
        const int rc = try_send (result, target_rid_, part_);
        (void) rc;
        return result;
    }

    ZLINK_CPP_NODISCARD int
    try_send (send_result_t &result_,
              const zlink_routing_id_t &target_rid_,
              message_t &part_)
    {
        std::vector<message_t> parts (1);
        parts[0] = std::move (part_);
        const int rc = try_send (result_, target_rid_, parts);
        if (rc != 0 || result_ != send_result_t::sent)
            part_ = std::move (parts[0]);
        return rc;
    }

    ZLINK_CPP_NODISCARD send_result_t
    try_send (const zlink_routing_id_t &target_rid_,
              std::vector<message_t> &parts_)
    {
        send_result_t result = send_result_t::sent;
        const int rc = try_send (result, target_rid_, parts_);
        (void) rc;
        return result;
    }

    ZLINK_CPP_NODISCARD int
    try_send (send_result_t &result_,
              const zlink_routing_id_t &target_rid_,
              std::vector<message_t> &parts_)
    {
        std::vector<zlink_msg_t> native_parts;
        if (detail::move_parts_to_native (parts_, native_parts) != 0)
            return -1;

        zlink_send_result_t native_result = ZLINK_SEND_RESULT_SENT;
        const int rc = zlink_try_send_rid (
          _socket, &target_rid_, native_parts.empty () ? NULL : &native_parts[0],
          native_parts.size (), &native_result);
        if (rc == 0) {
            result_ = detail::to_send_result (native_result);
            if (native_result != ZLINK_SEND_RESULT_SENT)
                detail::restore_parts_from_native (parts_, native_parts);
            return 0;
        }

        detail::restore_parts_from_native (parts_, native_parts);
        return -1;
    }

    ZLINK_CPP_NODISCARD int recv (message_t &part_,
                                  recv_flag flags_ = recv_flag::none)
    {
        return detail::recv_single_part (_socket, NULL, flags_, part_);
    }

    ZLINK_CPP_NODISCARD int recv (std::vector<message_t> &parts_,
                                  recv_flag flags_ = recv_flag::none)
    {
        return detail::recv_parts (_socket, NULL, flags_, parts_);
    }

    ZLINK_CPP_NODISCARD int recv (zlink_routing_id_t &source_rid_,
                                  message_t &part_,
                                  recv_flag flags_ = recv_flag::none)
    {
        std::memset (&source_rid_, 0, sizeof (source_rid_));
        return detail::recv_single_part (_socket, &source_rid_, flags_, part_);
    }

    ZLINK_CPP_NODISCARD int
    recv (zlink_routing_id_t &source_rid_,
          std::vector<message_t> &parts_,
          recv_flag flags_ = recv_flag::none)
    {
        std::memset (&source_rid_, 0, sizeof (source_rid_));
        return detail::recv_parts (_socket, &source_rid_, flags_, parts_);
    }

    ZLINK_CPP_NODISCARD int
    receive (received_t &received_, recv_flag flags_ = recv_flag::none)
    {
        received_.routing_id = empty_routing_id ();
        return detail::recv_parts (
          _socket, &received_.routing_id, flags_, received_.parts);
    }

    ZLINK_CPP_NODISCARD int publish (const std::string &topic_id_,
                                     message_t &part_,
                                     send_flag flags_ = send_flag::none)
    {
        std::vector<message_t> parts (1);
        parts[0] = std::move (part_);
        const int rc = publish (topic_id_, parts, flags_);
        if (rc != 0)
            part_ = std::move (parts[0]);
        return rc;
    }

    ZLINK_CPP_NODISCARD int publish (const std::string &topic_id_,
                                     std::vector<message_t> &parts_,
                                     send_flag flags_ = send_flag::none)
    {
        std::vector<zlink_msg_t> native_parts;
        if (detail::move_parts_to_native (parts_, native_parts) != 0)
            return -1;

        const int rc = zlink_publish (
          _socket, topic_id_.c_str (),
          native_parts.empty () ? NULL : &native_parts[0], native_parts.size (),
          static_cast<zlink_send_flags_t> (flags_));
        if (rc != 0)
            detail::restore_parts_from_native (parts_, native_parts);
        return rc;
    }

    ZLINK_CPP_NODISCARD send_result_t try_publish (const std::string &topic_id_,
                                                   message_t &part_)
    {
        send_result_t result = send_result_t::sent;
        const int rc = try_publish (result, topic_id_, part_);
        (void) rc;
        return result;
    }

    ZLINK_CPP_NODISCARD int
    try_publish (send_result_t &result_,
                 const std::string &topic_id_,
                 message_t &part_)
    {
        std::vector<message_t> parts (1);
        parts[0] = std::move (part_);
        const int rc = try_publish (result_, topic_id_, parts);
        if (rc != 0 || result_ != send_result_t::sent)
            part_ = std::move (parts[0]);
        return rc;
    }

    ZLINK_CPP_NODISCARD send_result_t
    try_publish (const std::string &topic_id_, std::vector<message_t> &parts_)
    {
        send_result_t result = send_result_t::sent;
        const int rc = try_publish (result, topic_id_, parts_);
        (void) rc;
        return result;
    }

    ZLINK_CPP_NODISCARD int
    try_publish (send_result_t &result_,
                 const std::string &topic_id_,
                 std::vector<message_t> &parts_)
    {
        std::vector<zlink_msg_t> native_parts;
        if (detail::move_parts_to_native (parts_, native_parts) != 0)
            return -1;

        zlink_send_result_t native_result = ZLINK_SEND_RESULT_SENT;
        const int rc = zlink_try_publish (
          _socket, topic_id_.c_str (),
          native_parts.empty () ? NULL : &native_parts[0], native_parts.size (),
          &native_result);
        if (rc == 0) {
            result_ = detail::to_send_result (native_result);
            if (native_result != ZLINK_SEND_RESULT_SENT)
                detail::restore_parts_from_native (parts_, native_parts);
            return 0;
        }

        detail::restore_parts_from_native (parts_, native_parts);
        return -1;
    }

    ZLINK_CPP_NODISCARD int set_subscription (const std::string &filter_)
    {
        return zlink_set_subscription (_socket, filter_.c_str ());
    }

    ZLINK_CPP_NODISCARD int unset_subscription (const std::string &filter_)
    {
        return zlink_unset_subscription (_socket, filter_.c_str ());
    }

    ZLINK_CPP_NODISCARD int
    subscription_at (size_t index_, std::string &filter_, bool *is_pattern_ = NULL)
    {
        size_t cap = 256;
        const size_t max_cap = 64u * 1024u;
        while (cap <= max_cap) {
            std::vector<char> buffer (cap);
            size_t size = cap;
            int pattern = 0;
            const int rc = zlink_subscription_at (
              _socket, index_, buffer.data (), &size, &pattern);
            if (rc == 0) {
                const size_t bounded = size <= buffer.size () ? size : buffer.size ();
                filter_.assign (buffer.data (), bounded);
                if (is_pattern_)
                    *is_pattern_ = pattern != 0;
                return 0;
            }

            if (errno != EINVAL || cap == max_cap)
                return -1;

            cap *= 2u;
            if (cap > max_cap)
                cap = max_cap;
        }

        errno = EINVAL;
        return -1;
    }

    ZLINK_CPP_NODISCARD int subscribe (std::string &topic_id_out_,
                                       message_t &part_,
                                       recv_flag flags_ = recv_flag::none)
    {
        std::vector<message_t> parts;
        const int rc = subscribe (topic_id_out_, parts, flags_);
        if (rc != 0)
            return rc;
        if (parts.size () != 1) {
            errno = EMSGSIZE;
            return -1;
        }
        part_ = std::move (parts[0]);
        return 0;
    }

    ZLINK_CPP_NODISCARD int
    subscribe (std::string &topic_id_out_,
               std::vector<message_t> &parts_out_,
               recv_flag flags_ = recv_flag::none)
    {
        zlink_routing_id_t unused_rid;
        return subscribe (unused_rid, topic_id_out_, parts_out_, flags_);
    }

    ZLINK_CPP_NODISCARD int subscribe (zlink_routing_id_t &source_rid_out_,
                                       std::string &topic_id_out_,
                                       message_t &part_,
                                       recv_flag flags_ = recv_flag::none)
    {
        std::vector<message_t> parts;
        const int rc = subscribe (source_rid_out_, topic_id_out_, parts, flags_);
        if (rc != 0)
            return rc;
        if (parts.size () != 1) {
            errno = EMSGSIZE;
            return -1;
        }
        part_ = std::move (parts[0]);
        return 0;
    }

    ZLINK_CPP_NODISCARD int
    subscribe (zlink_routing_id_t &source_rid_out_,
               std::string &topic_id_out_,
               std::vector<message_t> &parts_out_,
               recv_flag flags_ = recv_flag::none)
    {
        std::vector<char> topic_buffer (256);
        zlink_msg_t *parts_native = NULL;
        size_t part_count = 0;
        size_t topic_size = topic_buffer.size ();
        std::memset (&source_rid_out_, 0, sizeof (source_rid_out_));
        const int rc = zlink_subscribe (
          _socket, &source_rid_out_, &parts_native, &part_count,
          topic_buffer.data (), &topic_size,
          static_cast<zlink_send_flags_t> (flags_));
        if (rc != 0)
            return rc;

        const size_t bounded_topic =
          topic_size <= topic_buffer.size () ? topic_size : topic_buffer.size ();
        topic_id_out_.assign (topic_buffer.data (), bounded_topic);
        return detail::assign_parts_from_native (parts_native, part_count, parts_out_);
    }

    ZLINK_CPP_NODISCARD int
    subscribe (subscribed_t &subscribed_, recv_flag flags_ = recv_flag::none)
    {
        subscribed_.routing_id = empty_routing_id ();
        subscribed_.topic.clear ();
        return subscribe (
          subscribed_.routing_id, subscribed_.topic, subscribed_.parts, flags_);
    }

    ZLINK_CPP_NODISCARD int subscription_event (bool &subscribed_out_,
                                                std::string &topic_id_out_,
                                                recv_flag flags_ = recv_flag::none)
    {
        zlink_routing_id_t unused_rid;
        return subscription_event (
          unused_rid, subscribed_out_, topic_id_out_, flags_);
    }

    ZLINK_CPP_NODISCARD int
    subscription_event (zlink_routing_id_t &source_rid_out_,
                        bool &subscribed_out_,
                        std::string &topic_id_out_,
                        recv_flag flags_ = recv_flag::none)
    {
        std::vector<char> topic_buffer (256);
        size_t topic_size = topic_buffer.size ();
        int subscribed = 0;
        std::memset (&source_rid_out_, 0, sizeof (source_rid_out_));
        const int rc = zlink_subscription_event (
          _socket, &source_rid_out_, &subscribed, topic_buffer.data (),
          &topic_size, static_cast<zlink_send_flags_t> (flags_));
        if (rc != 0)
            return rc;

        const size_t bounded_topic =
          topic_size <= topic_buffer.size () ? topic_size : topic_buffer.size ();
        topic_id_out_.assign (topic_buffer.data (), bounded_topic);
        subscribed_out_ = subscribed != 0;
        return 0;
    }

    ZLINK_CPP_NODISCARD int
    subscription_event (subscription_event_t &event_,
                        recv_flag flags_ = recv_flag::none)
    {
        event_.routing_id = empty_routing_id ();
        event_.topic.clear ();
        event_.subscribed = false;
        return subscription_event (
          event_.routing_id, event_.subscribed, event_.topic, flags_);
    }

    ZLINK_CPP_NODISCARD int recv_handler (zlink_socket_msg_handler_fn handler_,
                                          void *userdata_ = NULL)
    {
        return zlink_recv_handler (_socket, handler_, userdata_);
    }

    ZLINK_CPP_NODISCARD int
    subscribe_handler (zlink_subscribe_handler_fn handler_, void *userdata_ = NULL)
    {
        return zlink_subscribe_handler (_socket, handler_, userdata_);
    }

    ZLINK_CPP_NODISCARD int
    send_ready_handler (zlink_send_ready_handler_fn handler_,
                        void *userdata_ = NULL)
    {
        return zlink_send_ready_handler (_socket, handler_, userdata_);
    }

    ZLINK_CPP_NODISCARD int
    set_option (socket_option option_, const void *value_, size_t size_)
    {
        return zlink_set_option (
          _socket, static_cast<zlink_option_t> (option_), value_, size_);
    }

    template<typename T>
    ZLINK_CPP_NODISCARD
    typename std::enable_if<!std::is_same<T, std::string>::value, int>::type
    set_option (socket_option option_, const T &value_)
    {
        return set_option (option_, &value_, sizeof (value_));
    }

    ZLINK_CPP_NODISCARD int
    set_option (socket_option option_, const std::string &value_)
    {
        if (!detail::is_common_string_option (option_)) {
            errno = EINVAL;
            return -1;
        }
        return set_option (option_, value_.data (), value_.size ());
    }

    template<typename T>
    ZLINK_CPP_NODISCARD
    typename std::enable_if<!std::is_same<T, std::string>::value, int>::type
    set_option (socket_option_key_t<T> key_, const T &value_)
    {
        return set_option (key_.option, value_);
    }

    ZLINK_CPP_NODISCARD int
    set_option (socket_option_key_t<std::string> key_, const std::string &value_)
    {
        return set_option (key_.option, value_);
    }

    ZLINK_CPP_NODISCARD int
    get_option (socket_option option_, void *value_, size_t *size_) const
    {
        return zlink_get_option (
          _socket, static_cast<zlink_option_t> (option_), value_, size_);
    }

    template<typename T>
    ZLINK_CPP_NODISCARD
    typename std::enable_if<!std::is_same<T, std::string>::value, int>::type
    get_option (socket_option option_, T *value_) const
    {
        if (!value_) {
            errno = EINVAL;
            return -1;
        }
        size_t size = sizeof (*value_);
        return get_option (option_, value_, &size);
    }

    template<typename T>
    ZLINK_CPP_NODISCARD
    typename std::enable_if<!std::is_same<T, std::string>::value, int>::type
    get_option (socket_option_key_t<T> key_, T *value_) const
    {
        return get_option (key_.option, value_);
    }

    ZLINK_CPP_NODISCARD int
    get_option (socket_option option_, std::string &value_) const
    {
        if (!detail::is_common_string_option (option_)) {
            errno = EINVAL;
            return -1;
        }
        return detail::get_string_option (
          zlink_get_option, _socket, static_cast<zlink_option_t> (option_),
          detail::initial_common_string_capacity (option_), value_);
    }

    ZLINK_CPP_NODISCARD int
    get_option (socket_option_key_t<std::string> key_, std::string &value_) const
    {
        return get_option (key_.option, value_);
    }

    ZLINK_CPP_NODISCARD int
    set_router_option (router_option option_, const void *value_, size_t size_)
    {
        return zlink_set_router_option (
          _socket, static_cast<zlink_router_option_t> (option_), value_, size_);
    }

    template<typename T>
    ZLINK_CPP_NODISCARD
    typename std::enable_if<!std::is_same<T, std::string>::value, int>::type
    set_router_option (router_option option_, const T &value_)
    {
        return set_router_option (option_, &value_, sizeof (value_));
    }

    ZLINK_CPP_NODISCARD int
    set_router_option (router_option option_, const std::string &value_)
    {
        return set_router_option (option_, value_.data (), value_.size ());
    }

    ZLINK_CPP_NODISCARD int
    get_router_option (router_option option_, void *value_, size_t *size_) const
    {
        return zlink_get_router_option (
          _socket, static_cast<zlink_router_option_t> (option_), value_, size_);
    }

    template<typename T>
    ZLINK_CPP_NODISCARD
    typename std::enable_if<!std::is_same<T, std::string>::value, int>::type
    get_router_option (router_option option_, T *value_) const
    {
        if (!value_) {
            errno = EINVAL;
            return -1;
        }
        size_t size = sizeof (*value_);
        return get_router_option (option_, value_, &size);
    }

    ZLINK_CPP_NODISCARD int
    get_router_option (router_option option_, std::string &value_) const
    {
        return detail::get_string_option (
          zlink_get_router_option, _socket,
          static_cast<zlink_router_option_t> (option_), 256, value_);
    }

    ZLINK_CPP_NODISCARD int
    set_dealer_option (dealer_option option_, const void *value_, size_t size_)
    {
        return zlink_set_dealer_option (
          _socket, static_cast<zlink_dealer_option_t> (option_), value_, size_);
    }

    template<typename T>
    ZLINK_CPP_NODISCARD int set_dealer_option (dealer_option option_,
                                               const T &value_)
    {
        return set_dealer_option (option_, &value_, sizeof (value_));
    }

    ZLINK_CPP_NODISCARD int
    set_pub_option (pub_option option_, const void *value_, size_t size_)
    {
        return zlink_set_pub_option (
          _socket, static_cast<zlink_pub_option_t> (option_), value_, size_);
    }

    template<typename T>
    ZLINK_CPP_NODISCARD
    typename std::enable_if<!std::is_same<T, std::string>::value, int>::type
    set_pub_option (pub_option option_, const T &value_)
    {
        return set_pub_option (option_, &value_, sizeof (value_));
    }

    ZLINK_CPP_NODISCARD int
    set_pub_option (pub_option option_, const std::string &value_)
    {
        return set_pub_option (option_, value_.data (), value_.size ());
    }

    ZLINK_CPP_NODISCARD int
    get_pub_option (pub_option option_, void *value_, size_t *size_) const
    {
        return zlink_get_pub_option (
          _socket, static_cast<zlink_pub_option_t> (option_), value_, size_);
    }

    template<typename T>
    ZLINK_CPP_NODISCARD
    typename std::enable_if<!std::is_same<T, std::string>::value, int>::type
    get_pub_option (pub_option option_, T *value_) const
    {
        if (!value_) {
            errno = EINVAL;
            return -1;
        }
        size_t size = sizeof (*value_);
        return get_pub_option (option_, value_, &size);
    }

    ZLINK_CPP_NODISCARD int
    get_pub_option (pub_option option_, std::string &value_) const
    {
        return detail::get_string_option (
          zlink_get_pub_option, _socket, static_cast<zlink_pub_option_t> (option_),
          256, value_);
    }

    ZLINK_CPP_NODISCARD int
    set_sub_option (sub_option option_, const void *value_, size_t size_)
    {
        return zlink_set_sub_option (
          _socket, static_cast<zlink_sub_option_t> (option_), value_, size_);
    }

    template<typename T>
    ZLINK_CPP_NODISCARD int set_sub_option (sub_option option_, const T &value_)
    {
        return set_sub_option (option_, &value_, sizeof (value_));
    }

    ZLINK_CPP_NODISCARD int
    get_sub_option (sub_option option_, void *value_, size_t *size_) const
    {
        return zlink_get_sub_option (
          _socket, static_cast<zlink_sub_option_t> (option_), value_, size_);
    }

    template<typename T>
    ZLINK_CPP_NODISCARD int get_sub_option (sub_option option_, T *value_) const
    {
        if (!value_) {
            errno = EINVAL;
            return -1;
        }
        size_t size = sizeof (*value_);
        return get_sub_option (option_, value_, &size);
    }

    ZLINK_CPP_NODISCARD int
    set_stream_option (stream_option option_, const void *value_, size_t size_)
    {
        return zlink_set_stream_option (
          _socket, static_cast<zlink_stream_option_t> (option_), value_, size_);
    }

    template<typename T>
    ZLINK_CPP_NODISCARD int set_stream_option (stream_option option_,
                                               const T &value_)
    {
        return set_stream_option (option_, &value_, sizeof (value_));
    }

    ZLINK_CPP_NODISCARD int
    get_stream_option (stream_option option_, void *value_, size_t *size_) const
    {
        return zlink_get_stream_option (
          _socket, static_cast<zlink_stream_option_t> (option_), value_, size_);
    }

    template<typename T>
    ZLINK_CPP_NODISCARD int
    get_stream_option (stream_option option_, T *value_) const
    {
        if (!value_) {
            errno = EINVAL;
            return -1;
        }
        size_t size = sizeof (*value_);
        return get_stream_option (option_, value_, &size);
    }

    ZLINK_CPP_NODISCARD int set_routing_id (const void *data_, size_t size_)
    {
        return zlink_set_routing_id (_socket, data_, size_);
    }

    ZLINK_CPP_NODISCARD int set_routing_id (const std::string &routing_id_)
    {
        return set_routing_id (routing_id_.data (), routing_id_.size ());
    }

    ZLINK_CPP_NODISCARD int get_routing_id (zlink_routing_id_t &routing_id_) const
    {
        std::memset (&routing_id_, 0, sizeof (routing_id_));
        return zlink_get_routing_id (_socket, &routing_id_);
    }

    ZLINK_CPP_NODISCARD int get_routing_id (std::string &routing_id_) const
    {
        zlink_routing_id_t native_rid;
        if (get_routing_id (native_rid) != 0)
            return -1;
        routing_id_ = routing_id_to_string (native_rid);
        return 0;
    }

    ZLINK_CPP_NODISCARD int set_tls_server (const std::string &cert_,
                                            const std::string &key_,
                                            bool require_client_cert_ = false)
    {
        return zlink_set_tls_server (
          _socket, cert_.c_str (), key_.c_str (), require_client_cert_ ? 1 : 0);
    }

    ZLINK_CPP_NODISCARD int set_tls_client (const std::string &ca_cert_,
                                            const std::string &hostname_,
                                            bool trust_system_ = false)
    {
        const char *ca_cert = ca_cert_.empty () ? NULL : ca_cert_.c_str ();
        const char *hostname = hostname_.empty () ? NULL : hostname_.c_str ();
        return zlink_set_tls_client (
          _socket, ca_cert, hostname, trust_system_ ? 1 : 0);
    }

    template<typename DiscoveryT>
    ZLINK_CPP_NODISCARD int attach_discovery (DiscoveryT &discovery_)
    {
        return zlink_socket_attach_discovery (_socket, discovery_.handle ());
    }

  private:
    void *_socket;
    bool _own;
};

} // namespace detail
} // namespace zlink

#endif
