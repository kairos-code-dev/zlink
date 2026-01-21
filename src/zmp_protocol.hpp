/* SPDX-License-Identifier: MPL-2.0 */

#ifndef __ZMQ_ZMP_PROTOCOL_HPP_INCLUDED__
#define __ZMQ_ZMP_PROTOCOL_HPP_INCLUDED__

#include <cstdlib>
#include <cstring>
#include <stdint.h>

namespace zmq
{
// ZMP protocol constants (v1)
enum
{
    zmp_magic = 0x5A,
    zmp_version = 0x02,
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
    zmp_control_heartbeat_ack = 0x03,
    zmp_control_ready = 0x04,
    zmp_control_error = 0x05
};

enum
{
    zmp_error_invalid_magic = 0x01,
    zmp_error_version_mismatch = 0x02,
    zmp_error_flags_invalid = 0x03,
    zmp_error_body_too_large = 0x04,
    zmp_error_socket_type_mismatch = 0x05,
    zmp_error_handshake_timeout = 0x06,
    zmp_error_internal = 0x7F
};

const uint32_t zmp_max_body_size = 0xFFFFFFFFu;

inline const char *zmp_error_reason (uint8_t code_)
{
    switch (code_) {
        case zmp_error_invalid_magic:
            return "invalid magic";
        case zmp_error_version_mismatch:
            return "version mismatch";
        case zmp_error_flags_invalid:
            return "flags invalid";
        case zmp_error_body_too_large:
            return "body too large";
        case zmp_error_socket_type_mismatch:
            return "socket type mismatch";
        case zmp_error_handshake_timeout:
            return "handshake timeout";
        default:
            return "internal";
    }
}

inline uint16_t zmp_effective_ttl_ds (uint16_t local_ttl_ds_,
                                      uint16_t remote_ttl_ds_)
{
    if (remote_ttl_ds_ == 0)
        return 0;
    if (local_ttl_ds_ == 0)
        return remote_ttl_ds_;
    return local_ttl_ds_ < remote_ttl_ds_ ? local_ttl_ds_ : remote_ttl_ds_;
}

inline bool zmp_protocol_enabled ()
{
    const char *env = std::getenv ("ZLINK_PROTOCOL");
    if (!env || *env == '\0')
        return true;
    return std::strcmp (env, "zmp") == 0 || std::strcmp (env, "ZMP") == 0;
}
}

#endif
