/* SPDX-License-Identifier: MPL-2.0 */

#ifndef __ZLINK_SPOT_NODE_ACCESS_HPP_INCLUDED__
#define __ZLINK_SPOT_NODE_ACCESS_HPP_INCLUDED__

#include "services/spot/spot_node.hpp"

namespace zlink
{
struct spot_node_access_t
{
    static spot_runtime_t *runtime (spot_node_t *node_)
    {
        return node_ ? node_->runtime () : NULL;
    }

    static spot_internal_receiver_t *ensure_internal_receiver (spot_node_t *node_)
    {
        return node_ ? node_->ensure_internal_receiver () : NULL;
    }

    static spot_internal_receiver_t *internal_receiver (spot_node_t *node_)
    {
        return node_ ? node_->internal_receiver () : NULL;
    }

    static void wake_control_task (spot_node_t *node_)
    {
        if (node_)
            node_->wake_control_task ();
    }

    static void schedule_subscription_replay (spot_node_t *node_)
    {
        if (node_)
            node_->schedule_subscription_replay ();
    }
};
}

#endif
