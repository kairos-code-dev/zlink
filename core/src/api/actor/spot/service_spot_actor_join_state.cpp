/* SPDX-License-Identifier: MPL-2.0 */

#include "utils/precompiled.hpp"

#include "services/spot/node/spot_node.hpp"
#include "services/spot/runtime/spot_handle.hpp"
#include "api/actor/spot/service_spot_actor_state_internal.hpp"

#include <algorithm>

namespace zlink
{
namespace spot_actor_api_internal
{

spot_logical_state_t *join_queue_key (const std::shared_ptr<spot_logical_state_t> &state_)
{
    return state_.get ();
}

spot_logical_state_t *join_queue_key (const queued_join_request_t *request_)
{
    if (!request_)
        return NULL;
    if (request_->spot_state)
        return request_->spot_state.get ();
    return request_->spot && request_->spot->logical_state ? request_->spot->logical_state.get ()
                                                           : NULL;
}

std::deque<queued_join_request_t *> &actor_join_state_t::queue_for (spot_logical_state_t *key_)
{
    return queues[key_];
}

bool actor_join_state_t::find_queue (spot_logical_state_t *key_, queue_map_t::iterator *it_)
{
    if (!key_ || !it_)
        return false;
    *it_ = queues.find (key_);
    return *it_ != queues.end ();
}

void actor_join_state_t::erase_queue_if_empty (spot_logical_state_t *key_)
{
    if (!key_)
        return;
    queue_map_t::iterator it = queues.find (key_);
    if (it != queues.end () && it->second.empty ())
        queues.erase (it);
}

void actor_join_state_t::enqueue (queued_join_request_t *request_)
{
    if (request_)
        queue_for (join_queue_key (request_)).push_back (request_);
}

bool actor_join_state_t::peek_for_spot (spot_handle_t *spot_, queued_join_request_t **request_out_)
{
    if (!spot_ || !spot_->logical_state || !request_out_)
        return false;
    queue_map_t::iterator queue_it;
    if (!find_queue (join_queue_key (spot_->logical_state), &queue_it) || queue_it->second.empty ())
        return false;
    *request_out_ = queue_it->second.front ();
    return true;
}

void actor_join_state_t::remove_queued (queued_join_request_t *request_)
{
    spot_logical_state_t *key = join_queue_key (request_);
    if (!request_ || !key)
        return;
    queue_map_t::iterator queue_it;
    if (!find_queue (key, &queue_it))
        return;
    std::deque<queued_join_request_t *> &queue = queue_it->second;
    for (std::deque<queued_join_request_t *>::iterator it = queue.begin (); it != queue.end ();
         ++it) {
        if (*it == request_) {
            queue.erase (it);
            break;
        }
    }
    erase_queue_if_empty (key);
}

void actor_join_state_t::take_queue (spot_logical_state_t *key_,
                                     std::deque<queued_join_request_t *> *pending_)
{
    if (!key_ || !pending_)
        return;
    queue_map_t::iterator queue_it = queues.find (key_);
    if (queue_it == queues.end ())
        return;
    pending_->swap (queue_it->second);
    queues.erase (queue_it);
}

void actor_join_state_t::replace_live_spot (spot_handle_t *from_, spot_handle_t *to_)
{
    for (std::set<queued_join_request_t *>::iterator it = live_requests.begin ();
         it != live_requests.end (); ++it) {
        if ((*it)->spot == from_)
            (*it)->spot = to_;
    }
}

void actor_join_state_t::collect_live_for_state (
  const std::shared_ptr<spot_logical_state_t> &state_,
  std::deque<queued_join_request_t *> *pending_) const
{
    if (!state_ || !pending_)
        return;
    for (std::set<queued_join_request_t *>::const_iterator it = live_requests.begin ();
         it != live_requests.end (); ++it) {
        queued_join_request_t *request = *it;
        if (request->spot_state == state_ && !request->replied
            && std::find (pending_->begin (), pending_->end (), request) == pending_->end ())
            pending_->push_back (request);
    }
}

void actor_join_state_t::drain_queued_for_stream (void *stream_,
                                                  std::deque<queued_join_request_t *> *aborted_)
{
    if (!stream_ || !aborted_)
        return;
    for (queue_map_t::iterator join_it = queues.begin (); join_it != queues.end (); ++join_it) {
        std::deque<queued_join_request_t *> &queue = join_it->second;
        for (std::deque<queued_join_request_t *>::iterator it = queue.begin ();
             it != queue.end ();) {
            queued_join_request_t *request = *it;
            if (request->actor && request->actor->bound_stream == stream_ && !request->replied) {
                it = queue.erase (it);
                aborted_->push_back (request);
                continue;
            }
            ++it;
        }
    }
}

void actor_join_state_t::collect_live_for_stream (
  void *stream_,
  const std::deque<queued_join_request_t *> &already_aborted_,
  std::vector<queued_join_request_t *> *received_aborts_) const
{
    if (!stream_ || !received_aborts_)
        return;
    for (std::set<queued_join_request_t *>::const_iterator it = live_requests.begin ();
         it != live_requests.end (); ++it) {
        queued_join_request_t *request = *it;
        if (request->actor && request->actor->bound_stream == stream_ && !request->replied
            && std::find (already_aborted_.begin (), already_aborted_.end (), request)
                 == already_aborted_.end ())
            received_aborts_->push_back (request);
    }
}

bool actor_join_state_t::spot_has_pending (spot_logical_state_t *key_) const
{
    if (!key_)
        return false;
    queue_map_t::const_iterator queue_it = queues.find (key_);
    if (queue_it != queues.end () && !queue_it->second.empty ())
        return true;
    return pending_count_by_spot.count (key_) != 0;
}

uint32_t actor_join_state_t::pending_count_for_spot (spot_logical_state_t *key_) const
{
    std::map<spot_logical_state_t *, size_t>::const_iterator it = pending_count_by_spot.find (key_);
    if (it == pending_count_by_spot.end ())
        return 0;
    return static_cast<uint32_t> (it->second);
}

bool actor_join_state_t::is_live (queued_join_request_t *request_) const
{
    return request_ && live_requests.count (request_) != 0;
}

void actor_join_state_t::mark_live (queued_join_request_t *request_)
{
    if (request_)
        live_requests.insert (request_);
}

void actor_join_state_t::unmark_live (queued_join_request_t *request_)
{
    live_requests.erase (request_);
}

bool actor_join_state_t::actor_has_pending (const actor_handle_t *actor_) const
{
    return actor_ && pending_count_by_actor.count (const_cast<actor_handle_t *> (actor_)) != 0;
}

void actor_join_state_t::increment_actor_pending (actor_handle_t *actor_)
{
    if (actor_)
        ++pending_count_by_actor[actor_];
}

void actor_join_state_t::decrement_actor_pending (actor_handle_t *actor_)
{
    if (!actor_)
        return;
    std::map<actor_handle_t *, size_t>::iterator it = pending_count_by_actor.find (actor_);
    if (it == pending_count_by_actor.end ())
        return;
    if (it->second <= 1)
        pending_count_by_actor.erase (it);
    else
        --it->second;
}

void actor_join_state_t::increment_spot_pending (spot_logical_state_t *key_)
{
    if (key_)
        ++pending_count_by_spot[key_];
}

void actor_join_state_t::decrement_spot_pending (spot_logical_state_t *key_)
{
    if (!key_)
        return;
    std::map<spot_logical_state_t *, size_t>::iterator it = pending_count_by_spot.find (key_);
    if (it == pending_count_by_spot.end ())
        return;
    if (it->second <= 1)
        pending_count_by_spot.erase (it);
    else
        --it->second;
}

bool actor_join_state_t::has_pending_remote_actor (zlink::spot_node_t *node_,
                                                   const char *actor_id_) const
{
    return node_ && actor_id_
           && pending_remote_actor_keys.count (std::make_pair (node_, std::string (actor_id_)))
                != 0;
}

void actor_join_state_t::track_pending_remote_actor (queued_join_request_t *request_)
{
    if (request_ && request_->remote && request_->target_node
        && request_->target_actor_ref.actor_id[0] != '\0') {
        pending_remote_actor_keys.insert (std::make_pair (
          request_->target_node, std::string (request_->target_actor_ref.actor_id)));
    }
}

void actor_join_state_t::untrack_pending_remote_actor (queued_join_request_t *request_)
{
    if (request_ && request_->remote && request_->target_node
        && request_->target_actor_ref.actor_id[0] != '\0') {
        pending_remote_actor_keys.erase (std::make_pair (
          request_->target_node, std::string (request_->target_actor_ref.actor_id)));
    }
}

}
}
