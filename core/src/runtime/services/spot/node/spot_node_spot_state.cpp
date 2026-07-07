/* SPDX-License-Identifier: MPL-2.0 */

#include "utils/precompiled.hpp"

#include "services/actor/service_spot_actor_internal.hpp"
#include "services/spot/node/spot_node.hpp"
#include "services/spot/runtime/spot_handle.hpp"
#include "utils/routing_id.hpp"

namespace zlink
{

int spot_node_t::try_register_spot_facade (spot_handle_t *spot_)
{
    if (!spot_) {
        errno = EFAULT;
        return -1;
    }

    scoped_lock_t lock (_sync);
    if (_handle_state.facades.insert (spot_).second)
        return 0;

    errno = EBUSY;
    return -1;
}

void spot_node_t::unregister_spot_facade (spot_handle_t *spot_)
{
    if (!spot_)
        return;

    std::shared_ptr<spot_logical_state_t> removed_state;
    if (spot_->logical_state) {
        scoped_lock_t pubsub_lock (spot_->logical_state->pubsub_sync);
        if (spot_->logical_state->send_ready_subject.load (std::memory_order_acquire) == spot_) {
            spot_->logical_state->send_ready_handler.store (NULL, std::memory_order_release);
            spot_->logical_state->send_ready_subject.store (NULL, std::memory_order_release);
            spot_->logical_state->send_ready_userdata.store (NULL, std::memory_order_release);
        }
    }
    {
        scoped_lock_t lock (_sync);
        _handle_state.facades.erase (spot_);
        if (!spot_->logical_state || spot_->logical_state->entry)
            return;
        for (std::set<spot_handle_t *>::const_iterator it = _handle_state.facades.begin ();
             it != _handle_state.facades.end (); ++it) {
            if ((*it)->logical_state == spot_->logical_state)
                return;
        }
        removed_state = spot_->logical_state;
        _handle_state.spots_by_rid.erase (zlink::routing_id_key (removed_state->routing_id));
    }
    submit_spot_owner_summary (removed_state, ZLINK_TOPOLOGY_STATE_STOPPED, 0);
}

bool spot_node_t::is_last_spot_facade_for_logical_state (spot_handle_t *spot_)
{
    if (!spot_ || !spot_->logical_state)
        return true;

    scoped_lock_t lock (_sync);
    for (std::set<spot_handle_t *>::const_iterator it = _handle_state.facades.begin ();
         it != _handle_state.facades.end (); ++it) {
        if (*it != spot_ && (*it)->logical_state == spot_->logical_state)
            return false;
    }
    return true;
}

std::shared_ptr<spot_logical_state_t> spot_node_t::create_user_spot_state ()
{
    bool publish_summary = false;
    std::shared_ptr<spot_logical_state_t> state;
    {
        scoped_lock_t lock (_sync);
        state = create_logical_spot_state_locked (false);
        if (!state)
            return std::shared_ptr<spot_logical_state_t> ();
        publish_summary = spot_owner_summary_publishable_locked ();
    }
    if (publish_summary)
        submit_spot_owner_summary (state, ZLINK_TOPOLOGY_STATE_READY, 0);
    return state;
}

std::shared_ptr<spot_logical_state_t> spot_node_t::entry_spot_state ()
{
    bool publish_summary = false;
    std::shared_ptr<spot_logical_state_t> state;
    {
        scoped_lock_t lock (_sync);
        if (_handle_state.entry_spot)
            return _handle_state.entry_spot;

        state = create_logical_spot_state_locked (true);
        if (!state)
            return std::shared_ptr<spot_logical_state_t> ();
        publish_summary = spot_owner_summary_publishable_locked ();
    }
    if (publish_summary)
        submit_spot_owner_summary (state, ZLINK_TOPOLOGY_STATE_READY, 0);
    return state;
}

std::shared_ptr<spot_logical_state_t> spot_node_t::create_logical_spot_state_locked (
  bool entry_, const zlink_routing_id_t *spot_rid_, bool publish_)
{
    std::shared_ptr<spot_logical_state_t> state (new (std::nothrow) spot_logical_state_t ());
    if (!state) {
        errno = ENOMEM;
        return std::shared_ptr<spot_logical_state_t> ();
    }
    state->node = this;
    state->stable_id = _handle_state.next_spot_stable_id++;
    if (spot_rid_)
        state->routing_id = *spot_rid_;
    else
        generate_random_uuid_routing_id (&state->routing_id);
    state->entry = entry_;
    state->rid_locked = entry_ && _handle_state.entry_spot_rid_locked;

    const std::string key = zlink::routing_id_key (state->routing_id);
    if (key.empty ()) {
        errno = EFAULT;
        return std::shared_ptr<spot_logical_state_t> ();
    }
    if (publish_) {
        if (entry_)
            _handle_state.entry_spot = state;
        _handle_state.spots_by_rid[key] = state;
    }
    return state;
}

bool spot_node_t::spot_owner_summary_publishable_locked () const
{
    return false;
}

void spot_node_t::lock_entry_spot_rid ()
{
    scoped_lock_t lock (_sync);
    _handle_state.entry_spot_rid_locked = true;
    if (_handle_state.entry_spot)
        _handle_state.entry_spot->rid_locked = true;
}

std::shared_ptr<spot_logical_state_t>
spot_node_t::lookup_spot_state (const zlink_routing_id_t *spot_rid_)
{
    if (!spot_rid_ || !zlink::valid_routing_id (*spot_rid_)) {
        errno = EINVAL;
        return std::shared_ptr<spot_logical_state_t> ();
    }
    scoped_lock_t lock (_sync);
    const std::map<std::string, std::shared_ptr<spot_logical_state_t>>::iterator it =
      _handle_state.spots_by_rid.find (zlink::routing_id_key (*spot_rid_));
    if (it == _handle_state.spots_by_rid.end ()) {
        errno = ENOENT;
        return std::shared_ptr<spot_logical_state_t> ();
    }
    return it->second;
}

void spot_node_t::remove_spot_state_if_unfacaded (
  const std::shared_ptr<spot_logical_state_t> &state_)
{
    if (!state_ || state_->entry)
        return;

    bool removed = false;
    {
        scoped_lock_t lock (_sync);
        for (std::set<spot_handle_t *>::const_iterator it = _handle_state.facades.begin ();
             it != _handle_state.facades.end (); ++it) {
            if ((*it)->logical_state == state_)
                return;
        }

        const std::string key = zlink::routing_id_key (state_->routing_id);
        std::map<std::string, std::shared_ptr<spot_logical_state_t>>::iterator it =
          _handle_state.spots_by_rid.find (key);
        if (it != _handle_state.spots_by_rid.end () && it->second == state_) {
            _handle_state.spots_by_rid.erase (it);
            removed = true;
        }
    }
    if (removed)
        submit_spot_owner_summary (state_, ZLINK_TOPOLOGY_STATE_STOPPED, 0);
}

void spot_node_t::snapshot_spot_states (
  std::vector<std::shared_ptr<spot_logical_state_t>> *out_) const
{
    if (!out_)
        return;

    scoped_lock_t lock (_sync);
    out_->reserve (out_->size () + _handle_state.spots_by_rid.size ());
    for (std::map<std::string, std::shared_ptr<spot_logical_state_t>>::const_iterator it =
           _handle_state.spots_by_rid.begin ();
         it != _handle_state.spots_by_rid.end (); ++it) {
        out_->push_back (it->second);
    }
    std::sort (out_->begin (), out_->end (),
               [] (const std::shared_ptr<spot_logical_state_t> &lhs_,
                   const std::shared_ptr<spot_logical_state_t> &rhs_) {
                   const uint64_t lhs_id = lhs_ ? lhs_->stable_id : 0;
                   const uint64_t rhs_id = rhs_ ? rhs_->stable_id : 0;
                   return lhs_id < rhs_id;
               });
}

int spot_node_t::update_spot_routing_id (spot_handle_t *spot_, const void *data_, size_t size_)
{
    if (!spot_ || spot_->node != this || !spot_->logical_state) {
        errno = EFAULT;
        return -1;
    }
    if (!data_ || size_ == 0 || size_ > sizeof (spot_->spot_routing_id.data)) {
        errno = EINVAL;
        return -1;
    }

    zlink_routing_id_t next;
    memset (&next, 0, sizeof (next));
    next.size = static_cast<uint8_t> (size_);
    memcpy (next.data, data_, size_);

    if (spot_->logical_state->entry && spot_actor_internal::node_has_any_actor (this) != 0) {
        errno = EBUSY;
        return -1;
    }
    zlink_routing_id_t old_rid;
    memset (&old_rid, 0, sizeof (old_rid));
    bool publish_summary = false;
    {
        scoped_lock_t lock (_sync);
        if (spot_->logical_state->entry && spot_->logical_state->rid_locked) {
            errno = EBUSY;
            return -1;
        }
        old_rid = spot_->logical_state->routing_id;
        const std::string old_key = zlink::routing_id_key (old_rid);
        const std::string new_key = zlink::routing_id_key (next);
        std::map<std::string, std::shared_ptr<spot_logical_state_t>>::const_iterator existing =
          _handle_state.spots_by_rid.find (new_key);
        if (existing != _handle_state.spots_by_rid.end ()
            && existing->second != spot_->logical_state) {
            errno = EADDRINUSE;
            return -1;
        }
        if (new_key != old_key && _handle_state.pending_spot_creations.count (new_key) != 0) {
            errno = EBUSY;
            return -1;
        }

        if (!old_key.empty ())
            _handle_state.spots_by_rid.erase (old_key);
        spot_->logical_state->routing_id = next;
        _handle_state.spots_by_rid[new_key] = spot_->logical_state;
        for (std::set<spot_handle_t *>::iterator it = _handle_state.facades.begin ();
             it != _handle_state.facades.end (); ++it) {
            if ((*it)->logical_state == spot_->logical_state)
                (*it)->spot_routing_id = next;
        }
        publish_summary = false;
    }
    if (publish_summary) {
        submit_spot_owner_summary_for_rid (
          old_rid, spot_->logical_state->entry ? ZLINK_SPOT_KIND_ENTRY : ZLINK_SPOT_KIND_USER,
          ZLINK_TOPOLOGY_STATE_STOPPED, 0);
        submit_spot_owner_summary (spot_->logical_state, ZLINK_TOPOLOGY_STATE_READY, 0);
    }
    return 0;
}

}
