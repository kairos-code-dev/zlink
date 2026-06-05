/* SPDX-License-Identifier: MPL-2.0 */
#pragma once

#include <zlink/Contracts/Messaging/message.hpp>

#include <concepts>
#include <string>
#include <type_traits>
#include <typeinfo>
#include <utility>

namespace zlink::stream_connector::detail
{

template <typename T> concept static_packet_name = requires
{
    {
        T::packet_name
    } -> std::convertible_to<const char *>;
};

template <typename T> std::string message_packet_name ()
{
    if constexpr (static_packet_name<T>) {
        return T::packet_name;
    } else {
        return typeid (T).name ();
    }
}

template <typename TMessage>
auto to_packet_payload (const TMessage &message, int) -> decltype (to_stream_payload (message), zlink::message_t{})
{
    auto payload = to_stream_payload (message);
    if constexpr (std::is_same_v<decltype (payload), zlink::message_t>) {
        return payload;
    } else {
        return zlink::message_t::from (std::move (payload));
    }
}

template <typename TMessage> zlink::message_t to_packet_payload (const TMessage &, long)
{
    return zlink::message_t::from (std::string ("{}"));
}

template <typename TMessage>
auto apply_packet_payload (TMessage &message,
                           const zlink::message_t &payload,
                           int) -> decltype (from_stream_payload (payload, message), void ())
{
    from_stream_payload (payload, message);
}

template <typename TMessage> void apply_packet_payload (TMessage &, const zlink::message_t &, long)
{
}

} // namespace zlink::stream_connector::detail
