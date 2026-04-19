/* SPDX-License-Identifier: MPL-2.0 */

#include "precompiled.hpp"

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
    uint16_t service_kind;
    uint16_t service_role;
    std::string service_name;

    bool operator== (const summary_key_t &other_) const
    {
        return service_kind == other_.service_kind
               && service_role == other_.service_role
               && service_name == other_.service_name;
    }
};

struct summary_key_hash_t
{
    size_t operator() (const summary_key_t &key_) const
    {
        size_t seed = std::hash<uint16_t> () (key_.service_kind);
        seed ^= std::hash<uint16_t> () (key_.service_role) + 0x9e3779b9
                + (seed << 6) + (seed >> 2);
        seed ^= std::hash<std::string> () (key_.service_name) + 0x9e3779b9
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
    if (filter_->service_name[0] != '\0'
        && strcmp (filter_->service_name, entry_.service_name) != 0)
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
    if (lhs_.service_kind != rhs_.service_kind)
        return lhs_.service_kind < rhs_.service_kind;
    const int name_cmp = strcmp (lhs_.service_name, rhs_.service_name);
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
           && filter_->routing_id.size > 0 && filter_->service_name[0] != '\0';
}

bool registry_service_summary_filter_match (
  const zlink_registry_service_summary_entry_t &entry_,
  const zlink_registry_service_summary_filter_t *filter_)
{
    if (!filter_)
        return true;
    if (filter_->service_kind != 0 && filter_->service_kind != entry_.service_kind)
        return false;
    if (filter_->service_role != 0
        && filter_->service_role != entry_.service_role) {
        return false;
    }
    if (filter_->service_name[0] != '\0'
        && strcmp (filter_->service_name, entry_.service_name) != 0) {
        return false;
    }
    return true;
}

bool registry_service_summary_less (
  const zlink_registry_service_summary_entry_t &lhs_,
  const zlink_registry_service_summary_entry_t &rhs_)
{
    if (lhs_.service_kind != rhs_.service_kind)
        return lhs_.service_kind < rhs_.service_kind;
    const int name_cmp = strcmp (lhs_.service_name, rhs_.service_name);
    if (name_cmp != 0)
        return name_cmp < 0;
    return lhs_.service_role < rhs_.service_role;
}

zlink_service_type_t public_service_type_local (zlink_service_type_t service_type_)
{
    return service_type_;
}

bool member_peer_entry_less (const zlink_member_peer_entry_t &lhs_,
                             const zlink_member_peer_entry_t &rhs_)
{
    if (lhs_.service_type != rhs_.service_type)
        return lhs_.service_type < rhs_.service_type;
    const int name_cmp = strcmp (lhs_.service_name, rhs_.service_name);
    if (name_cmp != 0)
        return name_cmp < 0;
    if (lhs_.service_role != rhs_.service_role)
        return lhs_.service_role < rhs_.service_role;
    return strcmp (lhs_.endpoint, rhs_.endpoint) < 0;
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
    out_->reserve (_topology.size ());

    std::vector<zlink_registry_topology_entry_t> matched;
    collect_matching_topology_entries_locked (filter_, &matched);

    if (!topology_filter_requests_spot_owner_local (filter_)) {
        *out_ = matched;
        return;
    }

    zlink_registry_topology_entry_t best_entry;
    if (select_spot_owner_entry_locked (matched, filter_->service_name,
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
    out_->reserve (_topology.size ());

    for (std::map<topology_key_t, topology_entry_t>::const_iterator it =
           _topology.begin ();
         it != _topology.end (); ++it) {
        if (topology_filter_match (it->second.entry, filter_))
            out_->push_back (it->second.entry);
    }
}

bool zlink::registry_t::select_spot_owner_entry_locked (
  const std::vector<zlink_registry_topology_entry_t> &matched_,
  const char *service_name_,
  zlink_registry_topology_entry_t *entry_out_) const
{
    if (!service_name_ || service_name_[0] == '\0' || !entry_out_)
        return false;

    service_key_t provider_service_key;
    provider_service_key.service_type = discovery_protocol::service_type_spot_node;
    provider_service_key.service_name = service_name_;
    service_map_t::const_iterator sit = _services.find (provider_service_key);
    if (sit == _services.end ())
        return false;

    bool found = false;
    uint64_t best_registered_at = 0;
    uint64_t best_reported_at = 0;
    std::string best_endpoint;
    for (size_t i = 0; i < matched_.size (); ++i) {
        const zlink_registry_topology_entry_t &entry = matched_[i];
        provider_key_t provider_key;
        provider_key.service_role = discovery_protocol::service_role_spot;
        provider_key.endpoint = entry.endpoint;
        provider_map_t::const_iterator pit =
          sit->second.providers.find (provider_key);
        if (pit == sit->second.providers.end ())
            continue;

        const uint64_t registered_at = pit->second.registered_at;
        const uint64_t reported_at = entry.last_reported_ms;
        if (!found || registered_at > best_registered_at
            || (registered_at == best_registered_at
                && reported_at > best_reported_at)
            || (registered_at == best_registered_at
                && reported_at == best_reported_at
                && entry.endpoint > best_endpoint)) {
            *entry_out_ = entry;
            best_registered_at = registered_at;
            best_reported_at = reported_at;
            best_endpoint = entry.endpoint;
            found = true;
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
        grouped.reserve (_topology.size ());
        for (std::map<topology_key_t, topology_entry_t>::const_iterator it =
               _topology.begin ();
             it != _topology.end (); ++it) {
            const zlink_registry_topology_entry_t &row = it->second.entry;
            summary_key_t key;
            key.service_kind = static_cast<uint16_t> (row.service_kind);
            key.service_role = static_cast<uint16_t> (row.service_role);
            key.service_name = row.service_name;
            zlink_registry_service_summary_entry_t &entry = grouped[key];
            if (entry.service_kind == 0) {
                memset (&entry, 0, sizeof (entry));
                entry.service_kind = row.service_kind;
                entry.service_role = row.service_role;
                strncpy (entry.service_name, row.service_name,
                         sizeof (entry.service_name) - 1);
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

int zlink::registry_t::member_peers (zlink_service_type_t service_type_,
                                     const char *service_name_,
                                     zlink_member_peer_entry_t *entries_,
                                     size_t *count_)
{
    service_public_api_scope_t admission (_public_api);
    if (!admission.acquired ())
        return -1;
    if (!service_name_ || service_name_[0] == '\0' || !count_) {
        errno = EINVAL;
        return -1;
    }

    std::vector<zlink_member_peer_entry_t> matched;
    {
        scoped_lock_t lock (_sync);
        service_key_t key;
        key.service_type =
          service_type_ == ZLINK_SERVICE_TYPE_SPOT
            ? discovery_protocol::service_type_spot_node
            : discovery_protocol::service_type_socket;
        key.service_name = service_name_;
        service_map_t::const_iterator sit = _services.find (key);
        if (sit != _services.end ()) {
            matched.reserve (sit->second.providers.size ());
            for (provider_map_t::const_iterator pit = sit->second.providers.begin ();
                 pit != sit->second.providers.end (); ++pit) {
                zlink_member_peer_entry_t entry;
                memset (&entry, 0, sizeof (entry));
                entry.service_type = public_service_type_local (service_type_);
                entry.service_role = pit->second.service_role;
                strncpy (entry.service_name, service_name_,
                         sizeof (entry.service_name) - 1);
                strncpy (entry.endpoint, pit->second.endpoint.c_str (),
                         sizeof (entry.endpoint) - 1);
                entry.routing_id = pit->second.routing_id;
                entry.admission_state = pit->second.admission_state;
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

int zlink::registry_t::member_peer_metadata (zlink_service_type_t service_type_,
                                             const char *service_name_,
                                             uint16_t service_role_,
                                             const char *endpoint_,
                                             zlink_msg_t *metadata_out_)
{
    service_public_api_scope_t admission (_public_api);
    if (!admission.acquired ())
        return -1;
    if (!service_name_ || service_name_[0] == '\0' || !endpoint_
        || endpoint_[0] == '\0' || !metadata_out_) {
        errno = EINVAL;
        return -1;
    }

    std::vector<unsigned char> metadata;
    bool found = false;
    {
        scoped_lock_t lock (_sync);
        service_key_t key;
        key.service_type =
          service_type_ == ZLINK_SERVICE_TYPE_SPOT
            ? discovery_protocol::service_type_spot_node
            : discovery_protocol::service_type_socket;
        key.service_name = service_name_;
        service_map_t::const_iterator sit = _services.find (key);
        if (sit != _services.end ()) {
            provider_key_t provider_key;
            provider_key.service_role = service_role_;
            provider_key.endpoint = endpoint_;
            provider_map_t::const_iterator pit =
              sit->second.providers.find (provider_key);
            if (pit != sit->second.providers.end ()) {
                metadata = pit->second.metadata;
                found = true;
            }
        }
    }

    if (!found) {
        errno = ENOENT;
        return -1;
    }
    if (zlink_msg_init_size (metadata_out_, metadata.size ()) != 0)
        return -1;
    if (!metadata.empty ())
        memcpy (zlink_msg_data (metadata_out_), &metadata[0], metadata.size ());
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
