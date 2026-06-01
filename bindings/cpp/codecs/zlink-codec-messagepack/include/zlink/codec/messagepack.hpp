/* SPDX-License-Identifier: MPL-2.0 */
#ifndef ZLINK_CPP_CODEC_MESSAGEPACK_HPP_INCLUDED
#define ZLINK_CPP_CODEC_MESSAGEPACK_HPP_INCLUDED

#include <zlink/Contracts/Messaging/message.hpp>

#include <msgpack.hpp>

namespace zlink::codec::messagepack
{

template<typename T>
T decode (const message_t &msg)
{
    const auto handle =
      msgpack::unpack (reinterpret_cast<const char *> (msg.data ()),
                       msg.size ());
    return handle.get ().template as<T> ();
}

template<typename T>
message_t encode (const T &value)
{
    msgpack::sbuffer buffer;
    msgpack::pack (buffer, value);
    return message_t::from (
      std::as_bytes (std::span<const char> (buffer.data (), buffer.size ())));
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

} // namespace zlink::codec::messagepack

#endif
