/* SPDX-License-Identifier: MPL-2.0 */
#ifndef ZLINK_CPP_CODEC_PROTO_HPP_INCLUDED
#define ZLINK_CPP_CODEC_PROTO_HPP_INCLUDED

#include "../message.hpp"

namespace zlink
{
namespace codec
{
namespace proto
{

template<class T> T decode (const message_t &message_);
template<class T> message_t encode (const T &value_);
template<class T> T parse (const message_t &message_);
template<class T> message_t to_message (const T &value_);

} // namespace proto
namespace protobuf = proto;
} // namespace codec
} // namespace zlink

#endif
