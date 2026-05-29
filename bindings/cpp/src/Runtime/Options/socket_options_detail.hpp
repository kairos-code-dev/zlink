/* SPDX-License-Identifier: MPL-2.0 */
#ifndef ZLINK_CPP_RUNTIME_OPTIONS_SOCKET_OPTIONS_DETAIL_HPP_INCLUDED
#define ZLINK_CPP_RUNTIME_OPTIONS_SOCKET_OPTIONS_DETAIL_HPP_INCLUDED

#include <zlink/Contracts/Errors/errors.hpp>
#include <zlink/Contracts/Sockets/socket_options.hpp>
#include <Runtime/Core/duration_conversion.hpp>
#include <Runtime/Core/routing_id_access.hpp>
#include <Runtime/Sockets/socket_access.hpp>
#include <Runtime/Options/option_ids.hpp>

#include <zlink.h>

#include <cerrno>
#include <cstring>
#include <optional>
#include <string>
#include <vector>

namespace zlink
{
namespace detail
{

inline void ensure_config_handle (void *handle_)
{
    if (!handle_)
        throw config_error_t (config_result_t::invalid_handle, EINVAL);
}

inline void *native_option_handle (socket_t *socket_)
{
    if (!socket_)
        throw config_error_t (config_result_t::invalid_handle, EINVAL);
    return native_handle (*socket_);
}

template <typename T, typename NativeOption, typename Getter>
inline T get_option_value (void *handle_, NativeOption option_, Getter getter_)
{
    ensure_config_handle (handle_);
    T value{};
    size_t size = sizeof (value);
    detail::throw_if_failed<config_error_t> (
      static_cast<config_result_t> (getter_ (handle_, option_, &value, &size)));
    return value;
}

template <typename T, typename NativeOption, typename Setter>
inline void set_option_value (void *handle_,
                              NativeOption option_,
                              const T &value_,
                              Setter setter_)
{
    ensure_config_handle (handle_);
    detail::throw_if_failed<config_error_t> (static_cast<config_result_t> (
      setter_ (handle_, option_, &value_, sizeof (value_))));
}

template <typename NativeOption, typename Getter>
std::string get_option_string_value (void *handle_,
                                     NativeOption option_,
                                     size_t initial_cap_,
                                     Getter getter_)
{
    ensure_config_handle (handle_);
    size_t cap = initial_cap_;
    const size_t max_cap = 64u * 1024u;
    while (cap <= max_cap) {
        std::vector<char> buffer (cap);
        size_t size = cap;
        const config_result_t result = static_cast<config_result_t> (
          getter_ (handle_, option_, buffer.data (), &size));
        if (result == config_result_t::ok) {
            const size_t bounded =
              size <= buffer.size () ? size : buffer.size ();
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

template <typename T>
T get_common_option_value (void *handle_, socket_option_id option_)
{
    return get_option_value<T> (handle_, static_cast<zlink_option_t> (option_),
                                zlink_get_option);
}

template <typename T>
inline void set_common_option_value (void *handle_,
                                     socket_option_id option_,
                                     const T &value_)
{
    set_option_value<T> (handle_, static_cast<zlink_option_t> (option_), value_,
                         zlink_set_option);
}

inline std::string get_common_option_string (void *handle_,
                                             socket_option_id option_)
{
    const size_t cap =
      option_ == detail::socket_option_id::last_endpoint ? 1024u : 256u;
    return get_option_string_value (
      handle_, static_cast<zlink_option_t> (option_), cap, zlink_get_option);
}

template <typename T>
T get_router_option_value (void *handle_, router_option_id option_)
{
    return get_option_value<T> (handle_,
                                static_cast<zlink_router_option_t> (option_),
                                zlink_get_router_option);
}

template <typename T>
inline void set_router_option_value (void *handle_,
                                     router_option_id option_,
                                     const T &value_)
{
    set_option_value<T> (handle_, static_cast<zlink_router_option_t> (option_),
                         value_, zlink_set_router_option);
}

template <typename T>
T get_dealer_option_value (void *handle_, dealer_option_id option_)
{
    return get_option_value<T> (handle_,
                                static_cast<zlink_dealer_option_t> (option_),
                                zlink_get_dealer_option);
}

template <typename T>
inline void set_dealer_option_value (void *handle_,
                                     dealer_option_id option_,
                                     const T &value_)
{
    set_option_value<T> (handle_, static_cast<zlink_dealer_option_t> (option_),
                         value_, zlink_set_dealer_option);
}

template <typename T>
T get_pub_option_value (void *handle_, pub_option_id option_)
{
    return get_option_value<T> (
      handle_, static_cast<zlink_pub_option_t> (option_), zlink_get_pub_option);
}

template <typename T>
inline void
set_pub_option_value (void *handle_, pub_option_id option_, const T &value_)
{
    set_option_value<T> (handle_, static_cast<zlink_pub_option_t> (option_),
                         value_, zlink_set_pub_option);
}

inline std::string get_pub_option_string (void *handle_, pub_option_id option_)
{
    return get_option_string_value (handle_,
                                    static_cast<zlink_pub_option_t> (option_),
                                    256u, zlink_get_pub_option);
}

template <typename T>
T get_sub_option_value (void *handle_, sub_option_id option_)
{
    return get_option_value<T> (
      handle_, static_cast<zlink_sub_option_t> (option_), zlink_get_sub_option);
}

template <typename T>
T get_stream_option_value (void *handle_, stream_option_id option_)
{
    return get_option_value<T> (handle_,
                                static_cast<zlink_stream_option_t> (option_),
                                zlink_get_stream_option);
}

template <typename T>
inline void set_stream_option_value (void *handle_,
                                     stream_option_id option_,
                                     const T &value_)
{
    set_option_value<T> (handle_, static_cast<zlink_stream_option_t> (option_),
                         value_, zlink_set_stream_option);
}

} // namespace detail
} // namespace zlink

#endif
