/* SPDX-License-Identifier: MPL-2.0 */

#include "utils/precompiled.hpp"

#include "services/spot/runtime/spot_handle.hpp"
#include "api/actor/spot/service_spot_actor_state_internal.hpp"

namespace zlink
{
namespace spot_actor_api_internal
{

void actor_lifecycle_state_t::clear (spot_logical_state_t *key_)
{
    queues.erase (key_);
}

void actor_lifecycle_state_t::enqueue (
  spot_logical_state_t *key_, const lifecycle_event_t &event_)
{
    queues[key_].push_back (event_);
}

bool actor_lifecycle_state_t::pop (
  spot_logical_state_t *key_,
  lifecycle_event_t *event_out_)
{
    if (!key_ || !event_out_)
        return false;
    std::map<spot_logical_state_t *, std::deque<lifecycle_event_t> >::iterator
      queue_it = queues.find (key_);
    if (queue_it == queues.end () || queue_it->second.empty ())
        return false;
    *event_out_ = queue_it->second.front ();
    queue_it->second.pop_front ();
    if (queue_it->second.empty ())
        queues.erase (queue_it);
    return true;
}

}
}
