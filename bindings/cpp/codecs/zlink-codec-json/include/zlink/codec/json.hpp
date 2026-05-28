/* SPDX-License-Identifier: MPL-2.0 */
#ifndef ZLINK_CPP_CODEC_JSON_HPP_INCLUDED
#define ZLINK_CPP_CODEC_JSON_HPP_INCLUDED

#include <zlink/Contracts/Messaging/message.hpp>

#include <nlohmann/json.hpp>

#include <string>

namespace zlink::codec::json
{

template<typename T>
T decode (const message_t &msg)
{
    const auto *begin = reinterpret_cast<const char *> (msg.data ());
    const auto *end = begin ? begin + msg.size () : begin;
    return nlohmann::json::parse (begin, end).template get<T> ();
}

template<typename T>
message_t encode (const T &value)
{
    const auto json = nlohmann::json (value);
    const auto text = json.dump ();
    return message_t::from_bytes (
      std::as_bytes (std::span<const char> (text.data (), text.size ())));
}

template<typename T>
T parse (const message_t &msg)
{
    return decode<T> (msg);
}

template<typename T>
message_t to_message (const T &value)
{
    return encode (value);
}

} // namespace zlink::codec::json

#endif
