/* SPDX-License-Identifier: MPL-2.0 */

#include "utils/precompiled.hpp"

#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>

#include "api/service/service_api_internal.hpp"
#include "api/spot/service_spot_dispatch_context_internal.hpp"
#include "api/spot/service_spot_request_reply_internal.hpp"
#include "api/spot/service_spot_request_reply_utils_internal.hpp"
#include "api/socket/socket_api_internal.hpp"
#include "core/ctx.hpp"
#include "services/control/service_control_runtime.hpp"
#include "services/spot/spot_handle.hpp"
#include "services/spot/spot_runtime.hpp"

namespace
{
using zlink::spot_reqrep_internal::router_state_identity_index;
using zlink::spot_reqrep_internal::spot_request_reply_index_mutex;
using zlink::spot_reqrep_internal::spot_state_identity_index;
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
