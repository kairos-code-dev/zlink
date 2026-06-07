/* SPDX-License-Identifier: MPL-2.0 */

#ifndef ZLINK_SPOT_NODE_ROUTER_CHANNEL_ARG_HPP_INCLUDED
#define ZLINK_SPOT_NODE_ROUTER_CHANNEL_ARG_HPP_INCLUDED

#include "zlink.h"

#include <string>

namespace zlink
{
namespace spot_node_router_channel_arg
{

inline char hex_digit (unsigned char value_)
{
    return static_cast<char> (value_ < 10 ? '0' + value_ : 'a' + value_ - 10);
}

inline std::string routing_id_hex (const zlink_routing_id_t &rid_)
{
    std::string hex;
    hex.reserve (static_cast<size_t> (rid_.size) * 2);
    for (uint8_t i = 0; i < rid_.size; ++i) {
        const unsigned char value = rid_.data[i];
        hex.push_back (hex_digit (static_cast<unsigned char> (value >> 4)));
        hex.push_back (hex_digit (static_cast<unsigned char> (value & 0x0f)));
    }
    return hex;
}

inline std::string from_endpoint (const std::string &channel_name_, const std::string &endpoint_)
{
    return channel_name_ + "\n" + endpoint_;
}

inline std::string from_routing_id (const std::string &channel_name_,
                                    const zlink_routing_id_t &peer_rid_,
                                    const std::string &endpoint_)
{
    return channel_name_ + "\n" + routing_id_hex (peer_rid_) + "\n" + endpoint_;
}

}
}

#endif
