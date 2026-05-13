/* SPDX-License-Identifier: MPL-2.0 */

#include "utils/precompiled.hpp"

#include "api/service_api_internal.hpp"
#include "api/service_spot_actor_internal.hpp"
#include "zlink_testing.hpp"
#include "services/spot/spot_handle.hpp"
#include "services/spot/spot_node.hpp"
#include "services/spot/spot_pub.hpp"
#include "services/spot/spot_sub.hpp"
#include "services/spot/spot_subject_access.hpp"

namespace zlink
{
service_public_api_guard_t *spot_public_api_guard_for_testing (void *spot_)
{
    spot_handle_t *spot = as_spot_handle (spot_);
    if (spot)
        return &spot->public_api;
    if (zlink::spot_pub_t *pub = as_spot_pub_side_handle (spot_))
        return pub->node () ? &pub->node ()->public_api_guard () : NULL;
    if (zlink::spot_sub_t *sub = as_spot_sub_side_handle (spot_))
        return sub->node () ? &sub->node ()->public_api_guard () : NULL;
    return NULL;
}

void destroy_spot_handle_for_testing (void *spot_)
{
    destroy_spot_handle_internal (spot_);
}

int actor_stream_owner_set_for_testing (void *stream_, void *node_)
{
    return spot_actor_internal::set_stream_owner (stream_, node_);
}
}
