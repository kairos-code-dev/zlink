/* SPDX-License-Identifier: MPL-2.0 */

#include "utils/precompiled.hpp"

#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>

#include "api/service_api_internal.hpp"
#include "api/service_spot_dispatch_context_internal.hpp"
#include "api/service_spot_request_reply_internal.hpp"
#include "api/service_spot_request_reply_utils_internal.hpp"
#include "api/socket_api_internal.hpp"
#include "core/ctx.hpp"
#include "services/control/service_control_runtime.hpp"
#include "services/spot/spot_handle.hpp"
#include "services/spot/spot_runtime.hpp"

namespace
{
using zlink::spot_reqrep_internal::g_router_state_identity_index;
using zlink::spot_reqrep_internal::g_spot_request_reply_index_mutex;
using zlink::spot_reqrep_internal::g_spot_state_identity_index;
using zlink::spot_reqrep_internal::has_valid_routing_id;
using zlink::spot_reqrep_internal::resolve_spot_identity;
using zlink::spot_reqrep_internal::resolve_spot_runtime;
using zlink::spot_reqrep_internal::routing_pair_t;
using zlink::spot_reqrep_internal::router_spot_request_reply_state_t;
using zlink::spot_reqrep_internal::router_state_identity_index_t;
using zlink::spot_reqrep_internal::routing_id_key;
using zlink::spot_reqrep_internal::spot_request_reply_state_t;
using zlink::spot_reqrep_internal::spot_state_identity_index_t;
using zlink::spot_reqrep_internal::spot_state_spot_index_t;

std::unordered_map<void *, std::shared_ptr<spot_request_reply_state_t> >
  g_spot_owner_states;

} // namespace

std::unordered_map<void *,
                   std::shared_ptr<zlink::spot_reqrep_internal::spot_request_reply_state_t> >
  &zlink::spot_reqrep_internal::spot_owner_states ()
{
    return g_spot_owner_states;
}

size_t zlink::spot_reqrep_internal::
  disconnected_routed_recv_queue_count_for_node (spot_node_t *node_)
{
    if (!node_)
        return 0;

    size_t count = 0;
    std::lock_guard<std::mutex> lock (g_spot_request_reply_index_mutex);
    for (std::unordered_map<void *,
                            std::shared_ptr<spot_request_reply_state_t> >::
           const_iterator it = g_spot_owner_states.begin ();
         it != g_spot_owner_states.end (); ++it) {
        const spot_handle_t *spot = static_cast<const spot_handle_t *> (it->first);
        if (!spot || !spot->check_tag () || spot->node != node_ || !it->second)
            continue;
        std::lock_guard<std::mutex> state_lock (it->second->recv.routed_recv_queue.mutex);
        if (it->second->recv.routed_recv_queue.disconnected)
            ++count;
    }
    return count;
}
