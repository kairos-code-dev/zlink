/* SPDX-License-Identifier: MPL-2.0 */

#ifndef __ZLINK_DISCOVERY_PROTOCOL_HPP_INCLUDED__
#define __ZLINK_DISCOVERY_PROTOCOL_HPP_INCLUDED__

#include "core/internal_defs.hpp"
#include "core/send_internal.hpp"
#include "core/msg.hpp"
#include "services/discovery/route_limits_internal.hpp"
#include "zlink_enum.h"
#include "utils/err.hpp"
#include "utils/stdint.hpp"

#include <string>
#include <string.h>
#include <vector>

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
static const uint16_t msg_update_attributes = 0x0007;
static const uint16_t msg_bootstrap_req = 0x0008;
static const uint16_t msg_bootstrap_rep = 0x0009;
static const uint16_t msg_topology_report = 0x000A;
static const uint16_t msg_topology_query = 0x000B;
static const uint16_t msg_topology_reply = 0x000C;
static const uint16_t msg_unregister_ack = 0x000D;
static const uint16_t msg_bind_route = 0x000E;
static const uint16_t msg_unbind_route = 0x000F;
static const uint16_t msg_resolve_route = 0x0010;
static const uint16_t msg_resolve_route_reply = 0x0011;

enum reply_status_t
{
    status_ok = 0x00,
    status_not_found = 0x01,
    status_rejected = 0x02,
    status_conflict = 0x03,
    status_unsupported = 0x04,
    status_invalid = 0xFF
};

inline int register_status_errno (uint8_t status_)
{
    if (status_ == status_conflict)
        return EEXIST;
    if (status_ == status_unsupported)
        return ENOTSUP;
    return EINVAL;
}

inline int route_status_errno (uint8_t status_)
{
    if (status_ == status_not_found)
        return ENOENT;
    if (status_ == status_rejected)
        return ESTALE;
    return EINVAL;
}

enum service_role_t
{
    service_role_invalid = 0,
    service_role_spot = 2,
    service_role_router = 3,
    service_role_dealer = 4,
    service_role_pub = 5,
    service_role_sub = 6
};

inline bool is_valid_service_role (uint16_t service_role_)
{
    return service_role_ == service_role_spot
           || service_role_ == service_role_router
           || service_role_ == service_role_dealer
           || service_role_ == service_role_pub
           || service_role_ == service_role_sub;
}

inline bool is_valid_auto_connect_type (uint16_t auto_connect_type_)
{
    return auto_connect_type_ == ZLINK_AUTO_CONNECT_ROUTE_MESH
           || auto_connect_type_ == ZLINK_AUTO_CONNECT_CLIENT_SERVER
           || auto_connect_type_ == ZLINK_AUTO_CONNECT_DEALER_MESH
           || auto_connect_type_ == ZLINK_AUTO_CONNECT_FANOUT
           || auto_connect_type_ == ZLINK_AUTO_CONNECT_SPOT_MESH;
}

inline bool auto_connect_type_allows_role (uint16_t auto_connect_type_,
                                           uint16_t service_role_)
{
    if (!is_valid_auto_connect_type (auto_connect_type_)
        || !is_valid_service_role (service_role_)) {
        return false;
    }

    switch (auto_connect_type_) {
        case ZLINK_AUTO_CONNECT_ROUTE_MESH:
            return service_role_ == service_role_router;
        case ZLINK_AUTO_CONNECT_CLIENT_SERVER:
            return service_role_ == service_role_router
                   || service_role_ == service_role_dealer;
        case ZLINK_AUTO_CONNECT_DEALER_MESH:
            return service_role_ == service_role_dealer;
        case ZLINK_AUTO_CONNECT_FANOUT:
            return service_role_ == service_role_pub
                   || service_role_ == service_role_sub;
        case ZLINK_AUTO_CONNECT_SPOT_MESH:
            return service_role_ == service_role_spot
                   || service_role_ == service_role_router;
        default:
            return false;
    }
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

inline bool auto_connect_type_allows_raw_socket (
  uint16_t auto_connect_type_)
{
    return auto_connect_type_ == ZLINK_AUTO_CONNECT_ROUTE_MESH
           || auto_connect_type_ == ZLINK_AUTO_CONNECT_CLIENT_SERVER
           || auto_connect_type_ == ZLINK_AUTO_CONNECT_DEALER_MESH
           || auto_connect_type_ == ZLINK_AUTO_CONNECT_FANOUT;
}

inline int compare_routing_id_bytes (const zlink_routing_id_t &lhs_,
                                     const zlink_routing_id_t &rhs_)
{
    const size_t lhs_size = lhs_.size;
    const size_t rhs_size = rhs_.size;
    const size_t common = lhs_size < rhs_size ? lhs_size : rhs_size;
    if (common > 0) {
        const int cmp = memcmp (lhs_.data, rhs_.data, common);
        if (cmp != 0)
            return cmp;
    }
    if (lhs_size < rhs_size)
        return -1;
    if (lhs_size > rhs_size)
        return 1;
    return 0;
}

inline int compare_connect_keys (const zlink_routing_id_t &local_rid_,
                                 const zlink_routing_id_t &remote_rid_,
                                 const std::string &local_endpoint_,
                                 const std::string &remote_endpoint_)
{
    if (local_rid_.size > 0 && remote_rid_.size > 0) {
        const int rid_cmp = compare_routing_id_bytes (local_rid_, remote_rid_);
        if (rid_cmp != 0)
            return rid_cmp;
    }

    if (local_endpoint_ < remote_endpoint_)
        return -1;
    if (local_endpoint_ > remote_endpoint_)
        return 1;
    return 0;
}

inline bool socket_auto_connect_target_matches (
  uint16_t auto_connect_type_,
  uint16_t local_role_,
  uint16_t remote_role_,
  const zlink_routing_id_t &local_rid_,
  const zlink_routing_id_t &remote_rid_,
  const std::string &local_endpoint_,
  const std::string &remote_endpoint_)
{
    if (!auto_connect_type_allows_role (auto_connect_type_, local_role_)
        || !auto_connect_type_allows_role (auto_connect_type_, remote_role_)) {
        return false;
    }

    switch (auto_connect_type_) {
        case ZLINK_AUTO_CONNECT_ROUTE_MESH:
            return local_role_ == service_role_router
                   && remote_role_ == service_role_router
                   && compare_connect_keys (local_rid_, remote_rid_,
                                            local_endpoint_, remote_endpoint_)
                        < 0;
        case ZLINK_AUTO_CONNECT_CLIENT_SERVER:
            return local_role_ == service_role_dealer
                   && remote_role_ == service_role_router;
        case ZLINK_AUTO_CONNECT_DEALER_MESH:
            return local_role_ == service_role_dealer
                   && remote_role_ == service_role_dealer
                   && compare_connect_keys (local_rid_, remote_rid_,
                                            local_endpoint_, remote_endpoint_)
                        < 0;
        case ZLINK_AUTO_CONNECT_FANOUT:
            return local_role_ == service_role_sub
                   && remote_role_ == service_role_pub;
        case ZLINK_AUTO_CONNECT_SPOT_MESH:
            return local_role_ == service_role_spot
                   && remote_role_ == service_role_spot
                   && compare_connect_keys (local_rid_, remote_rid_,
                                            local_endpoint_, remote_endpoint_)
                        < 0;
        default:
            return false;
    }
}

struct bootstrap_req_t
{
    uint16_t msg_id;
    uint16_t auto_connect_type;
    zlink_routing_id_t routing_id;
    char channel_name[256];
};

struct bootstrap_rep_t
{
    uint16_t msg_id;
    uint16_t reserved;
    uint32_t heartbeat_interval_ms;
    uint32_t registry_id;
    uint32_t feature_flags;
    uint32_t status_errno;
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

inline int send_i64 (void *socket_, int64_t value_, int flags_)
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

inline bool read_u8 (const zlink_msg_t &msg_, uint8_t *out_)
{
    if (!out_)
        return false;
    if (zlink_msg_size (&msg_) != sizeof (uint8_t))
        return false;
    memcpy (out_, zlink_msg_data (const_cast<zlink_msg_t *> (&msg_)),
            sizeof (uint8_t));
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

inline bool read_i64 (const zlink_msg_t &msg_, int64_t *out_)
{
    if (!out_)
        return false;
    if (zlink_msg_size (&msg_) != sizeof (int64_t))
        return false;
    memcpy (out_, zlink_msg_data (const_cast<zlink_msg_t *> (&msg_)),
            sizeof (int64_t));
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

inline void read_bytes (const zlink_msg_t &msg_,
                        std::vector<unsigned char> *out_)
{
    if (!out_)
        return;
    const size_t size = zlink_msg_size (&msg_);
    out_->resize (size);
    if (size > 0)
        memcpy (&(*out_)[0], zlink_msg_data (const_cast<zlink_msg_t *> (&msg_)),
                size);
}

struct status_ack_t
{
    status_ack_t () :
        status (status_invalid),
        source_registry (0),
        registration_id (0)
    {
    }

    uint8_t status;
    std::string resolved_endpoint;
    uint32_t source_registry;
    uint64_t registration_id;
    std::string error;
};

struct service_provider_record_t
{
    service_provider_record_t () :
        service_role (0),
        source_registry (0),
        registration_id (0),
        provider_update_seq (0),
        weight (0),
        value (0)
    {
        memset (&routing_id, 0, sizeof (routing_id));
    }

    uint16_t service_role;
    std::string endpoint;
    zlink_routing_id_t routing_id;
    uint32_t source_registry;
    uint64_t registration_id;
    uint64_t provider_update_seq;
    uint16_t weight;
    int64_t value;
    std::vector<unsigned char> metadata;
};

struct service_record_t
{
    uint16_t auto_connect_type;
    std::string channel_name;
    uint64_t contract_created_at;
    std::vector<service_provider_record_t> providers;

    service_record_t () :
        auto_connect_type (0),
        contract_created_at (0)
    {
    }
};

struct service_list_t
{
    service_list_t () :
        registry_id (0),
        list_seq (0)
    {
    }

    uint32_t registry_id;
    uint64_t list_seq;
    std::vector<service_record_t> services;
};

struct route_record_t
{
    route_record_t () :
        raw_kind (0),
        owner_service_role (0),
        owner_source_registry (0),
        owner_registration_id (0),
        updated_at_ms (0)
    {
        memset (&owner_routing_id, 0, sizeof (owner_routing_id));
    }

    std::string channel_name;
    uint32_t raw_kind;
    std::vector<unsigned char> key;
    std::vector<unsigned char> value;
    std::string owner_channel_name;
    uint16_t owner_service_role;
    std::vector<unsigned char> owner_routing_id_key;
    uint32_t owner_source_registry;
    uint64_t owner_registration_id;
    uint64_t updated_at_ms;
    zlink_routing_id_t owner_routing_id;
};

struct route_list_t
{
    route_list_t () :
        registry_id (0),
        list_seq (0),
        chunk_index (0),
        chunk_count (0)
    {
    }

    uint32_t registry_id;
    uint64_t list_seq;
    uint32_t chunk_index;
    uint32_t chunk_count;
    std::vector<route_record_t> routes;
};

inline bool decode_status_ack (const std::vector<zlink_msg_t> &frames_,
                               uint16_t expected_msg_id_,
                               status_ack_t *out_)
{
    if (!out_) {
        errno = EINVAL;
        return false;
    }

    uint16_t msg_id = 0;
    if (frames_.size () < 2 || !read_u16 (frames_[0], &msg_id)
        || msg_id != expected_msg_id_ || !read_u8 (frames_[1], &out_->status)) {
        errno = EPROTO;
        return false;
    }

    out_->resolved_endpoint.clear ();
    out_->source_registry = 0;
    out_->registration_id = 0;
    out_->error.clear ();

    if (expected_msg_id_ == msg_register_ack) {
        if (frames_.size () >= 3)
            out_->resolved_endpoint = read_string (frames_[2]);
        if (frames_.size () >= 4 && !read_u32 (frames_[3],
                                               &out_->source_registry)) {
            errno = EPROTO;
            return false;
        }
        if (frames_.size () >= 5 && !read_u64 (frames_[4],
                                               &out_->registration_id)) {
            errno = EPROTO;
            return false;
        }
        if (frames_.size () >= 6)
            out_->error = read_string (frames_[5]);
    } else if (expected_msg_id_ == msg_unregister_ack && frames_.size () >= 3) {
        out_->error = read_string (frames_[2]);
    }
    return true;
}

inline bool decode_service_list (const std::vector<zlink_msg_t> &frames_,
                                 service_list_t *out_)
{
    if (!out_) {
        errno = EINVAL;
        return false;
    }

    out_->services.clear ();
    uint16_t msg_id = 0;
    uint32_t service_count = 0;
    if (frames_.size () < 4 || !read_u16 (frames_[0], &msg_id)
        || msg_id != msg_service_list || !read_u32 (frames_[1],
                                                    &out_->registry_id)
        || !read_u64 (frames_[2], &out_->list_seq)
        || !read_u32 (frames_[3], &service_count)) {
        errno = EPROTO;
        return false;
    }

    size_t index = 4;
    out_->services.reserve (service_count);
    for (uint32_t i = 0; i < service_count; ++i) {
        if (index + 3 >= frames_.size ()) {
            errno = EPROTO;
            return false;
        }

        service_record_t service;
        uint32_t provider_count = 0;
        if (!read_u16 (frames_[index++], &service.auto_connect_type)
            || !is_valid_auto_connect_type (service.auto_connect_type)) {
            errno = EPROTO;
            return false;
        }
        service.channel_name = read_string (frames_[index++]);
        if (!read_u64 (frames_[index++], &service.contract_created_at)
            || !read_u32 (frames_[index++], &provider_count)) {
            errno = EPROTO;
            return false;
        }

        service.providers.reserve (provider_count);
        for (uint32_t p = 0; p < provider_count; ++p) {
            if (index + 8 >= frames_.size ()) {
                errno = EPROTO;
                return false;
            }

            service_provider_record_t provider;
            if (!read_u16 (frames_[index++], &provider.service_role)
                || !auto_connect_type_allows_role (service.auto_connect_type,
                                                   provider.service_role)) {
                errno = EPROTO;
                return false;
            }
            provider.endpoint = read_string (frames_[index++]);
            if (!read_routing_id (frames_[index++], &provider.routing_id)
                || !read_u32 (frames_[index++], &provider.source_registry)
                || !read_u64 (frames_[index++], &provider.registration_id)
                || !read_u64 (frames_[index++], &provider.provider_update_seq)
                || !read_u16 (frames_[index++], &provider.weight)
                || !read_i64 (frames_[index++], &provider.value)) {
                errno = EPROTO;
                return false;
            }
            read_bytes (frames_[index++], &provider.metadata);
            service.providers.push_back (provider);
        }
        out_->services.push_back (service);
    }

    if (index != frames_.size ()) {
        errno = EPROTO;
        return false;
    }
    return true;
}

inline bool decode_route_list (const std::vector<zlink_msg_t> &frames_,
                               route_list_t *out_)
{
    if (!out_) {
        errno = EINVAL;
        return false;
    }

    out_->routes.clear ();
    uint16_t msg_id = 0;
    uint32_t route_count = 0;
    if (frames_.size () < 6 || !read_u16 (frames_[0], &msg_id)
        || msg_id != msg_registry_sync || !read_u32 (frames_[1],
                                                     &out_->registry_id)
        || !read_u64 (frames_[2], &out_->list_seq)
        || !read_u32 (frames_[3], &out_->chunk_index)
        || !read_u32 (frames_[4], &out_->chunk_count)
        || !read_u32 (frames_[5], &route_count)
        || out_->chunk_count == 0 || out_->chunk_index >= out_->chunk_count) {
        errno = EPROTO;
        return false;
    }

    size_t index = 6;
    out_->routes.reserve (route_count);
    for (uint32_t i = 0; i < route_count; ++i) {
        if (index + 10 >= frames_.size ()) {
            errno = EPROTO;
            return false;
        }

        route_record_t route;
        route.channel_name = read_string (frames_[index++]);
        if (!read_u32 (frames_[index++], &route.raw_kind)) {
            errno = EPROTO;
            return false;
        }
        read_bytes (frames_[index++], &route.key);
        if (route.key.empty () || route.key.size () > ZLINK_ROUTE_KEY_MAX) {
            errno = EPROTO;
            return false;
        }
        read_bytes (frames_[index++], &route.value);
        if (route.value.size () > ZLINK_ROUTE_VALUE_MAX) {
            errno = EPROTO;
            return false;
        }
        route.owner_channel_name = read_string (frames_[index++]);
        if (!read_u16 (frames_[index++], &route.owner_service_role)) {
            errno = EPROTO;
            return false;
        }
        read_bytes (frames_[index++], &route.owner_routing_id_key);
        if (route.owner_routing_id_key.size ()
            > sizeof (route.owner_routing_id.data)) {
            errno = EPROTO;
            return false;
        }
        if (!read_u32 (frames_[index++], &route.owner_source_registry)
            || !read_u64 (frames_[index++], &route.owner_registration_id)
            || !read_u64 (frames_[index++], &route.updated_at_ms)
            || !read_routing_id (frames_[index++], &route.owner_routing_id)) {
            errno = EPROTO;
            return false;
        }
        out_->routes.push_back (route);
    }

    if (index != frames_.size ()) {
        errno = EPROTO;
        return false;
    }
    return true;
}

inline bool decode_topology_reply (
  const std::vector<zlink_msg_t> &frames_,
  std::vector<zlink_registry_topology_entry_t> *entries_out_)
{
    if (!entries_out_) {
        errno = EINVAL;
        return false;
    }

    entries_out_->clear ();
    uint16_t msg_id = 0;
    uint32_t count = 0;
    if (frames_.size () < 2 || !read_u16 (frames_[0], &msg_id)
        || msg_id != msg_topology_reply || !read_u32 (frames_[1], &count)
        || frames_.size () != static_cast<size_t> (count) + 2) {
        errno = EPROTO;
        return false;
    }

    entries_out_->reserve (count);
    for (uint32_t i = 0; i < count; ++i) {
        zlink_registry_topology_entry_t entry;
        memset (&entry, 0, sizeof (entry));
        if (zlink_msg_size (&frames_[i + 2]) != sizeof (entry)) {
            errno = EPROTO;
            return false;
        }
        memcpy (&entry, zlink_msg_data (const_cast<zlink_msg_t *> (
                         &frames_[i + 2])),
                sizeof (entry));
        entries_out_->push_back (entry);
    }
    return true;
}

inline bool decode_route_reply (const std::vector<zlink_msg_t> &frames_,
                                zlink_routing_id_t *owner_rid_out_,
                                zlink_msg_t *value_out_)
{
    uint16_t msg_id = 0;
    uint8_t status = status_invalid;
    if (frames_.size () < 5 || !read_u16 (frames_[0], &msg_id)
        || msg_id != msg_resolve_route_reply || !read_u8 (frames_[1],
                                                          &status)) {
        errno = EPROTO;
        return false;
    }

    if (status != status_ok) {
        errno = route_status_errno (status);
        return false;
    }

    if (owner_rid_out_ && !read_routing_id (frames_[2], owner_rid_out_)) {
        errno = EPROTO;
        return false;
    }
    if (value_out_) {
        const size_t value_size = zlink_msg_size (&frames_[3]);
        if (zlink_msg_init_size (value_out_, value_size) != 0)
            return false;
        if (value_size > 0)
            memcpy (zlink_msg_data (value_out_),
                    zlink_msg_data (const_cast<zlink_msg_t *> (&frames_[3])),
                    value_size);
    }
    return true;
}
}
}

#endif
