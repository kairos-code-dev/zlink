/* SPDX-License-Identifier: MPL-2.0 */

#include "utils/precompiled.hpp"

#include "core/send_internal.hpp"
#include "services/registry/registry.hpp"
#include "services/discovery/discovery_protocol.hpp"

#include <string.h>

void zlink::registry_t::send_route_reply (void *router_,
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

    discovery_protocol::send_u16 (router_, discovery_protocol::msg_resolve_route_reply,
                                  ZLINK_SNDMORE);
    discovery_protocol::send_frame (router_, &status_, sizeof (status_), ZLINK_SNDMORE);
    discovery_protocol::send_routing_id (router_, rid, ZLINK_SNDMORE);
    discovery_protocol::send_frame (router_, value.empty () ? NULL : &value[0], value.size (),
                                    ZLINK_SNDMORE);
    discovery_protocol::send_string (router_, error_, 0);
}

bool zlink::registry_t::read_route_key (zlink_route_kind_t kind_,
                                        const zlink_msg_t &key_frame_,
                                        const std::string &channel_name_,
                                        route_key_t *out_) const
{
    if (!out_ || channel_name_.empty () || kind_ == ZLINK_ROUTE_KIND_INVALID) {
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
      static_cast<const char *> (zlink_msg_data (const_cast<zlink_msg_t *> (&key_frame_))),
      key_size);
    return true;
}

bool zlink::registry_t::owner_routing_id_from_key (const owner_identity_t &owner_,
                                                   zlink_routing_id_t *out_) const
{
    if (!out_)
        return false;
    if (owner_.routing_id_key.size () > sizeof (out_->data))
        return false;
    memset (out_, 0, sizeof (*out_));
    out_->size = static_cast<uint8_t> (owner_.routing_id_key.size ());
    if (!owner_.routing_id_key.empty ())
        memcpy (out_->data, owner_.routing_id_key.data (), owner_.routing_id_key.size ());
    return true;
}

void zlink::registry_t::handle_bind_route (void *router_,
                                           const zlink_msg_t *frames_,
                                           size_t frame_count_,
                                           const zlink_routing_id_t &sender_id_)
{
    if (frame_count_ != 8) {
        send_route_reply (router_, sender_id_, discovery_protocol::status_invalid, NULL, NULL,
                          "invalid bind route");
        return;
    }

    uint32_t raw_kind = 0;
    if (!discovery_protocol::read_u32 (frames_[1], &raw_kind)) {
        send_route_reply (router_, sender_id_, discovery_protocol::status_invalid, NULL, NULL,
                          "invalid kind");
        return;
    }
    const size_t value_size = zlink_msg_size (&frames_[3]);
    if (value_size > ZLINK_ROUTE_VALUE_MAX) {
        send_route_reply (router_, sender_id_, discovery_protocol::status_invalid, NULL, NULL,
                          "value too large");
        return;
    }
    const std::string channel_name = discovery_protocol::read_string (frames_[4]);
    uint16_t owner_role = 0;
    uint64_t registration_id = 0;
    if (!discovery_protocol::read_u16 (frames_[5], &owner_role)
        || !discovery_protocol::read_u64 (frames_[7], &registration_id)) {
        send_route_reply (router_, sender_id_, discovery_protocol::status_invalid, NULL, NULL,
                          "invalid owner");
        return;
    }
    const std::string owner_endpoint = discovery_protocol::read_string (frames_[6]);

    const uint64_t now_ms = zlink::clock_t ().now_ms ();
    uint8_t status = discovery_protocol::status_ok;
    std::string error;
    {
        scoped_lock_t lock (_sync);
        owner_identity_t owner;
        if (!find_provider_owner_locked (channel_name, owner_role, owner_endpoint, &owner, NULL)) {
            owner.registration_id = 0;
        }
        if (owner.registration_id == 0) {
            status = discovery_protocol::status_not_found;
            error = "owner not found";
        } else if (owner.registration_id != registration_id) {
            status = discovery_protocol::status_rejected;
            error = "stale owner generation";
        } else {
            route_key_t route_key;
            if (!read_route_key (raw_kind, frames_[2], channel_name, &route_key)) {
                status = discovery_protocol::status_invalid;
                error = "invalid route key";
            } else {
                route_entry_t entry;
                entry.key = route_key;
                if (value_size > 0) {
                    const unsigned char *value_data = static_cast<const unsigned char *> (
                      zlink_msg_data (const_cast<zlink_msg_t *> (&frames_[3])));
                    entry.value.assign (value_data, value_data + value_size);
                }
                entry.owner = owner;
                entry.updated_at_ms = now_ms;
                entry.advertising_registry =
                  _coordination_state.registry_id == 0 ? 1 : _coordination_state.registry_id;
                int route_error = 0;
                if (!replace_route_observation_for_advertiser_locked (entry, &route_error)) {
                    status = discovery_protocol::status_invalid;
                    error = route_error == ENOSPC ? "route store full" : "route too large";
                } else {
                    _coordination_state.list_seq++;
                }
            }
        }
    }

    send_route_reply (router_, sender_id_, status, NULL, NULL, error);
}

void zlink::registry_t::handle_unbind_route (void *router_,
                                             const zlink_msg_t *frames_,
                                             size_t frame_count_,
                                             const zlink_routing_id_t &sender_id_)
{
    if (frame_count_ != 7) {
        send_route_reply (router_, sender_id_, discovery_protocol::status_invalid, NULL, NULL,
                          "invalid unbind route");
        return;
    }

    uint32_t raw_kind = 0;
    const std::string channel_name = discovery_protocol::read_string (frames_[3]);
    uint16_t owner_role = 0;
    uint64_t registration_id = 0;
    if (!discovery_protocol::read_u32 (frames_[1], &raw_kind)
        || !discovery_protocol::read_u16 (frames_[4], &owner_role)
        || !discovery_protocol::read_u64 (frames_[6], &registration_id)) {
        send_route_reply (router_, sender_id_, discovery_protocol::status_invalid, NULL, NULL,
                          "invalid unbind route");
        return;
    }
    const std::string owner_endpoint = discovery_protocol::read_string (frames_[5]);

    uint8_t status = discovery_protocol::status_ok;
    std::string error;
    {
        scoped_lock_t lock (_sync);
        owner_identity_t owner;
        zlink_routing_id_t owner_rid;
        if (!find_provider_owner_locked (channel_name, owner_role, owner_endpoint, &owner,
                                         &owner_rid))
            owner.registration_id = 0;
        if (owner.registration_id == 0) {
            status = discovery_protocol::status_not_found;
            error = "owner not found";
        } else if (owner.registration_id != registration_id) {
            status = discovery_protocol::status_rejected;
            error = "stale owner generation";
        } else {
            route_key_t route_key;
            if (!read_route_key (raw_kind, frames_[2], channel_name, &route_key)) {
                status = discovery_protocol::status_invalid;
                error = "invalid route key";
            } else {
                const uint32_t advertising_registry =
                  _coordination_state.registry_id == 0 ? 1 : _coordination_state.registry_id;
                int route_error = 0;
                if (!erase_route_observation_for_owner_locked (route_key, owner,
                                                               advertising_registry,
                                                               &route_error)) {
                    status = discovery_protocol::status_not_found;
                    error = "route not found";
                } else {
                    _coordination_state.list_seq++;
                }
            }
        }
    }

    send_route_reply (router_, sender_id_, status, NULL, NULL, error);
}

void zlink::registry_t::handle_resolve_route (void *router_,
                                              const zlink_msg_t *frames_,
                                              size_t frame_count_,
                                              const zlink_routing_id_t &sender_id_)
{
    if (frame_count_ < 4) {
        send_route_reply (router_, sender_id_, discovery_protocol::status_invalid, NULL, NULL,
                          "invalid resolve route");
        return;
    }

    uint32_t raw_kind = 0;
    if (!discovery_protocol::read_u32 (frames_[1], &raw_kind)) {
        send_route_reply (router_, sender_id_, discovery_protocol::status_invalid, NULL, NULL,
                          "invalid kind");
        return;
    }
    const std::string channel_name = discovery_protocol::read_string (frames_[3]);

    uint8_t status = discovery_protocol::status_not_found;
    zlink_routing_id_t owner_rid;
    memset (&owner_rid, 0, sizeof (owner_rid));
    std::vector<unsigned char> value;
    std::string error = "route not found";
    {
        scoped_lock_t lock (_sync);
        route_key_t route_key;
        if (!read_route_key (raw_kind, frames_[2], channel_name, &route_key)) {
            status = discovery_protocol::status_invalid;
            error = "invalid route key";
        } else {
            materialize_route_winner_locked (route_key);
            route_map_t::const_iterator it = _projection_state.routes.find (route_key);
            if (it != _projection_state.routes.end () && owner_is_live_locked (it->owner)
                && owner_routing_id_from_key (it->owner, &owner_rid)) {
                status = discovery_protocol::status_ok;
                value = it->value;
                error.clear ();
            }
        }
    }

    send_route_reply (router_, sender_id_, status, &owner_rid, &value, error);
}
