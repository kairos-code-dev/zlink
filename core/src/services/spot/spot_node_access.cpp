/* SPDX-License-Identifier: MPL-2.0 */

#include "utils/precompiled.hpp"

#include "services/spot/spot_node_access.hpp"

#include "services/spot/spot_node.hpp"

namespace zlink
{
spot_runtime_t *spot_node_access_t::runtime (spot_node_t *node_)
{
    return node_ ? node_->runtime () : NULL;
}

spot_internal_receiver_t *
spot_node_access_t::ensure_internal_receiver (spot_node_t *node_)
{
    return node_ ? node_->ensure_internal_receiver () : NULL;
}

spot_internal_receiver_t *spot_node_access_t::internal_receiver (spot_node_t *node_)
{
    return node_ ? node_->internal_receiver () : NULL;
}

void spot_node_access_t::wake_control_task (spot_node_t *node_)
{
    if (node_)
        node_->wake_control_task ();
}

void spot_node_access_t::schedule_subscription_replay (spot_node_t *node_)
{
    if (node_)
        node_->schedule_subscription_replay ();
}
}
