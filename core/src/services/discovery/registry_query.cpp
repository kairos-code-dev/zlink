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

bool gateway_peer_filter_match (
  const zlink_registry_gateway_peer_entry_t &entry_,
  const zlink_registry_gateway_peer_filter_t *filter_)
{
    if (!filter_)
        return true;
    if (filter_->state != 0 && filter_->state != entry_.state)
        return false;
    if (filter_->service_name[0] != '\0'
        && strcmp (filter_->service_name, entry_.service_name) != 0) {
        return false;
    }
    if (filter_->gateway_routing_id.size > 0) {
        if (filter_->gateway_routing_id.size != entry_.gateway_routing_id.size)
            return false;
        if (memcmp (filter_->gateway_routing_id.data,
                    entry_.gateway_routing_id.data,
                    filter_->gateway_routing_id.size)
            != 0) {
            return false;
        }
    }
    if (filter_->peer_routing_id.size > 0) {
        if (filter_->peer_routing_id.size != entry_.peer_routing_id.size)
            return false;
        if (memcmp (filter_->peer_routing_id.data, entry_.peer_routing_id.data,
                    filter_->peer_routing_id.size)
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
        for (std::map<topology_key_t, topology_entry_t>::const_iterator it =
               _topology.begin ();
             it != _topology.end (); ++it) {
            if (topology_filter_match (it->second.entry, filter_))
                matched.push_back (it->second.entry);
        }
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

    typedef std::pair<std::pair<uint16_t, uint16_t>, std::string> summary_key_t;
    std::map<summary_key_t, zlink_registry_service_summary_entry_t> grouped;
    {
        scoped_lock_t lock (_sync);
        for (std::map<topology_key_t, topology_entry_t>::const_iterator it =
               _topology.begin ();
             it != _topology.end (); ++it) {
            const zlink_registry_topology_entry_t &row = it->second.entry;
            const summary_key_t key (
              std::make_pair (
                std::make_pair (static_cast<uint16_t> (row.service_kind),
                                static_cast<uint16_t> (row.service_role)),
                std::string (row.service_name)));
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

    for (std::map<summary_key_t,
                  zlink_registry_service_summary_entry_t>::const_iterator it =
           grouped.begin ();
         it != grouped.end (); ++it) {
        if (registry_service_summary_filter_match (it->second, filter_))
            out_->push_back (it->second);
    }
    std::sort (out_->begin (), out_->end (), registry_service_summary_less);
    return 0;
}

int zlink::registry_t::gateway_peers_snapshot (
  zlink_registry_gateway_peer_entry_t *entries_,
  size_t *count_)
{
    service_public_api_scope_t admission (_public_api);
    if (!admission.acquired ())
        return -1;
    return gateway_peers_query (NULL, entries_, count_);
}

int zlink::registry_t::gateway_peers_query (
  const zlink_registry_gateway_peer_filter_t *filter_,
  zlink_registry_gateway_peer_entry_t *entries_,
  size_t *count_)
{
    service_public_api_scope_t admission (_public_api);
    if (!admission.acquired ())
        return -1;
    if (!count_) {
        errno = EINVAL;
        return -1;
    }

    std::vector<zlink_registry_gateway_peer_entry_t> matched;
    {
        scoped_lock_t lock (_sync);
        for (std::map<gateway_peer_key_t, gateway_peer_entry_t>::const_iterator it =
               _gateway_peers.begin ();
             it != _gateway_peers.end (); ++it) {
            if (gateway_peer_filter_match (it->second.entry, filter_))
                matched.push_back (it->second.entry);
        }
    }

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
        for (std::map<topology_key_t, topology_entry_t>::const_iterator it =
               _topology.begin ();
             it != _topology.end (); ++it) {
            if (topology_filter_match (it->second.entry, filter_ptr))
                entries.push_back (it->second.entry);
        }
    }
    std::sort (entries.begin (), entries.end (), topology_entry_less);
    send_topology_reply (router_, sender_id_, entries);
}

void zlink::registry_t::handle_gateway_peer_query (
  void *router_,
  const zlink_msg_t *frames_,
  size_t frame_count_,
  const zlink_routing_id_t &sender_id_)
{
    zlink_registry_gateway_peer_filter_t filter;
    memset (&filter, 0, sizeof (filter));
    const zlink_registry_gateway_peer_filter_t *filter_ptr = NULL;

    if (frame_count_ >= 2 && zlink_msg_size (&frames_[1]) == sizeof (filter)) {
        memcpy (&filter,
                zlink_msg_data (const_cast<zlink_msg_t *> (&frames_[1])),
                sizeof (filter));
        filter_ptr = &filter;
    }

    std::vector<zlink_registry_gateway_peer_entry_t> entries;
    {
        scoped_lock_t lock (_sync);
        for (std::map<gateway_peer_key_t, gateway_peer_entry_t>::const_iterator it =
               _gateway_peers.begin ();
             it != _gateway_peers.end (); ++it) {
            if (gateway_peer_filter_match (it->second.entry, filter_ptr))
                entries.push_back (it->second.entry);
        }
    }
    send_gateway_peer_reply (router_, sender_id_, entries);
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

void zlink::registry_t::send_gateway_peer_reply (
  void *router_,
  const zlink_routing_id_t &sender_id_,
  const std::vector<zlink_registry_gateway_peer_entry_t> &entries_)
{
    zlink_msg_t id_frame;
    zlink_msg_init_size (&id_frame, sender_id_.size);
    if (sender_id_.size > 0)
        memcpy (zlink_msg_data (&id_frame), sender_id_.data, sender_id_.size);
    if (zlink::send_msg_internal (router_, &id_frame, ZLINK_SNDMORE) == -1) {
        zlink_msg_close (&id_frame);
        return;
    }

    discovery_protocol::send_u16 (
      router_, discovery_protocol::msg_gateway_peer_reply, ZLINK_SNDMORE);
    discovery_protocol::send_u32 (router_,
                                  static_cast<uint32_t> (entries_.size ()),
                                  entries_.empty () ? 0 : ZLINK_SNDMORE);
    for (size_t i = 0; i < entries_.size (); ++i) {
        discovery_protocol::send_frame (
          router_, &entries_[i], sizeof (entries_[i]),
          (i + 1 == entries_.size ()) ? 0 : ZLINK_SNDMORE);
    }
}
