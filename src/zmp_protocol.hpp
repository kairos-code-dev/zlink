/* SPDX-License-Identifier: MPL-2.0 */

#ifndef __ZMQ_ZMP_PROTOCOL_HPP_INCLUDED__
#define __ZMQ_ZMP_PROTOCOL_HPP_INCLUDED__

#include <cstdlib>
#include <cstring>
#include <stdint.h>

namespace zmq
{
// ZMP protocol constants (v0)
enum
{
    zmp_magic = 0x5A,
    zmp_version = 0x01,
    zmp_header_size = 8,
    zmp_flag_more = 0x01,
    zmp_flag_control = 0x02,
    zmp_flag_identity = 0x04,
    zmp_flag_subscribe = 0x08,
    zmp_flag_cancel = 0x10,
    zmp_flag_mask = zmp_flag_more | zmp_flag_control | zmp_flag_identity
                    | zmp_flag_subscribe | zmp_flag_cancel
};

enum
{
    zmp_control_hello = 0x01,
    zmp_control_heartbeat = 0x02,
    zmp_control_heartbeat_ack = 0x03
};

const uint32_t zmp_max_body_size = 256u * 1024u * 1024u;

inline bool zmp_protocol_enabled ()
{
    const char *env = std::getenv ("ZLINK_PROTOCOL");
    if (!env)
        return false;
    return std::strcmp (env, "zmp") == 0 || std::strcmp (env, "ZMP") == 0;
}
}

#endif
