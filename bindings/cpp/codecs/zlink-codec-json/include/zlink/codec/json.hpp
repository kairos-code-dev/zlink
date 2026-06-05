/* SPDX-License-Identifier: MPL-2.0 */
#ifndef ZLINK_CPP_CODEC_JSON_HPP_INCLUDED
#define ZLINK_CPP_CODEC_JSON_HPP_INCLUDED

#include <zlink/Contracts/Messaging/message.hpp>

#include <nlohmann/json.hpp>

#include <string>

namespace zlink::codec::json
{

template <typename T> T parse_message (const message_t &msg)
{
    const auto *begin = reinterpret_cast<const char *> (msg.data ());
    const auto *end = begin ? begin + msg.size () : begin;
    return nlohmann::json::parse (begin, end).template get<T> ();
}

template <typename T> message_t make_message (const T &value)
{
    const auto json = nlohmann::json (value);
    const auto text = json.dump ();
    return message_t::from (std::as_bytes (std::span<const char> (text.data (), text.size ())));
}

template <typename T> T decode (const message_t &msg)
{
    return msg.template parse_json<T> ();
}

template <typename T> message_t encode (const T &value)
{
    return message_t::from_json (value);
}

template <typename T> T parse (const message_t &msg)
{
    return msg.template parse_json<T> ();
}

template <typename T> message_t to_message (const T &value)
{
    return message_t::from_json (value);
}

} // namespace zlink::codec::json

namespace zlink
{

template <typename T> message_t message_t::from_json (const T &value_)
{
    return codec::json::make_message (value_);
}

template <typename T> T message_t::parse_json () const
{
    return codec::json::parse_message<T> (*this);
}

} // namespace zlink

#endif
