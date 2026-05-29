/* SPDX-License-Identifier: MPL-2.0 */

#include "utils/precompiled.hpp"

#include "services/spot/node/spot_node.hpp"
#include "services/spot/runtime/spot_handle.hpp"

#include "utils/routing_id.hpp"

namespace zlink
{
std::shared_ptr<spot_logical_state_t> spot_node_t::get_or_new_spot_state (
  const zlink_routing_id_t *spot_rid_, bool *created_out_)
{
    if (created_out_)
        *created_out_ = false;
    if (!spot_rid_ || !zlink::valid_routing_id (*spot_rid_)) {
        errno = EINVAL;
        return std::shared_ptr<spot_logical_state_t> ();
    }

    std::shared_ptr<spot_logical_state_t> state;
    const std::string key = zlink::routing_id_key (*spot_rid_);
    std::unique_lock<mutex_t> lock (_sync);
    for (;;) {
        const std::map<std::string, std::shared_ptr<spot_logical_state_t> >::iterator
          it = _handle_state.spots_by_rid.find (key);
        if (it != _handle_state.spots_by_rid.end ()) {
            state = it->second;
            return state;
        }

        if (_handle_state.pending_spot_creations.insert (key).second) {
            state = create_logical_spot_state_locked (false, spot_rid_, false);
            if (!state) {
                _handle_state.pending_spot_creations.erase (key);
                lock.unlock ();
                _spot_creation_cv.notify_all ();
                return std::shared_ptr<spot_logical_state_t> ();
            }
            if (created_out_)
                *created_out_ = true;
            return state;
        }

        _spot_creation_cv.wait (lock);
    }
}

bool spot_node_t::publish_get_or_new_spot_state (
  const std::shared_ptr<spot_logical_state_t> &state_)
{
    if (!state_)
        return false;

    bool publish_summary = false;
    bool published = false;
    const std::string key = zlink::routing_id_key (state_->routing_id);
    {
        scoped_lock_t lock (_sync);
        std::map<std::string, std::shared_ptr<spot_logical_state_t> >::iterator
          it = _handle_state.spots_by_rid.find (key);
        if (it == _handle_state.spots_by_rid.end ()) {
            _handle_state.spots_by_rid[key] = state_;
            published = true;
            publish_summary = spot_owner_summary_publishable_locked ();
        }
        _handle_state.pending_spot_creations.erase (key);
    }
    _spot_creation_cv.notify_all ();

    if (published && publish_summary)
        submit_spot_owner_summary (state_, ZLINK_TOPOLOGY_STATE_READY, 0);
    return published;
}

void spot_node_t::cancel_get_or_new_spot_state (
  const std::shared_ptr<spot_logical_state_t> &state_)
{
    if (!state_)
        return;

    const std::string key = zlink::routing_id_key (state_->routing_id);
    {
        scoped_lock_t lock (_sync);
        _handle_state.pending_spot_creations.erase (key);
    }
    _spot_creation_cv.notify_all ();
}
}
