/* SPDX-License-Identifier: MPL-2.0 */

#ifndef __ZLINK_SPOT_NODE_ACCESS_HPP_INCLUDED__
#define __ZLINK_SPOT_NODE_ACCESS_HPP_INCLUDED__

namespace zlink
{
class spot_runtime_t;
class spot_internal_receiver_t;
class spot_node_t;

struct spot_node_access_t
{
    static spot_runtime_t *runtime (spot_node_t *node_);
    static spot_internal_receiver_t *ensure_internal_receiver (spot_node_t *node_);
    static spot_internal_receiver_t *internal_receiver (spot_node_t *node_);
    static void wake_control_task (spot_node_t *node_);
    static void schedule_subscription_replay (spot_node_t *node_);
};
}

#endif
