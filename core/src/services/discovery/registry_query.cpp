/* SPDX-License-Identifier: MPL-2.0 */

#include "precompiled.hpp"

#include "core/c_api_copy_internal.hpp"
#include "core/send_internal.hpp"
#include "services/discovery/registry.hpp"
#include "services/discovery/discovery_protocol.hpp"

#include <algorithm>
#include <string.h>
#include <utility>

namespace
{
struct summary_key_t
{
    uint16_t auto_connect_type;
    uint16_t service_role;
    std::string channel_name;

    bool operator== (const summary_key_t &other_) const
    {
        return auto_connect_type == other_.auto_connect_type
               && service_role == other_.service_role
               && channel_name == other_.channel_name;
    }
};

struct summary_key_hash_t
{
    size_t operator() (const summary_key_t &key_) const
    {
        size_t seed = std::hash<uint16_t> () (key_.auto_connect_type);
        seed ^= std::hash<uint16_t> () (key_.service_role) + 0x9e3779b9
                + (seed << 6) + (seed >> 2);
        seed ^= std::hash<std::string> () (key_.channel_name) + 0x9e3779b9
                + (seed << 6) + (seed >> 2);
        return seed;
    }
};

bool topology_filter_match (
  const zlink_registry_topology_entry_t &entry_,
  const zlink_registry_topology_filter_t *filter_)
{
    if (!filter_)
        return true;
    if (filter_->auto_connect_type != ZLINK_AUTO_CONNECT_INVALID
        && filter_->auto_connect_type != entry_.auto_connect_type)
        return false;
    if (filter_->service_kind != 0 && filter_->service_kind != entry_.service_kind)
        return false;
    if (filter_->service_role != 0
        && filter_->service_role != entry_.service_role) {
        return false;
    }
    if (filter_->state != 0 && filter_->state != entry_.state)
        return false;
    if (filter_->source != 0 && filter_->source != entry_.source)
        return false;
    if (filter_->channel_name[0] != '\0'
        && strcmp (filter_->channel_name, entry_.channel_name) != 0)
        return false;
    if (filter_->routing_id.size > 0) {
        if (filter_->routing_id.size != entry_.routing_id.size)
            return false;
        if (memcmp (filter_->routing_id.data, entry_.routing_id.data,
                    filter_->routing_id.size)
            != 0) {
            return false;
        }
    }
    return true;
}

bool topology_entry_less (const zlink_registry_topology_entry_t &lhs_,
                          const zlink_registry_topology_entry_t &rhs_)
{
    if (lhs_.auto_connect_type != rhs_.auto_connect_type)
        return lhs_.auto_connect_type < rhs_.auto_connect_type;
    if (lhs_.service_kind != rhs_.service_kind)
        return lhs_.service_kind < rhs_.service_kind;
    const int name_cmp = strcmp (lhs_.channel_name, rhs_.channel_name);
    if (name_cmp != 0)
        return name_cmp < 0;
    if (lhs_.service_role != rhs_.service_role)
        return lhs_.service_role < rhs_.service_role;
    const int endpoint_cmp = strcmp (lhs_.endpoint, rhs_.endpoint);
    if (endpoint_cmp != 0)
        return endpoint_cmp < 0;
    if (lhs_.routing_id.size != rhs_.routing_id.size)
        return lhs_.routing_id.size < rhs_.routing_id.size;
    if (lhs_.routing_id.size == 0)
        return false;
    return memcmp (lhs_.routing_id.data, rhs_.routing_id.data,
                   lhs_.routing_id.size)
           < 0;
}

bool topology_filter_requests_spot_owner_local (
  const zlink_registry_topology_filter_t *filter_)
{
    return filter_ && filter_->service_kind == ZLINK_SERVICE_KIND_SPOT_PUB
           && filter_->service_role == ZLINK_SERVICE_ROLE_SPOT
           && filter_->routing_id.size > 0 && filter_->channel_name[0] != '\0';
}

bool registry_service_summary_filter_match (
  const zlink_registry_service_summary_entry_t &entry_,
  const zlink_registry_service_summary_filter_t *filter_)
{
    if (!filter_)
        return true;
    if (filter_->auto_connect_type != ZLINK_AUTO_CONNECT_INVALID
        && filter_->auto_connect_type != entry_.auto_connect_type)
        return false;
    if (filter_->service_role != 0
        && filter_->service_role != entry_.service_role) {
        return false;
    }
    if (filter_->channel_name[0] != '\0'
        && strcmp (filter_->channel_name, entry_.channel_name) != 0) {
        return false;
    }
    return true;
}

bool registry_service_summary_less (
  const zlink_registry_service_summary_entry_t &lhs_,
  const zlink_registry_service_summary_entry_t &rhs_)
{
    if (lhs_.auto_connect_type != rhs_.auto_connect_type)
        return lhs_.auto_connect_type < rhs_.auto_connect_type;
    const int name_cmp = strcmp (lhs_.channel_name, rhs_.channel_name);
    if (name_cmp != 0)
        return name_cmp < 0;
    return lhs_.service_role < rhs_.service_role;
}

bool member_peer_entry_less (const zlink_member_peer_entry_t &lhs_,
                             const zlink_member_peer_entry_t &rhs_)
{
    if (lhs_.auto_connect_type != rhs_.auto_connect_type)
        return lhs_.auto_connect_type < rhs_.auto_connect_type;
    const int name_cmp = strcmp (lhs_.channel_name, rhs_.channel_name);
    if (name_cmp != 0)
        return name_cmp < 0;
    if (lhs_.service_role != rhs_.service_role)
        return lhs_.service_role < rhs_.service_role;
    return strcmp (lhs_.endpoint, rhs_.endpoint) < 0;
}

struct provider_projection_key_t
{
    uint16_t service_role;
    std::string routing_id_key;

    bool operator< (const provider_projection_key_t &other_) const
    {
        if (service_role != other_.service_role)
            return service_role < other_.service_role;
        return routing_id_key < other_.routing_id_key;
    }
};

struct provider_projection_state_t
{
    provider_projection_state_t () :
        initialized (false),
        conflicted (false),
        source_registry (0),
        registration_id (0)
    {
    }

    bool initialized;
    bool conflicted;
    uint32_t source_registry;
    uint64_t registration_id;
    std::string endpoint;
};

std::string member_routing_id_key_local (const zlink_routing_id_t &rid_)
{
    if (rid_.size == 0)
        return std::string ();
    return std::string (reinterpret_cast<const char *> (rid_.data), rid_.size);
}
}

int zlink::registry_t::topology_snapshot (zlink_registry_topology_entry_t *entries_,
                                          size_t *count_)
{
    service_public_api_scope_t admission (_public_api);
    if (!admission.acquired ())
        return -1;
    return topology_query (NULL, entries_, count_);
}

int zlink::registry_t::topology_query (
  const zlink_registry_topology_filter_t *filter_,
  zlink_registry_topology_entry_t *entries_,
  size_t *count_)
{
    service_public_api_scope_t admission (_public_api);
    if (!admission.acquired ())
        return -1;
    if (!count_) {
        errno = EINVAL;
        return -1;
    }

    std::vector<zlink_registry_topology_entry_t> matched;
    {
        scoped_lock_t lock (_sync);
        collect_topology_entries_locked (filter_, &matched);
    }
    std::sort (matched.begin (), matched.end (), topology_entry_less);

    if (!entries_) {
        *count_ = matched.size ();
        return 0;
    }
    if (*count_ < matched.size ()) {
        *count_ = matched.size ();
        errno = ENOBUFS;
        return -1;
    }
    for (size_t i = 0; i < matched.size (); ++i)
        entries_[i] = matched[i];
    *count_ = matched.size ();
    return 0;
}

void zlink::registry_t::collect_topology_entries_locked (
  const zlink_registry_topology_filter_t *filter_,
  std::vector<zlink_registry_topology_entry_t> *out_) const
{
    if (!out_)
        return;
    out_->clear ();
    out_->reserve (_projection_state.topology.size ());

    std::vector<zlink_registry_topology_entry_t> matched;
    collect_matching_topology_entries_locked (filter_, &matched);

    if (!topology_filter_requests_spot_owner_local (filter_)) {
        *out_ = matched;
        return;
    }

    zlink_registry_topology_entry_t best_entry;
    if (select_spot_owner_entry_locked (matched, filter_->channel_name,
                                        &best_entry)) {
        out_->push_back (best_entry);
    }
}

void zlink::registry_t::collect_matching_topology_entries_locked (
  const zlink_registry_topology_filter_t *filter_,
  std::vector<zlink_registry_topology_entry_t> *out_) const
{
    if (!out_)
        return;
    out_->clear ();
    out_->reserve (_projection_state.topology.size ());

    for (std::map<topology_key_t, topology_entry_t>::const_iterator it =
           _projection_state.topology.begin ();
         it != _projection_state.topology.end (); ++it) {
        if (topology_filter_match (it->second.entry, filter_))
            out_->push_back (it->second.entry);
    }
}

bool zlink::registry_t::select_spot_owner_entry_locked (
  const std::vector<zlink_registry_topology_entry_t> &matched_,
  const char *channel_name_,
  zlink_registry_topology_entry_t *entry_out_) const
{
    if (!channel_name_ || channel_name_[0] == '\0' || !entry_out_)
        return false;

    service_key_t provider_service_key;
    provider_service_key.channel_name = channel_name_;
    service_map_t::const_iterator sit = _projection_state.services.find (provider_service_key);
    if (sit == _projection_state.services.end ())
        return false;

    bool found = false;
    uint64_t best_registration_id = 0;
    uint64_t best_reported_at = 0;
    std::string best_routing_id;
    for (size_t i = 0; i < matched_.size (); ++i) {
        const zlink_registry_topology_entry_t &entry = matched_[i];
        topology_key_t topology_key;
        topology_key.service_kind = entry.service_kind;
        topology_key.service_role = entry.service_role;
        topology_key.routing_id_key.assign (
          reinterpret_cast<const char *> (entry.routing_id.data),
          entry.routing_id.size);
        topology_key.channel_name = entry.channel_name;
        topology_key.endpoint = entry.endpoint;
        std::map<topology_key_t, topology_entry_t>::const_iterator tit =
          _projection_state.topology.find (topology_key);
        if (tit == _projection_state.topology.end () || !tit->second.has_owner
            || !owner_is_live_locked (tit->second.owner)) {
            continue;
        }

        const uint64_t registration_id = tit->second.owner.registration_id;
        const uint64_t reported_at = entry.last_reported_ms;
        const std::string owner_rid = tit->second.owner.routing_id_key;
        if (!found || reported_at > best_reported_at
            || (reported_at == best_reported_at
                && registration_id > best_registration_id)
            || (reported_at == best_reported_at
                && registration_id == best_registration_id
                && owner_rid < best_routing_id)) {
            *entry_out_ = entry;
            best_registration_id = registration_id;
            best_reported_at = reported_at;
            best_routing_id = owner_rid;
            found = true;
        }
    }

    if (!found) {
        for (size_t i = 0; i < matched_.size (); ++i) {
            const zlink_registry_topology_entry_t &entry = matched_[i];
            provider_key_t provider_key;
            provider_key.service_role = discovery_protocol::service_role_spot;
            provider_key.endpoint = entry.endpoint;
            provider_map_t::const_iterator pit =
              sit->second.providers.find (provider_key);
            if (pit == sit->second.providers.end ())
                continue;

            const uint64_t registration_id = pit->second.registration_id;
            const uint64_t reported_at = entry.last_reported_ms;
            const std::string owner_rid =
              std::string (reinterpret_cast<const char *> (
                             pit->second.routing_id.data),
                           pit->second.routing_id.size);
            if (!found || reported_at > best_reported_at
                || (reported_at == best_reported_at
                    && registration_id > best_registration_id)
                || (reported_at == best_reported_at
                    && registration_id == best_registration_id
                    && owner_rid < best_routing_id)) {
                *entry_out_ = entry;
                best_registration_id = registration_id;
                best_reported_at = reported_at;
                best_routing_id = owner_rid;
                found = true;
            }
        }
    }

    return found;
}

int zlink::registry_t::service_summary_snapshot (
  const zlink_registry_service_summary_filter_t *filter_,
  std::vector<zlink_registry_service_summary_entry_t> *out_)
{
    service_public_api_scope_t admission (_public_api);
    if (!admission.acquired ())
        return -1;
    if (!out_) {
        errno = EINVAL;
        return -1;
    }

    out_->clear ();

    std::unordered_map<summary_key_t,
                       zlink_registry_service_summary_entry_t,
                       summary_key_hash_t>
      grouped;
    {
        scoped_lock_t lock (_sync);
        grouped.reserve (_projection_state.topology.size ());
        for (std::map<topology_key_t, topology_entry_t>::const_iterator it =
               _projection_state.topology.begin ();
             it != _projection_state.topology.end (); ++it) {
            const zlink_registry_topology_entry_t &row = it->second.entry;
            summary_key_t key;
            key.auto_connect_type = static_cast<uint16_t> (row.auto_connect_type);
            key.service_role = static_cast<uint16_t> (row.service_role);
            key.channel_name = row.channel_name;
            zlink_registry_service_summary_entry_t &entry = grouped[key];
            if (entry.auto_connect_type == ZLINK_AUTO_CONNECT_INVALID) {
                memset (&entry, 0, sizeof (entry));
                entry.auto_connect_type = row.auto_connect_type;
                entry.service_role = row.service_role;
                copy_fixed_c_string_from_cstr (entry.channel_name,
                                               sizeof (entry.channel_name),
                                               row.channel_name);
            }
            entry.total_count++;
            if (row.last_reported_ms > entry.last_reported_ms)
                entry.last_reported_ms = row.last_reported_ms;
            switch (row.state) {
                case ZLINK_TOPOLOGY_STATE_DISCOVERED:
                case ZLINK_TOPOLOGY_STATE_CONNECTING:
                    entry.connecting_count++;
                    break;
                case ZLINK_TOPOLOGY_STATE_READY:
                    entry.ready_count++;
                    break;
                case ZLINK_TOPOLOGY_STATE_LOST:
                case ZLINK_TOPOLOGY_STATE_ERROR:
                    entry.error_count++;
                    break;
                case ZLINK_TOPOLOGY_STATE_STOPPED:
                    entry.stopped_count++;
                    break;
                default:
                    break;
            }
        }
    }

    out_->reserve (grouped.size ());
    for (std::unordered_map<summary_key_t,
                            zlink_registry_service_summary_entry_t,
                            summary_key_hash_t>::const_iterator it =
           grouped.begin ();
         it != grouped.end (); ++it) {
        if (registry_service_summary_filter_match (it->second, filter_))
            out_->push_back (it->second);
    }
    std::sort (out_->begin (), out_->end (), registry_service_summary_less);
    return 0;
}

int zlink::registry_t::member_peers (const char *channel_name_,
                                     zlink_member_peer_entry_t *entries_,
                                     size_t *count_)
{
    service_public_api_scope_t admission (_public_api);
    if (!admission.acquired ())
        return -1;
    if (!channel_name_ || channel_name_[0] == '\0' || !count_) {
        errno = EINVAL;
        return -1;
    }

    std::vector<zlink_member_peer_entry_t> matched;
    {
        scoped_lock_t lock (_sync);
        service_key_t key;
        key.channel_name = channel_name_;
        service_map_t::const_iterator sit = _projection_state.services.find (key);
        if (sit != _projection_state.services.end ()) {
            std::map<provider_projection_key_t, provider_projection_state_t>
              projection;
            for (provider_map_t::const_iterator pit =
                   sit->second.providers.begin ();
                 pit != sit->second.providers.end (); ++pit) {
                const std::string rid_key =
                  member_routing_id_key_local (pit->second.routing_id);
                if (rid_key.empty ())
                    continue;
                provider_projection_key_t projection_key;
                projection_key.service_role = pit->second.service_role;
                projection_key.routing_id_key = rid_key;
                provider_projection_state_t &state =
                  projection[projection_key];
                if (!state.initialized) {
                    state.initialized = true;
                    state.source_registry = pit->second.source_registry;
                    state.registration_id = pit->second.registration_id;
                    state.endpoint = pit->second.endpoint;
                } else if (state.source_registry != pit->second.source_registry
                           || state.registration_id
                                != pit->second.registration_id
                           || state.endpoint != pit->second.endpoint) {
                    state.conflicted = true;
                }
            }

            matched.reserve (sit->second.providers.size ());
            for (provider_map_t::const_iterator pit = sit->second.providers.begin ();
                 pit != sit->second.providers.end (); ++pit) {
                const std::string rid_key =
                  member_routing_id_key_local (pit->second.routing_id);
                if (!rid_key.empty ()) {
                    provider_projection_key_t projection_key;
                    projection_key.service_role = pit->second.service_role;
                    projection_key.routing_id_key = rid_key;
                    std::map<provider_projection_key_t,
                             provider_projection_state_t>::const_iterator
                      projection_it = projection.find (projection_key);
                    if (projection_it != projection.end ()
                        && projection_it->second.conflicted) {
                        continue;
                    }
                }
                zlink_member_peer_entry_t entry;
                memset (&entry, 0, sizeof (entry));
                entry.auto_connect_type =
                  static_cast<zlink_auto_connect_type_t> (
                    sit->second.auto_connect_type);
                entry.service_role =
                  static_cast<zlink_service_role_t> (pit->second.service_role);
                copy_fixed_c_string_from_cstr (entry.channel_name,
                                               sizeof (entry.channel_name),
                                               channel_name_);
                copy_fixed_c_string_from_cstr (entry.endpoint,
                                               sizeof (entry.endpoint),
                                               pit->second.endpoint.c_str ());
                entry.routing_id = pit->second.routing_id;
                entry.weight = pit->second.weight;
                entry.value = pit->second.value;
                matched.push_back (entry);
            }
        }
    }

    std::sort (matched.begin (), matched.end (), member_peer_entry_less);
    if (!entries_) {
        *count_ = matched.size ();
        return 0;
    }
    if (*count_ < matched.size ()) {
        *count_ = matched.size ();
        errno = ENOBUFS;
        return -1;
    }
    for (size_t i = 0; i < matched.size (); ++i)
        entries_[i] = matched[i];
    *count_ = matched.size ();
    return 0;
}

void zlink::registry_t::handle_topology_query (
  void *router_,
  const zlink_msg_t *frames_,
  size_t frame_count_,
  const zlink_routing_id_t &sender_id_)
{
    zlink_registry_topology_filter_t filter;
    memset (&filter, 0, sizeof (filter));
    const zlink_registry_topology_filter_t *filter_ptr = NULL;

    if (frame_count_ >= 2 && zlink_msg_size (&frames_[1]) == sizeof (filter)) {
        memcpy (&filter,
                zlink_msg_data (const_cast<zlink_msg_t *> (&frames_[1])),
                sizeof (filter));
        filter_ptr = &filter;
    }

    std::vector<zlink_registry_topology_entry_t> entries;
    {
        scoped_lock_t lock (_sync);
        collect_topology_entries_locked (filter_ptr, &entries);
    }
    std::sort (entries.begin (), entries.end (), topology_entry_less);
    send_topology_reply (router_, sender_id_, entries);
}

void zlink::registry_t::send_topology_reply (
  void *router_,
  const zlink_routing_id_t &sender_id_,
  const std::vector<zlink_registry_topology_entry_t> &entries_)
{
    zlink_msg_t id_frame;
    zlink_msg_init_size (&id_frame, sender_id_.size);
    if (sender_id_.size > 0)
        memcpy (zlink_msg_data (&id_frame), sender_id_.data, sender_id_.size);
    if (zlink::send_msg_internal (router_, &id_frame, ZLINK_SNDMORE) == -1) {
        zlink_msg_close (&id_frame);
        return;
    }

    discovery_protocol::send_u16 (router_, discovery_protocol::msg_topology_reply,
                                  ZLINK_SNDMORE);
    discovery_protocol::send_u32 (router_,
                                  static_cast<uint32_t> (entries_.size ()),
                                  entries_.empty () ? 0 : ZLINK_SNDMORE);
    for (size_t i = 0; i < entries_.size (); ++i) {
        discovery_protocol::send_frame (
          router_, &entries_[i], sizeof (entries_[i]),
          (i + 1 == entries_.size ()) ? 0 : ZLINK_SNDMORE);
    }
}

void zlink::registry_t::send_route_reply (
  void *router_,
  const zlink_routing_id_t &sender_id_,
  uint8_t status_,
  const zlink_routing_id_t *owner_rid_,
  const std::vector<unsigned char> *value_,
  const std::string &error_)
{
    zlink_msg_t id_frame;
    zlink_msg_init_size (&id_frame, sender_id_.size);
    if (sender_id_.size > 0)
        memcpy (zlink_msg_data (&id_frame), sender_id_.data, sender_id_.size);
    if (zlink::send_msg_internal (router_, &id_frame, ZLINK_SNDMORE) == -1) {
        zlink_msg_close (&id_frame);
        return;
    }

    zlink_routing_id_t empty_rid;
    memset (&empty_rid, 0, sizeof (empty_rid));
    const zlink_routing_id_t &rid = owner_rid_ ? *owner_rid_ : empty_rid;
    const std::vector<unsigned char> empty_value;
    const std::vector<unsigned char> &value = value_ ? *value_ : empty_value;

    discovery_protocol::send_u16 (
      router_, discovery_protocol::msg_resolve_route_reply, ZLINK_SNDMORE);
    discovery_protocol::send_frame (router_, &status_, sizeof (status_),
                                    ZLINK_SNDMORE);
    discovery_protocol::send_routing_id (router_, rid, ZLINK_SNDMORE);
    discovery_protocol::send_frame (
      router_, value.empty () ? NULL : &value[0], value.size (),
      ZLINK_SNDMORE);
    discovery_protocol::send_string (router_, error_, 0);
}

bool zlink::registry_t::read_route_key (zlink_route_kind_t kind_,
                                        const zlink_msg_t &key_frame_,
                                        const std::string &channel_name_,
                                        route_key_t *out_) const
{
    if (!out_ || channel_name_.empty ()
        || kind_ == ZLINK_ROUTE_KIND_INVALID) {
        errno = EINVAL;
        return false;
    }
    const size_t key_size = zlink_msg_size (&key_frame_);
    if (key_size == 0 || key_size > ZLINK_ROUTE_KEY_MAX) {
        errno = key_size == 0 ? EINVAL : EMSGSIZE;
        return false;
    }
    out_->channel_name = channel_name_;
    out_->kind = kind_;
    out_->key.assign (
      static_cast<const char *> (
        zlink_msg_data (const_cast<zlink_msg_t *> (&key_frame_))),
      key_size);
    return true;
}

bool zlink::registry_t::owner_routing_id_from_key (
  const owner_identity_t &owner_,
  zlink_routing_id_t *out_) const
{
    if (!out_)
        return false;
    if (owner_.routing_id_key.size () > sizeof (out_->data))
        return false;
    memset (out_, 0, sizeof (*out_));
    out_->size = static_cast<uint8_t> (owner_.routing_id_key.size ());
    if (!owner_.routing_id_key.empty ())
        memcpy (out_->data, owner_.routing_id_key.data (),
                owner_.routing_id_key.size ());
    return true;
}

void zlink::registry_t::handle_bind_route (
  void *router_,
  const zlink_msg_t *frames_,
  size_t frame_count_,
  const zlink_routing_id_t &sender_id_)
{
    if (frame_count_ != 8) {
        send_route_reply (router_, sender_id_, 0xFF, NULL, NULL,
                          "invalid bind route");
        return;
    }

    uint32_t raw_kind = 0;
    if (!discovery_protocol::read_u32 (frames_[1], &raw_kind)) {
        send_route_reply (router_, sender_id_, 0xFF, NULL, NULL,
                          "invalid kind");
        return;
    }
    const size_t value_size = zlink_msg_size (&frames_[3]);
    if (value_size > ZLINK_ROUTE_VALUE_MAX) {
        send_route_reply (router_, sender_id_, 0xFF, NULL, NULL,
                          "value too large");
        return;
    }
    const std::string channel_name =
      discovery_protocol::read_string (frames_[4]);
    uint16_t owner_role = 0;
    uint64_t registration_id = 0;
    if (!discovery_protocol::read_u16 (frames_[5], &owner_role)
        || !discovery_protocol::read_u64 (frames_[7], &registration_id)) {
        send_route_reply (router_, sender_id_, 0xFF, NULL, NULL,
                          "invalid owner");
        return;
    }
    const std::string owner_endpoint =
      discovery_protocol::read_string (frames_[6]);

    const uint64_t now_ms = zlink::clock_t ().now_ms ();
    uint8_t status = 0x00;
    std::string error;
    {
        scoped_lock_t lock (_sync);
        owner_identity_t owner;
        zlink_routing_id_t owner_rid;
        if (!find_provider_owner_locked (channel_name, owner_role, owner_endpoint,
                                         &owner, &owner_rid)) {
            owner.registration_id = 0;
        }
        if (owner.registration_id == 0) {
            status = 0x01;
            error = "owner not found";
        } else if (owner.registration_id != registration_id) {
            status = 0x02;
            error = "stale owner generation";
        } else {
            route_key_t route_key;
            if (!read_route_key (raw_kind, frames_[2], channel_name,
                                 &route_key)) {
                status = 0xFF;
                error = "invalid route key";
            } else {
                route_entry_t entry;
                entry.key = route_key;
                if (value_size > 0) {
                    const unsigned char *value_data =
                      static_cast<const unsigned char *> (
                        zlink_msg_data (
                          const_cast<zlink_msg_t *> (&frames_[3])));
                    entry.value.assign (value_data, value_data + value_size);
                }
                entry.owner = owner;
                entry.updated_at_ms = now_ms;
                entry.advertising_registry = _coordination_state.registry_id == 0 ? 1
                                                               : _coordination_state.registry_id;
                (void) owner_rid;

                size_t replaced_memory = 0;
                route_observations_by_route_t::const_iterator route_it =
                  _projection_state.route_observations_by_route.find (route_key);
                if (route_it != _projection_state.route_observations_by_route.end ()) {
                    for (route_observation_key_set_t::const_iterator obs =
                           route_it->second.begin ();
                         obs != route_it->second.end (); ++obs) {
                        if (obs->advertising_registry
                            != entry.advertising_registry)
                            continue;
                        route_observation_map_t::const_iterator current =
                          _projection_state.route_observations.find (*obs);
                        if (current != _projection_state.route_observations.end ())
                            replaced_memory +=
                              route_entry_memory_bytes (current->second);
                    }
                }

                int route_error = 0;
                if (!route_store_can_fit_locked (entry, replaced_memory,
                                                 &route_error)) {
                    status = 0xFF;
                    error = route_error == ENOSPC ? "route store full"
                                                  : "route too large";
                } else {
                route_key_set_t dirty_routes;
                erase_route_observations_by_route_advertiser_locked (
                  route_key, entry.advertising_registry, &dirty_routes);

                    upsert_route_observation_locked (entry, &dirty_routes);
                    materialize_dirty_routes_locked (dirty_routes);
                    _coordination_state.list_seq++;
                }
            }
        }
    }

    send_route_reply (router_, sender_id_, status, NULL, NULL, error);
}

void zlink::registry_t::handle_unbind_route (
  void *router_,
  const zlink_msg_t *frames_,
  size_t frame_count_,
  const zlink_routing_id_t &sender_id_)
{
    if (frame_count_ != 7) {
        send_route_reply (router_, sender_id_, 0xFF, NULL, NULL,
                          "invalid unbind route");
        return;
    }

    uint32_t raw_kind = 0;
    const std::string channel_name =
      discovery_protocol::read_string (frames_[3]);
    uint16_t owner_role = 0;
    uint64_t registration_id = 0;
    if (!discovery_protocol::read_u32 (frames_[1], &raw_kind)
        || !discovery_protocol::read_u16 (frames_[4], &owner_role)
        || !discovery_protocol::read_u64 (frames_[6], &registration_id)) {
        send_route_reply (router_, sender_id_, 0xFF, NULL, NULL,
                          "invalid unbind route");
        return;
    }
    const std::string owner_endpoint =
      discovery_protocol::read_string (frames_[5]);

    uint8_t status = 0x00;
    std::string error;
    {
        scoped_lock_t lock (_sync);
        owner_identity_t owner;
        zlink_routing_id_t owner_rid;
        if (!find_provider_owner_locked (channel_name, owner_role,
                                         owner_endpoint, &owner, &owner_rid))
            owner.registration_id = 0;
        if (owner.registration_id == 0) {
            status = 0x01;
            error = "owner not found";
        } else if (owner.registration_id != registration_id) {
            status = 0x02;
            error = "stale owner generation";
        } else {
            route_key_t route_key;
            if (!read_route_key (raw_kind, frames_[2], channel_name,
                                 &route_key)) {
                status = 0xFF;
                error = "invalid route key";
            } else {
                route_map_t::iterator it = _projection_state.routes.find (route_key);
                if (it == _projection_state.routes.end ()) {
                    status = 0x01;
                    error = "route not found";
                } else if (!(it->owner == owner)) {
                    status = 0x02;
                    error = "route owner mismatch";
                } else {
                    route_key_set_t dirty_routes;
                    route_observation_key_t obs_key;
                    obs_key.route_key = route_key;
                    obs_key.owner = owner;
                    obs_key.advertising_registry = _coordination_state.registry_id == 0
                                                     ? 1
                                                     : _coordination_state.registry_id;
                    erase_route_observation_locked (obs_key, &dirty_routes);
                    materialize_dirty_routes_locked (dirty_routes);
                    _coordination_state.list_seq++;
                }
            }
        }
    }

    send_route_reply (router_, sender_id_, status, NULL, NULL, error);
}

void zlink::registry_t::handle_resolve_route (
  void *router_,
  const zlink_msg_t *frames_,
  size_t frame_count_,
  const zlink_routing_id_t &sender_id_)
{
    if (frame_count_ < 4) {
        send_route_reply (router_, sender_id_, 0xFF, NULL, NULL,
                          "invalid resolve route");
        return;
    }

    uint32_t raw_kind = 0;
    if (!discovery_protocol::read_u32 (frames_[1], &raw_kind)) {
        send_route_reply (router_, sender_id_, 0xFF, NULL, NULL,
                          "invalid kind");
        return;
    }
    const std::string channel_name =
      discovery_protocol::read_string (frames_[3]);

    uint8_t status = 0x01;
    zlink_routing_id_t owner_rid;
    memset (&owner_rid, 0, sizeof (owner_rid));
    std::vector<unsigned char> value;
    std::string error = "route not found";
    {
        scoped_lock_t lock (_sync);
        route_key_t route_key;
        if (!read_route_key (raw_kind, frames_[2], channel_name, &route_key)) {
            status = 0xFF;
            error = "invalid route key";
        } else {
            materialize_route_winner_locked (route_key);
            route_map_t::const_iterator it = _projection_state.routes.find (route_key);
            if (it != _projection_state.routes.end () && owner_is_live_locked (it->owner)
                && owner_routing_id_from_key (it->owner, &owner_rid)) {
                status = 0x00;
                value = it->value;
                error.clear ();
            }
        }
    }

    send_route_reply (router_, sender_id_, status, &owner_rid, &value, error);
}
