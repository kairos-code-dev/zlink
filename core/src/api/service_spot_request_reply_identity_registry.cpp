/* SPDX-License-Identifier: MPL-2.0 */

#include "utils/precompiled.hpp"

#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

#include "api/service_spot_request_reply_internal.hpp"
#include "api/service_spot_request_reply_utils_internal.hpp"
#include "services/spot/spot_runtime.hpp"

namespace
{
using zlink::spot_reqrep_internal::g_router_state_identity_index;
using zlink::spot_reqrep_internal::g_spot_request_reply_index_mutex;
using zlink::spot_reqrep_internal::g_spot_state_identity_index;
using zlink::spot_reqrep_internal::resolve_spot_runtime;
using zlink::spot_reqrep_internal::router_spot_request_reply_state_t;
using zlink::spot_reqrep_internal::router_state_identity_index_t;
using zlink::spot_reqrep_internal::spot_request_reply_state_t;
using zlink::spot_reqrep_internal::spot_owner_states;
using zlink::spot_reqrep_internal::spot_state_identity_index_t;
using zlink::spot_reqrep_internal::spot_state_spot_index_t;
using zlink::spot_reqrep_internal::routing_pair_t;
using zlink::spot_reqrep_internal::resolve_spot_identity;

zlink::spot_runtime_t *resolve_active_spot_runtime (void *spot_)
{
    zlink::spot_runtime_t *runtime = resolve_spot_runtime (spot_);
    if (!runtime || !runtime->execution.data_plane_running
        || !runtime->internal_router) {
        return NULL;
    }
    return runtime;
}
}

std::shared_ptr<zlink::spot_reqrep_internal::spot_request_reply_state_t>
zlink::spot_reqrep_internal::find_spot_state_by_identity (
  const std::string &node_rid_,
  const std::string &spot_rid_)
{
    std::lock_guard<std::mutex> lock (g_spot_request_reply_index_mutex);
    spot_state_identity_index_t::iterator node_it =
      g_spot_state_identity_index.find (node_rid_);
    if (node_it == g_spot_state_identity_index.end ())
        return std::shared_ptr<spot_request_reply_state_t> ();

    spot_state_spot_index_t::iterator spot_it =
      node_it->second.find (spot_rid_);
    if (spot_it == node_it->second.end ())
        return std::shared_ptr<spot_request_reply_state_t> ();

    std::shared_ptr<spot_request_reply_state_t> state = spot_it->second.lock ();
    if (!state) {
        node_it->second.erase (spot_it);
        if (node_it->second.empty ())
            g_spot_state_identity_index.erase (node_it);
        return std::shared_ptr<spot_request_reply_state_t> ();
    }
    return state;
}

std::vector<std::shared_ptr<zlink::spot_reqrep_internal::spot_request_reply_state_t> >
zlink::spot_reqrep_internal::snapshot_spot_states ()
{
    std::vector<std::shared_ptr<spot_request_reply_state_t> > states;
    std::lock_guard<std::mutex> lock (g_spot_request_reply_index_mutex);
    const std::unordered_map<void *, std::shared_ptr<spot_request_reply_state_t> >
      &owners = spot_owner_states ();
    states.reserve (owners.size ());
    for (std::unordered_map<void *,
                            std::shared_ptr<spot_request_reply_state_t> >::const_iterator
           it = owners.begin ();
         it != owners.end (); ++it) {
        if (it->second)
            states.push_back (it->second);
    }
    return states;
}

std::shared_ptr<zlink::spot_reqrep_internal::router_spot_request_reply_state_t>
zlink::spot_reqrep_internal::find_router_state_by_rid (
  const std::string &router_rid_)
{
    std::lock_guard<std::mutex> lock (g_spot_request_reply_index_mutex);
    router_state_identity_index_t::iterator it =
      g_router_state_identity_index.find (router_rid_);
    if (it == g_router_state_identity_index.end ())
        return std::shared_ptr<router_spot_request_reply_state_t> ();
    std::shared_ptr<router_spot_request_reply_state_t> state = it->second.lock ();
    if (!state) {
        g_router_state_identity_index.erase (it);
        return std::shared_ptr<router_spot_request_reply_state_t> ();
    }
    return state;
}

void zlink::spot_reqrep_internal::unregister_spot_identity (
  const std::shared_ptr<spot_request_reply_state_t> &state_)
{
    if (!state_ || !state_->owner)
        return;

    routing_pair_t identity;
    if (!resolve_spot_identity (state_->owner, &identity))
        return;

    std::lock_guard<std::mutex> lock (g_spot_request_reply_index_mutex);
    spot_state_identity_index_t::iterator node_it =
      g_spot_state_identity_index.find (identity.node_rid);
    if (node_it == g_spot_state_identity_index.end ())
        return;

    spot_state_spot_index_t::iterator spot_it =
      node_it->second.find (identity.spot_rid);
    if (spot_it == node_it->second.end ())
        return;

    std::shared_ptr<spot_request_reply_state_t> indexed = spot_it->second.lock ();
    if (!indexed || indexed == state_)
        node_it->second.erase (spot_it);
    if (node_it->second.empty ())
        g_spot_state_identity_index.erase (node_it);
}

zlink::spot_runtime_t *
zlink::spot_reqrep_internal::resolve_runtime_for_spot_destination (
  const std::string &node_rid_,
  const std::string &spot_rid_)
{
    std::shared_ptr<spot_request_reply_state_t> state =
      find_spot_state_by_identity (node_rid_, spot_rid_);
    if (!state)
        return NULL;
    return resolve_active_spot_runtime (state->owner);
}

void zlink::spot_reqrep_internal::bind_router_state_rid (
  void *router_,
  const std::string &router_rid_,
  const std::shared_ptr<router_spot_request_reply_state_t> &state_)
{
    if (!router_ || !state_)
        return;

    {
        std::lock_guard<std::mutex> state_lock (state_->mutex);
        state_->router_rid = router_rid_;
    }

    std::lock_guard<std::mutex> lock (g_spot_request_reply_index_mutex);
    g_router_state_identity_index[router_rid_] = state_;
}
