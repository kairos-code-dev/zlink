/* SPDX-License-Identifier: MPL-2.0 */
#ifndef ZLINK_CPP_CODEC_PROTO_HPP_INCLUDED
#define ZLINK_CPP_CODEC_PROTO_HPP_INCLUDED

#include <zlink/Contracts/Messaging/message.hpp>

#include <google/protobuf/message.h>

#include <limits>
#include <stdexcept>
#include <type_traits>

namespace zlink::codec::proto
{

template<typename T>
T decode (const message_t &msg)
{
    static_assert (
      std::is_base_of<::google::protobuf::Message, T>::value,
      "T must derive from google::protobuf::Message");

    T value;
    if (!value.ParseFromArray (
          msg.data (), static_cast<int> (msg.size ()))) {
        throw std::runtime_error ("failed to decode protobuf payload");
    }
    return value;
}

template<typename T>
message_t encode (const T &value)
{
    static_assert (
      std::is_base_of<::google::protobuf::Message, T>::value,
      "T must derive from google::protobuf::Message");

    const auto size = value.ByteSizeLong ();
    if (size > static_cast<size_t> (std::numeric_limits<int>::max ())) {
        throw std::overflow_error ("protobuf payload exceeds message_t limits");
    }

    message_t msg (size);
    if (!msg.valid ()) {
        throw std::runtime_error ("failed to allocate zlink message");
    }

    if (!value.SerializeToArray (msg.data (), static_cast<int> (size))) {
        throw std::runtime_error ("failed to encode protobuf payload");
    }

    return msg;
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

} // namespace zlink::codec::proto

namespace zlink::codec
{
namespace protobuf = proto;
}

#endif
