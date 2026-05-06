/* SPDX-License-Identifier: MPL-2.0 */
#ifndef ZLINK_CPP_CODEC_JSON_HPP_INCLUDED
#define ZLINK_CPP_CODEC_JSON_HPP_INCLUDED

#include "../message.hpp"

namespace zlink
{
namespace codec
{
namespace json
{

template<class T> T decode (const message_t &message_);
template<class T> message_t encode (const T &value_);
template<class T> T parse (const message_t &message_);
template<class T> message_t to_message (const T &value_);

} // namespace json
} // namespace codec
} // namespace zlink

#endif
