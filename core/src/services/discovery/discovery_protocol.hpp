/* SPDX-License-Identifier: MPL-2.0 */

#ifndef __ZLINK_DISCOVERY_PROTOCOL_HPP_INCLUDED__
#define __ZLINK_DISCOVERY_PROTOCOL_HPP_INCLUDED__

#include "core/internal_defs.hpp"
#include "core/send_internal.hpp"
#include "core/msg.hpp"
#include "utils/err.hpp"
#include "utils/stdint.hpp"

#include <string>

namespace zlink
{
namespace discovery_protocol
{
static const uint16_t msg_register = 0x0001;
static const uint16_t msg_register_ack = 0x0002;
static const uint16_t msg_unregister = 0x0003;
static const uint16_t msg_heartbeat = 0x0004;
static const uint16_t msg_service_list = 0x0005;
static const uint16_t msg_registry_sync = 0x0006;
static const uint16_t msg_update_weight = 0x0007;
static const uint16_t msg_bootstrap_req = 0x0008;
static const uint16_t msg_bootstrap_rep = 0x0009;
static const uint16_t msg_topology_report = 0x000A;
static const uint16_t msg_topology_query = 0x000B;
static const uint16_t msg_topology_reply = 0x000C;
static const uint16_t msg_unregister_ack = 0x000D;
static const uint16_t msg_gateway_peer_report = 0x000E;
static const uint16_t msg_gateway_peer_query = 0x000F;
static const uint16_t msg_gateway_peer_reply = 0x0010;

static const uint16_t service_type_gateway_receiver = 1;
static const uint16_t service_type_spot_node = 2;
static const uint16_t service_type_socket = 3;

enum service_role_t
{
    service_role_invalid = 0,
    service_role_gateway = 1,
    service_role_spot = 2,
    service_role_router = 3,
    service_role_dealer = 4,
    service_role_pub = 5,
    service_role_sub = 6
};

inline bool is_valid_service_type (uint16_t service_type_)
{
    return service_type_ == service_type_gateway_receiver
           || service_type_ == service_type_spot_node
           || service_type_ == service_type_socket;
}

inline bool is_valid_service_role (uint16_t service_role_)
{
    return service_role_ == service_role_gateway
           || service_role_ == service_role_spot
           || service_role_ == service_role_router
           || service_role_ == service_role_dealer
           || service_role_ == service_role_pub
           || service_role_ == service_role_sub;
}

inline uint16_t fixed_service_role_for_type (uint16_t service_type_)
{
    if (service_type_ == service_type_gateway_receiver)
        return service_role_gateway;
    if (service_type_ == service_type_spot_node)
        return service_role_spot;
    return service_role_invalid;
}

inline bool is_valid_service_role_for_type (uint16_t service_type_,
                                            uint16_t service_role_)
{
    if (!is_valid_service_type (service_type_)
        || !is_valid_service_role (service_role_)) {
        return false;
    }

    if (service_type_ == service_type_gateway_receiver)
        return service_role_ == service_role_gateway;
    if (service_type_ == service_type_spot_node)
        return service_role_ == service_role_spot;

    return service_role_ == service_role_router
           || service_role_ == service_role_dealer
           || service_role_ == service_role_pub
           || service_role_ == service_role_sub;
}

inline uint16_t derive_socket_service_role (int socket_type_)
{
    if (socket_type_ == ZLINK_CORE_SOCKET_ROUTER)
        return service_role_router;
    if (socket_type_ == ZLINK_CORE_SOCKET_DEALER)
        return service_role_dealer;
    if (socket_type_ == ZLINK_CORE_SOCKET_PUB)
        return service_role_pub;
    if (socket_type_ == ZLINK_CORE_SOCKET_SUB)
        return service_role_sub;
    return service_role_invalid;
}

inline bool service_roles_match (uint16_t local_role_, uint16_t remote_role_)
{
    if (!is_valid_service_role (local_role_)
        || !is_valid_service_role (remote_role_)) {
        return false;
    }

    if (local_role_ == service_role_gateway)
        return remote_role_ == service_role_gateway;
    if (local_role_ == service_role_spot)
        return remote_role_ == service_role_spot;
    if (local_role_ == service_role_pub)
        return remote_role_ == service_role_sub;
    if (local_role_ == service_role_sub)
        return remote_role_ == service_role_pub;

    return remote_role_ == service_role_router
           || remote_role_ == service_role_dealer;
}

struct bootstrap_req_t
{
    uint16_t msg_id;
    uint16_t service_type;
    zlink_routing_id_t routing_id;
};

struct bootstrap_rep_t
{
    uint16_t msg_id;
    uint16_t reserved;
    uint32_t heartbeat_interval_ms;
    uint32_t registry_id;
    uint32_t feature_flags;
    char pub_endpoint[256];
    char uplink_endpoint[256];
};

inline int send_frame (void *socket_, const void *data_, size_t size_, int flags_)
{
    zlink_msg_t msg;
    if (zlink_msg_init_size (&msg, size_) != 0)
        return -1;
    if (size_ > 0 && data_)
        memcpy (zlink_msg_data (&msg), data_, size_);
    const int rc = zlink::send_msg_internal (socket_, &msg, flags_);
    if (rc == -1)
        zlink_msg_close (&msg);
    return rc;
}

inline int send_u16 (void *socket_, uint16_t value_, int flags_)
{
    return send_frame (socket_, &value_, sizeof (value_), flags_);
}

inline int send_u32 (void *socket_, uint32_t value_, int flags_)
{
    return send_frame (socket_, &value_, sizeof (value_), flags_);
}

inline int send_u64 (void *socket_, uint64_t value_, int flags_)
{
    return send_frame (socket_, &value_, sizeof (value_), flags_);
}

inline int send_string (void *socket_, const std::string &value_, int flags_)
{
    return send_frame (socket_, value_.empty () ? NULL : value_.data (),
                       value_.size (), flags_);
}

inline int send_routing_id (void *socket_, const zlink_routing_id_t &rid_,
                            int flags_)
{
    return send_frame (socket_, rid_.size ? rid_.data : NULL, rid_.size, flags_);
}

inline bool read_u16 (const zlink_msg_t &msg_, uint16_t *out_)
{
    if (!out_)
        return false;
    if (zlink_msg_size (&msg_) != sizeof (uint16_t))
        return false;
    memcpy (out_, zlink_msg_data (const_cast<zlink_msg_t *> (&msg_)),
            sizeof (uint16_t));
    return true;
}

inline bool read_u32 (const zlink_msg_t &msg_, uint32_t *out_)
{
    if (!out_)
        return false;
    if (zlink_msg_size (&msg_) != sizeof (uint32_t))
        return false;
    memcpy (out_, zlink_msg_data (const_cast<zlink_msg_t *> (&msg_)),
            sizeof (uint32_t));
    return true;
}

inline bool read_u64 (const zlink_msg_t &msg_, uint64_t *out_)
{
    if (!out_)
        return false;
    if (zlink_msg_size (&msg_) != sizeof (uint64_t))
        return false;
    memcpy (out_, zlink_msg_data (const_cast<zlink_msg_t *> (&msg_)),
            sizeof (uint64_t));
    return true;
}

inline std::string read_string (const zlink_msg_t &msg_)
{
    const size_t size = zlink_msg_size (&msg_);
    if (size == 0)
        return std::string ();
    const char *data =
      static_cast<const char *> (zlink_msg_data (const_cast<zlink_msg_t *> (&msg_)));
    return std::string (data, data + size);
}

inline bool read_routing_id (const zlink_msg_t &msg_, zlink_routing_id_t *out_)
{
    if (!out_)
        return false;
    const size_t size = zlink_msg_size (&msg_);
    if (size > sizeof (out_->data))
        return false;
    out_->size = static_cast<uint8_t> (size);
    if (size > 0)
        memcpy (out_->data,
                zlink_msg_data (const_cast<zlink_msg_t *> (&msg_)), size);
    return true;
}
}
}

#endif
