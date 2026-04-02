/* SPDX-License-Identifier: MPL-2.0 */
#ifndef ZLINK_CPP_BASE_SOCKET_HPP_INCLUDED
#define ZLINK_CPP_BASE_SOCKET_HPP_INCLUDED

#include "context.hpp"
#include "message.hpp"
#include "monitor.hpp"
#include "service_monitor.hpp"
#include "socket_handle.hpp"
#include "types.hpp"

#include <cerrno>
#include <cstring>
#include <string>
#include <type_traits>
#include <vector>

namespace zlink
{

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

    return assign_parts_from_native (native_parts, native_part_count, parts_);
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

} // namespace detail

class base_socket_t : public socket_handle_t
{
  public:
    bool valid () const noexcept { return socket_handle_t::valid (); }

    ZLINK_CPP_NODISCARD int bind (const std::string &endpoint_)
    {
        return zlink_bind (handle (), endpoint_.c_str ());
    }

    ZLINK_CPP_NODISCARD int connect (const std::string &endpoint_)
    {
        return zlink_connect (handle (), endpoint_.c_str ());
    }

    ZLINK_CPP_NODISCARD int unbind (const std::string &endpoint_)
    {
        return zlink_unbind (handle (), endpoint_.c_str ());
    }

    ZLINK_CPP_NODISCARD int disconnect (const std::string &endpoint_)
    {
        return zlink_disconnect (handle (), endpoint_.c_str ());
    }

    monitor_handle_t
    monitor_handle (monitor_event events_ = monitor_event::all) const
    {
        zlink_socket_monitor_open_options_t options;
        options.events =
          static_cast<zlink_socket_monitor_event_mask_t> (events_);
        return monitor_handle_t (
          zlink_socket_monitor_open (const_cast<void *> (handle ()), &options));
    }

    service_monitor_handle_t
    service_monitor_handle (
      service_monitor_event events_ = service_monitor_event::all) const
    {
        return service_monitor_handle_t (const_cast<void *> (handle ()), events_);
    }

    ZLINK_CPP_NODISCARD int
    set_option (socket_option_key_t<std::string> key_,
                const std::string &value_)
    {
        return set_option (key_.option, value_);
    }

    template<typename T>
    ZLINK_CPP_NODISCARD int set_option (socket_option_key_t<T> key_,
                                        const T &value_)
    {
        return set_option (key_.option, value_);
    }

    ZLINK_CPP_NODISCARD int
    get_option (socket_option_key_t<std::string> key_,
                std::string &value_) const
    {
        return get_option (key_.option, value_);
    }

    template<typename T>
    ZLINK_CPP_NODISCARD int get_option (socket_option_key_t<T> key_,
                                        T *value_) const
    {
        return get_option (key_.option, value_);
    }

    ZLINK_CPP_NODISCARD int set_tls_server (const std::string &cert_,
                                            const std::string &key_,
                                            bool require_client_cert_ = false)
    {
        return zlink_set_tls_server (
          handle (), cert_.c_str (), key_.c_str (),
          require_client_cert_ ? 1 : 0);
    }

    ZLINK_CPP_NODISCARD int set_tls_client (const std::string &ca_cert_,
                                            const std::string &hostname_,
                                            bool trust_system_ = false)
    {
        const char *ca = ca_cert_.empty () ? NULL : ca_cert_.c_str ();
        const char *hostname =
          hostname_.empty () ? NULL : hostname_.c_str ();
        return zlink_set_tls_client (
          handle (), ca, hostname, trust_system_ ? 1 : 0);
    }

  protected:
    template<typename DiscoveryT>
    ZLINK_CPP_NODISCARD int attach_discovery (DiscoveryT &discovery_)
    {
        return zlink_socket_attach_discovery (handle (), discovery_.handle ());
    }

    base_socket_t () noexcept {}

    base_socket_t (context_t &ctx_, socket_type type_)
        : socket_handle_t (
            zlink_socket (ctx_.handle (),
                          static_cast<zlink_socket_type_t> (type_)),
            true)
    {
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
          handle (), native_parts.empty () ? NULL : &native_parts[0],
          native_parts.size (), static_cast<zlink_send_flags_t> (flags_));
        if (rc != 0)
            detail::restore_parts_from_native (parts_, native_parts);
        return rc;
    }

    ZLINK_CPP_NODISCARD int send (const routing_id_t &target_rid_,
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
    send (const routing_id_t &target_rid_,
          std::vector<message_t> &parts_,
          send_flag flags_ = send_flag::none)
    {
        std::vector<zlink_msg_t> native_parts;
        if (detail::move_parts_to_native (parts_, native_parts) != 0)
            return -1;

        const int rc = zlink_send_rid (
          handle (), routing_id_native (target_rid_),
          native_parts.empty () ? NULL : &native_parts[0],
          native_parts.size (), static_cast<zlink_send_flags_t> (flags_));
        if (rc != 0)
            detail::restore_parts_from_native (parts_, native_parts);
        return rc;
    }

    ZLINK_CPP_NODISCARD int try_send (send_result_t &result_,
                                      message_t &part_)
    {
        std::vector<message_t> parts (1);
        parts[0] = std::move (part_);
        const int rc = try_send (result_, parts);
        if (rc != 0 || result_ != send_result_t::sent)
            part_ = std::move (parts[0]);
        return rc;
    }

    ZLINK_CPP_NODISCARD int
    try_send (send_result_t &result_, std::vector<message_t> &parts_)
    {
        std::vector<zlink_msg_t> native_parts;
        if (detail::move_parts_to_native (parts_, native_parts) != 0)
            return -1;

        zlink_send_result_t native_result = ZLINK_SEND_RESULT_SENT;
        const int rc = zlink_try_send (
          handle (), native_parts.empty () ? NULL : &native_parts[0],
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

    ZLINK_CPP_NODISCARD int
    try_send (send_result_t &result_,
              const routing_id_t &target_rid_,
              message_t &part_)
    {
        std::vector<message_t> parts (1);
        parts[0] = std::move (part_);
        const int rc = try_send (result_, target_rid_, parts);
        if (rc != 0 || result_ != send_result_t::sent)
            part_ = std::move (parts[0]);
        return rc;
    }

    ZLINK_CPP_NODISCARD int
    try_send (send_result_t &result_,
              const routing_id_t &target_rid_,
              std::vector<message_t> &parts_)
    {
        std::vector<zlink_msg_t> native_parts;
        if (detail::move_parts_to_native (parts_, native_parts) != 0)
            return -1;

        zlink_send_result_t native_result = ZLINK_SEND_RESULT_SENT;
        const int rc = zlink_try_send_rid (
          handle (), routing_id_native (target_rid_),
          native_parts.empty () ? NULL : &native_parts[0],
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

    ZLINK_CPP_NODISCARD int
    receive (received_t &received_, recv_flag flags_ = recv_flag::none)
    {
        received_.routing_id = routing_id_t ();
        return detail::recv_parts (
          handle (), routing_id_native (received_.routing_id), flags_,
          received_.parts);
    }

    ZLINK_CPP_NODISCARD int publish (const std::string &topic_id_,
                                     message_t &part_,
                                     send_flag flags_ = send_flag::none)
    {
        validate_no_embedded_null (topic_id_, "topic");
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
        validate_no_embedded_null (topic_id_, "topic");
        std::vector<zlink_msg_t> native_parts;
        if (detail::move_parts_to_native (parts_, native_parts) != 0)
            return -1;

        const int rc = zlink_publish (
          handle (), topic_id_.c_str (),
          native_parts.empty () ? NULL : &native_parts[0], native_parts.size (),
          static_cast<zlink_send_flags_t> (flags_));
        if (rc != 0)
            detail::restore_parts_from_native (parts_, native_parts);
        return rc;
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
          handle (), topic_id_.c_str (),
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
        validate_no_embedded_null (filter_, "filter");
        return zlink_set_subscription (handle (), filter_.c_str ());
    }

    ZLINK_CPP_NODISCARD int unset_subscription (const std::string &filter_)
    {
        validate_no_embedded_null (filter_, "filter");
        return zlink_unset_subscription (handle (), filter_.c_str ());
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
              handle (), index_, buffer.data (), &size, &pattern);
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

    ZLINK_CPP_NODISCARD int
    subscribe (subscribed_t &subscribed_, recv_flag flags_ = recv_flag::none)
    {
        subscribed_.routing_id = routing_id_t ();
        subscribed_.topic.clear ();
        return subscribe (
          subscribed_.routing_id, subscribed_.topic, subscribed_.parts, flags_);
    }

    ZLINK_CPP_NODISCARD int
    subscribe (routing_id_t &source_rid_out_,
               std::string &topic_id_out_,
               std::vector<message_t> &parts_out_,
               recv_flag flags_ = recv_flag::none)
    {
        std::vector<char> topic_buffer (256);
        zlink_msg_t *parts_native = NULL;
        size_t part_count = 0;
        size_t topic_size = topic_buffer.size ();
        const int rc = zlink_subscribe (
          handle (), routing_id_native (source_rid_out_), &parts_native,
          &part_count,
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
    subscription_event (subscription_event_t &event_,
                        recv_flag flags_ = recv_flag::none)
    {
        event_.routing_id = routing_id_t ();
        event_.topic.clear ();
        event_.subscribed = false;
        return subscription_event (
          event_.routing_id, event_.subscribed, event_.topic, flags_);
    }

    ZLINK_CPP_NODISCARD int
    subscription_event (routing_id_t &source_rid_out_,
                        bool &subscribed_out_,
                        std::string &topic_id_out_,
                        recv_flag flags_ = recv_flag::none)
    {
        std::vector<char> topic_buffer (256);
        size_t topic_size = topic_buffer.size ();
        int subscribed = 0;
        const int rc = zlink_subscription_event (
          handle (), routing_id_native (source_rid_out_), &subscribed,
          topic_buffer.data (),
          &topic_size, static_cast<zlink_send_flags_t> (flags_));
        if (rc != 0)
            return rc;

        const size_t bounded_topic =
          topic_size <= topic_buffer.size () ? topic_size : topic_buffer.size ();
        topic_id_out_.assign (topic_buffer.data (), bounded_topic);
        subscribed_out_ = subscribed != 0;
        return 0;
    }

    ZLINK_CPP_NODISCARD int on_receive (zlink_socket_msg_handler_fn handler_,
                                        void *userdata_ = NULL)
    {
        return zlink_recv_handler (handle (), handler_, userdata_);
    }

    ZLINK_CPP_NODISCARD int
    on_subscribe (zlink_subscribe_handler_fn handler_, void *userdata_ = NULL)
    {
        return zlink_subscribe_handler (handle (), handler_, userdata_);
    }

    ZLINK_CPP_NODISCARD int
    on_send_ready (zlink_send_ready_handler_fn handler_,
                   void *userdata_ = NULL)
    {
        return zlink_send_ready_handler (handle (), handler_, userdata_);
    }

  protected:
    ZLINK_CPP_NODISCARD int
    set_option (socket_option option_, const void *value_, size_t size_)
    {
        return zlink_set_option (
          handle (), static_cast<zlink_option_t> (option_), value_, size_);
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

    ZLINK_CPP_NODISCARD int
    get_option (socket_option option_, void *value_, size_t *size_) const
    {
        return zlink_get_option (
          const_cast<void *> (handle ()),
          static_cast<zlink_option_t> (option_), value_, size_);
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

    ZLINK_CPP_NODISCARD int
    get_option (socket_option option_, std::string &value_) const
    {
        return detail::get_string_option (
          [](void *socket_, socket_option option_, void *value_, size_t *size_) {
              return zlink_get_option (
                socket_, static_cast<zlink_option_t> (option_), value_, size_);
          },
          const_cast<void *> (handle ()), option_,
          option_ == socket_option::last_endpoint ? 1024u : 512u, value_);
    }

    ZLINK_CPP_NODISCARD int set_routing_id_raw (const void *data_, size_t size_)
    {
        return zlink_set_routing_id (handle (), data_, size_);
    }

    ZLINK_CPP_NODISCARD int set_routing_id_raw (const std::string &routing_id_)
    {
        return set_routing_id_raw (routing_id_.data (), routing_id_.size ());
    }

    ZLINK_CPP_NODISCARD int
    get_routing_id_raw (routing_id_t &routing_id_) const
    {
        return zlink_get_routing_id (
          const_cast<void *> (handle ()), routing_id_native (routing_id_));
    }

    ZLINK_CPP_NODISCARD int get_routing_id_raw (std::string &routing_id_) const
    {
        routing_id_t native_rid;
        if (get_routing_id_raw (native_rid) != 0)
            return -1;
        routing_id_ = routing_id_to_string (native_rid);
        return 0;
    }

    ZLINK_CPP_NODISCARD int
    set_router_option (router_option option_, const void *value_, size_t size_)
    {
        return zlink_set_router_option (
          handle (), static_cast<zlink_router_option_t> (option_), value_, size_);
    }

    template<typename T>
    ZLINK_CPP_NODISCARD int set_router_option (router_option option_,
                                               const T &value_)
    {
        return set_router_option (option_, &value_, sizeof (value_));
    }

    ZLINK_CPP_NODISCARD int
    set_router_option (router_option_key_t<std::string> key_,
                       const std::string &value_)
    {
        return set_router_option (key_.option, value_.data (), value_.size ());
    }

    template<typename T>
    ZLINK_CPP_NODISCARD int set_router_option (router_option_key_t<T> key_,
                                               const T &value_)
    {
        return set_router_option (key_.option, value_);
    }

    ZLINK_CPP_NODISCARD int
    get_router_option (router_option option_, void *value_, size_t *size_) const
    {
        return zlink_get_router_option (
          const_cast<void *> (handle ()),
          static_cast<zlink_router_option_t> (option_), value_, size_);
    }

    template<typename T>
    ZLINK_CPP_NODISCARD int
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
          zlink_get_router_option, const_cast<void *> (handle ()),
          static_cast<zlink_router_option_t> (option_), 256u, value_);
    }

    ZLINK_CPP_NODISCARD int
    get_router_option (router_option_key_t<std::string> key_,
                       std::string &value_) const
    {
        return get_router_option (key_.option, value_);
    }

    template<typename T>
    ZLINK_CPP_NODISCARD int
    get_router_option (router_option_key_t<T> key_, T *value_) const
    {
        return get_router_option (key_.option, value_);
    }

    ZLINK_CPP_NODISCARD int
    set_dealer_option (dealer_option option_, const void *value_, size_t size_)
    {
        return zlink_set_dealer_option (
          handle (), static_cast<zlink_dealer_option_t> (option_), value_, size_);
    }

    template<typename T>
    ZLINK_CPP_NODISCARD int set_dealer_option (dealer_option option_,
                                               const T &value_)
    {
        return set_dealer_option (option_, &value_, sizeof (value_));
    }

    ZLINK_CPP_NODISCARD int
    set_dealer_option (dealer_option_key_t<std::string> key_,
                       const std::string &value_)
    {
        return set_dealer_option (key_.option, value_.data (), value_.size ());
    }

    template<typename T>
    ZLINK_CPP_NODISCARD int set_dealer_option (dealer_option_key_t<T> key_,
                                               const T &value_)
    {
        return set_dealer_option (key_.option, value_);
    }

    ZLINK_CPP_NODISCARD int
    set_pub_option (pub_option option_, const void *value_, size_t size_)
    {
        return zlink_set_pub_option (
          handle (), static_cast<zlink_pub_option_t> (option_), value_, size_);
    }

    template<typename T>
    ZLINK_CPP_NODISCARD int set_pub_option (pub_option option_,
                                            const T &value_)
    {
        return set_pub_option (option_, &value_, sizeof (value_));
    }

    ZLINK_CPP_NODISCARD int
    set_pub_option (pub_option_key_t<std::string> key_,
                    const std::string &value_)
    {
        return set_pub_option (key_.option, value_.data (), value_.size ());
    }

    template<typename T>
    ZLINK_CPP_NODISCARD int set_pub_option (pub_option_key_t<T> key_,
                                            const T &value_)
    {
        return set_pub_option (key_.option, value_);
    }

    ZLINK_CPP_NODISCARD int
    get_pub_option (pub_option option_, void *value_, size_t *size_) const
    {
        return zlink_get_pub_option (
          const_cast<void *> (handle ()),
          static_cast<zlink_pub_option_t> (option_), value_, size_);
    }

    template<typename T>
    ZLINK_CPP_NODISCARD int get_pub_option (pub_option option_, T *value_) const
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
          zlink_get_pub_option, const_cast<void *> (handle ()),
          static_cast<zlink_pub_option_t> (option_), 256u, value_);
    }

    ZLINK_CPP_NODISCARD int
    get_pub_option (pub_option_key_t<std::string> key_,
                    std::string &value_) const
    {
        return get_pub_option (key_.option, value_);
    }

    template<typename T>
    ZLINK_CPP_NODISCARD int get_pub_option (pub_option_key_t<T> key_,
                                            T *value_) const
    {
        return get_pub_option (key_.option, value_);
    }

    ZLINK_CPP_NODISCARD int
    set_sub_option (sub_option option_, const void *value_, size_t size_)
    {
        return zlink_set_sub_option (
          handle (), static_cast<zlink_sub_option_t> (option_), value_, size_);
    }

    template<typename T>
    ZLINK_CPP_NODISCARD int set_sub_option (sub_option option_, const T &value_)
    {
        return set_sub_option (option_, &value_, sizeof (value_));
    }

    template<typename T>
    ZLINK_CPP_NODISCARD int set_sub_option (sub_option_key_t<T> key_,
                                            const T &value_)
    {
        return set_sub_option (key_.option, value_);
    }

    ZLINK_CPP_NODISCARD int
    get_sub_option (sub_option option_, void *value_, size_t *size_) const
    {
        return zlink_get_sub_option (
          const_cast<void *> (handle ()),
          static_cast<zlink_sub_option_t> (option_), value_, size_);
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

    template<typename T>
    ZLINK_CPP_NODISCARD int get_sub_option (sub_option_key_t<T> key_,
                                            T *value_) const
    {
        return get_sub_option (key_.option, value_);
    }

    ZLINK_CPP_NODISCARD int
    set_stream_option (stream_option option_, const void *value_, size_t size_)
    {
        return zlink_set_stream_option (
          handle (), static_cast<zlink_stream_option_t> (option_), value_, size_);
    }

    template<typename T>
    ZLINK_CPP_NODISCARD int set_stream_option (stream_option option_,
                                               const T &value_)
    {
        return set_stream_option (option_, &value_, sizeof (value_));
    }

    template<typename T>
    ZLINK_CPP_NODISCARD int set_stream_option (stream_option_key_t<T> key_,
                                               const T &value_)
    {
        return set_stream_option (key_.option, value_);
    }

    ZLINK_CPP_NODISCARD int
    get_stream_option (stream_option option_, void *value_, size_t *size_) const
    {
        return zlink_get_stream_option (
          const_cast<void *> (handle ()),
          static_cast<zlink_stream_option_t> (option_), value_, size_);
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

    template<typename T>
    ZLINK_CPP_NODISCARD int
    get_stream_option (stream_option_key_t<T> key_, T *value_) const
    {
        return get_stream_option (key_.option, value_);
    }
};

} // namespace zlink

#endif
