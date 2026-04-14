/* SPDX-License-Identifier: MPL-2.0 */
#ifndef ZLINK_CPP_TYPES_IMPL_HPP_INCLUDED
#define ZLINK_CPP_TYPES_IMPL_HPP_INCLUDED

namespace zlink
{
namespace detail
{

inline void ensure_config_handle (void *handle_)
{
    if (!handle_)
        throw config_error_t (config_result_t::invalid_handle, EINVAL);
}

inline std::unordered_map<void *, int> &dealer_option_store ()
{
    static std::unordered_map<void *, int> values;
    return values;
}

template<typename T>
inline T get_common_option_value (void *handle_, socket_option option_)
{
    ensure_config_handle (handle_);
    T value {};
    size_t size = sizeof (value);
    detail::throw_if_failed<config_error_t> (
      static_cast<config_result_t> (
        zlink_get_option (
          handle_, static_cast<zlink_option_t> (option_), &value, &size)));
    return value;
}

template<typename T>
inline void set_common_option_value (void *handle_,
                                     socket_option option_,
                                     const T &value_)
{
    ensure_config_handle (handle_);
    detail::throw_if_failed<config_error_t> (
      static_cast<config_result_t> (
        zlink_set_option (
          handle_, static_cast<zlink_option_t> (option_), &value_,
          sizeof (value_))));
}

inline std::string get_common_option_string (void *handle_, socket_option option_)
{
    ensure_config_handle (handle_);
    size_t cap = option_ == socket_option::last_endpoint ? 1024u : 256u;
    const size_t max_cap = 64u * 1024u;
    while (cap <= max_cap) {
        std::vector<char> buffer (cap);
        size_t size = cap;
        const config_result_t result = static_cast<config_result_t> (
          zlink_get_option (
            handle_, static_cast<zlink_option_t> (option_), buffer.data (),
            &size));
        if (result == config_result_t::ok) {
            const size_t bounded = size <= buffer.size () ? size : buffer.size ();
            size_t out_size = bounded;
            if (out_size > 0 && buffer[out_size - 1] == '\0')
                --out_size;
            return std::string (buffer.data (), out_size);
        }
        if (errno != EINVAL || cap == max_cap)
            throw config_error_t (result, zlink_errno ());
        cap *= 2u;
        if (cap > max_cap)
            cap = max_cap;
    }
    throw config_error_t (config_result_t::invalid_argument, EINVAL);
}

template<typename T>
inline T get_router_option_value (void *handle_, router_option option_)
{
    ensure_config_handle (handle_);
    T value {};
    size_t size = sizeof (value);
    detail::throw_if_failed<config_error_t> (
      static_cast<config_result_t> (
        zlink_get_router_option (
          handle_, static_cast<zlink_router_option_t> (option_), &value,
          &size)));
    return value;
}

template<typename T>
inline void set_router_option_value (void *handle_,
                                     router_option option_,
                                     const T &value_)
{
    ensure_config_handle (handle_);
    detail::throw_if_failed<config_error_t> (
      static_cast<config_result_t> (
        zlink_set_router_option (
          handle_, static_cast<zlink_router_option_t> (option_), &value_,
          sizeof (value_))));
}

template<typename T>
inline T get_dealer_option_value (void *handle_, dealer_option option_)
{
    ensure_config_handle (handle_);
    typename std::unordered_map<void *, int>::const_iterator it =
      dealer_option_store ().find (handle_);
    if (it == dealer_option_store ().end ())
        return T ();
    return static_cast<T> (it->second);
}

template<typename T>
inline void set_dealer_option_value (void *handle_,
                                     dealer_option option_,
                                     const T &value_)
{
    ensure_config_handle (handle_);
    detail::throw_if_failed<config_error_t> (
      static_cast<config_result_t> (
        zlink_set_dealer_option (
          handle_, static_cast<zlink_dealer_option_t> (option_), &value_,
          sizeof (value_))));
    dealer_option_store ()[handle_] = static_cast<int> (value_);
}

template<typename T>
inline T get_pub_option_value (void *handle_, pub_option option_)
{
    ensure_config_handle (handle_);
    T value {};
    size_t size = sizeof (value);
    detail::throw_if_failed<config_error_t> (
      static_cast<config_result_t> (
        zlink_get_pub_option (
          handle_, static_cast<zlink_pub_option_t> (option_), &value, &size)));
    return value;
}

template<typename T>
inline void set_pub_option_value (void *handle_,
                                  pub_option option_,
                                  const T &value_)
{
    ensure_config_handle (handle_);
    detail::throw_if_failed<config_error_t> (
      static_cast<config_result_t> (
        zlink_set_pub_option (
          handle_, static_cast<zlink_pub_option_t> (option_), &value_,
          sizeof (value_))));
}

template<typename T>
inline T get_sub_option_value (void *handle_, sub_option option_)
{
    ensure_config_handle (handle_);
    T value {};
    size_t size = sizeof (value);
    detail::throw_if_failed<config_error_t> (
      static_cast<config_result_t> (
        zlink_get_sub_option (
          handle_, static_cast<zlink_sub_option_t> (option_), &value, &size)));
    return value;
}

template<typename T>
inline T get_stream_option_value (void *handle_, stream_option option_)
{
    ensure_config_handle (handle_);
    T value {};
    size_t size = sizeof (value);
    throw_if_failed<config_error_t> (
      static_cast<config_result_t> (
        zlink_get_stream_option (
          handle_, static_cast<zlink_stream_option_t> (option_), &value,
          &size)));
    return value;
}

template<typename T>
inline void set_stream_option_value (void *handle_,
                                     stream_option option_,
                                     const T &value_)
{
    ensure_config_handle (handle_);
    throw_if_failed<config_error_t> (
      static_cast<config_result_t> (
        zlink_set_stream_option (
          handle_, static_cast<zlink_stream_option_t> (option_), &value_,
          sizeof (value_))));
}

inline recv_error_t invalid_single_part_error ()
{
    return recv_error_t (recv_result_t::not_supported, EMSGSIZE);
}

inline void close_parts (std::vector<message_t> &parts_)
{
    for (std::vector<message_t>::iterator it = parts_.begin ();
         it != parts_.end (); ++it)
        it->close ();
    parts_.clear ();
}

} // namespace detail

inline int common_socket_options_t::linger () const
{
    return detail::get_common_option_value<int> (_handle, socket_option::linger);
}

inline void common_socket_options_t::linger (int value)
{
    detail::set_common_option_value<int> (_handle, socket_option::linger, value);
}

inline int common_socket_options_t::send_hwm () const
{
    return detail::get_common_option_value<int> (_handle, socket_option::sndhwm);
}

inline void common_socket_options_t::send_hwm (int value)
{
    detail::set_common_option_value<int> (_handle, socket_option::sndhwm, value);
}

inline int common_socket_options_t::recv_hwm () const
{
    return detail::get_common_option_value<int> (_handle, socket_option::rcvhwm);
}

inline void common_socket_options_t::recv_hwm (int value)
{
    detail::set_common_option_value<int> (_handle, socket_option::rcvhwm, value);
}

inline int common_socket_options_t::send_timeout () const
{
    return detail::get_common_option_value<int> (
      _handle, socket_option::sndtimeo);
}

inline void common_socket_options_t::send_timeout (int value)
{
    detail::set_common_option_value<int> (
      _handle, socket_option::sndtimeo, value);
}

inline int common_socket_options_t::recv_timeout () const
{
    return detail::get_common_option_value<int> (
      _handle, socket_option::rcvtimeo);
}

inline void common_socket_options_t::recv_timeout (int value)
{
    detail::set_common_option_value<int> (
      _handle, socket_option::rcvtimeo, value);
}

inline bool common_socket_options_t::immediate () const
{
    return detail::get_common_option_value<int> (
             _handle, socket_option::immediate)
           != 0;
}

inline void common_socket_options_t::immediate (bool value)
{
    detail::set_common_option_value<int> (
      _handle, socket_option::immediate, value ? 1 : 0);
}

inline int common_socket_options_t::connect_timeout () const
{
    return detail::get_common_option_value<int> (
      _handle, socket_option::connect_timeout);
}

inline void common_socket_options_t::connect_timeout (int value)
{
    detail::set_common_option_value<int> (
      _handle, socket_option::connect_timeout, value);
}

inline bool common_socket_options_t::ipv6 () const
{
    return detail::get_common_option_value<int> (_handle, socket_option::ipv6)
           != 0;
}

inline void common_socket_options_t::ipv6 (bool value)
{
    detail::set_common_option_value<int> (
      _handle, socket_option::ipv6, value ? 1 : 0);
}

inline bool common_socket_options_t::tcp_no_delay () const
{
    return detail::get_common_option_value<int> (
             _handle, socket_option::tcp_nodelay)
           != 0;
}

inline void common_socket_options_t::tcp_no_delay (bool value)
{
    detail::set_common_option_value<int> (
      _handle, socket_option::tcp_nodelay, value ? 1 : 0);
}

inline bool common_socket_options_t::tcp_keepalive () const
{
    return detail::get_common_option_value<int> (
             _handle, socket_option::tcp_keepalive)
           != 0;
}

inline void common_socket_options_t::tcp_keepalive (bool value)
{
    detail::set_common_option_value<int> (
      _handle, socket_option::tcp_keepalive, value ? 1 : 0);
}

inline int common_socket_options_t::heartbeat_interval () const
{
    return detail::get_common_option_value<int> (
      _handle, socket_option::heartbeat_ivl);
}

inline void common_socket_options_t::heartbeat_interval (int value)
{
    detail::set_common_option_value<int> (
      _handle, socket_option::heartbeat_ivl, value);
}

inline int common_socket_options_t::heartbeat_ttl () const
{
    return detail::get_common_option_value<int> (
      _handle, socket_option::heartbeat_ttl);
}

inline void common_socket_options_t::heartbeat_ttl (int value)
{
    detail::set_common_option_value<int> (
      _handle, socket_option::heartbeat_ttl, value);
}

inline int common_socket_options_t::heartbeat_timeout () const
{
    return detail::get_common_option_value<int> (
      _handle, socket_option::heartbeat_timeout);
}

inline void common_socket_options_t::heartbeat_timeout (int value)
{
    detail::set_common_option_value<int> (
      _handle, socket_option::heartbeat_timeout, value);
}

inline int64_t common_socket_options_t::max_message_size () const
{
    return detail::get_common_option_value<int64_t> (
      _handle, socket_option::maxmsgsize);
}

inline void common_socket_options_t::max_message_size (int64_t value)
{
    detail::set_common_option_value<int64_t> (
      _handle, socket_option::maxmsgsize, value);
}

inline int common_socket_options_t::backlog () const
{
    return detail::get_common_option_value<int> (_handle, socket_option::backlog);
}

inline void common_socket_options_t::backlog (int value)
{
    detail::set_common_option_value<int> (_handle, socket_option::backlog, value);
}

inline int common_socket_options_t::reconnect_interval () const
{
    return detail::get_common_option_value<int> (
      _handle, socket_option::reconnect_ivl);
}

inline void common_socket_options_t::reconnect_interval (int value)
{
    detail::set_common_option_value<int> (
      _handle, socket_option::reconnect_ivl, value);
}

inline int common_socket_options_t::reconnect_interval_max () const
{
    return detail::get_common_option_value<int> (
      _handle, socket_option::reconnect_ivl_max);
}

inline void common_socket_options_t::reconnect_interval_max (int value)
{
    detail::set_common_option_value<int> (
      _handle, socket_option::reconnect_ivl_max, value);
}

inline std::string common_socket_options_t::last_endpoint () const
{
    return detail::get_common_option_string (_handle, socket_option::last_endpoint);
}

inline bool router_socket_options_t::mandatory () const
{
    return detail::get_router_option_value<int> (_handle, router_option::mandatory)
           != 0;
}

inline void router_socket_options_t::mandatory (bool value)
{
    detail::set_router_option_value<int> (
      _handle, router_option::mandatory, value ? 1 : 0);
}

inline bool router_socket_options_t::handover () const
{
    return detail::get_router_option_value<int> (_handle, router_option::handover)
           != 0;
}

inline void router_socket_options_t::handover (bool value)
{
    detail::set_router_option_value<int> (
      _handle, router_option::handover, value ? 1 : 0);
}

inline bool router_socket_options_t::probe_router () const
{
    return detail::get_router_option_value<int> (_handle, router_option::probe)
           != 0;
}

inline void router_socket_options_t::probe_router (bool value)
{
    detail::set_router_option_value<int> (
      _handle, router_option::probe, value ? 1 : 0);
}

inline std::optional<routing_id_t> router_socket_options_t::connect_routing_id () const
{
    detail::ensure_config_handle (_handle);
    zlink_routing_id_t native;
    std::memset (&native, 0, sizeof (native));
    size_t size = sizeof (native);
    detail::throw_if_failed<config_error_t> (
      static_cast<config_result_t> (
        zlink_get_router_option (
          _handle,
          static_cast<zlink_router_option_t> (router_option::connect_routing_id),
          &native, &size)));
    if (native.size == 0)
        return std::nullopt;
    return routing_id_t (native);
}

inline void
router_socket_options_t::connect_routing_id (const routing_id_t &value)
{
    detail::ensure_config_handle (_handle);
    const zlink_routing_id_t native = value.native ();
    detail::throw_if_failed<config_error_t> (
      static_cast<config_result_t> (
        zlink_set_router_option (
          _handle,
          static_cast<zlink_router_option_t> (router_option::connect_routing_id),
          native.data, native.size)));
}

inline bool dealer_socket_options_t::probe_router () const
{
    return detail::get_dealer_option_value<int> (_handle, dealer_option::probe)
           != 0;
}

inline void dealer_socket_options_t::probe_router (bool value)
{
    detail::set_dealer_option_value<int> (
      _handle, dealer_option::probe, value ? 1 : 0);
}

inline bool stream_socket_options_t::notify () const
{
    return detail::get_stream_option_value<int> (_handle, stream_option::notify)
           != 0;
}

inline void stream_socket_options_t::notify (bool value)
{
    detail::set_stream_option_value<int> (
      _handle, stream_option::notify, value ? 1 : 0);
}

inline bool pub_socket_options_t::verbose () const
{
    return detail::get_pub_option_value<int> (_handle, pub_option::verbose) != 0;
}

inline void pub_socket_options_t::verbose (bool value)
{
    detail::set_pub_option_value<int> (
      _handle, pub_option::verbose, value ? 1 : 0);
}

inline bool pub_socket_options_t::verboser () const
{
    return detail::get_pub_option_value<int> (_handle, pub_option::verboser) != 0;
}

inline void pub_socket_options_t::verboser (bool value)
{
    detail::set_pub_option_value<int> (
      _handle, pub_option::verboser, value ? 1 : 0);
}

inline bool pub_socket_options_t::no_drop () const
{
    return detail::get_pub_option_value<int> (_handle, pub_option::nodrop) != 0;
}

inline void pub_socket_options_t::no_drop (bool value)
{
    detail::set_pub_option_value<int> (
      _handle, pub_option::nodrop, value ? 1 : 0);
}

inline bool pub_socket_options_t::manual () const
{
    return detail::get_pub_option_value<int> (_handle, pub_option::manual) != 0;
}

inline void pub_socket_options_t::manual (bool value)
{
    detail::set_pub_option_value<int> (
      _handle, pub_option::manual, value ? 1 : 0);
}

inline int sub_socket_options_t::topics_count () const
{
    return detail::get_sub_option_value<int> (_handle, sub_option::topics_count);
}

inline message_t &received_t::first_part ()
{
    if (!is_single_part ())
        throw detail::invalid_single_part_error ();
    return _parts.front ();
}

inline message_t topic_message_t::single_part_or_throw ()
{
    if (!is_single_part ())
        throw detail::invalid_single_part_error ();
    return _parts.front ();
}

inline message_t &topic_message_t::first_part ()
{
    if (!is_single_part ())
        throw detail::invalid_single_part_error ();
    return _parts.front ();
}

inline message_t received_t::single_part_or_throw ()
{
    if (!is_single_part ())
        throw detail::invalid_single_part_error ();
    return _parts.front ();
}

inline void received_t::reply (message_t &part) const
{
    reply (part, send_flags_t::none);
}

inline void received_t::reply (message_t &part, send_flags_t flags) const
{
    std::vector<message_t> parts;
    parts.push_back (std::move (part));
    reply (parts, flags);
    if (!parts.empty ())
        part = std::move (parts.front ());
}

inline void received_t::reply (std::vector<message_t> &parts) const
{
    reply (parts, send_flags_t::none);
}

inline void received_t::reply (std::vector<message_t> &parts, send_flags_t flags) const
{
    if (!_reply_fn)
        throw submit_error_t (submit_result_t::invalid_argument, EINVAL);
    _reply_fn (parts, flags);
}

inline void received_t::close ()
{
    detail::close_parts (_parts);
}

inline void topic_message_t::close ()
{
    detail::close_parts (_parts);
}

} // namespace zlink

#endif
