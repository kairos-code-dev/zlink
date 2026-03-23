/* SPDX-License-Identifier: MPL-2.0 */

#include "utils/precompiled.hpp"

#include "api/monitor_api_internal.hpp"

#include "services/spot/spot_internal_receiver.hpp"
#include "services/spot/spot_node.hpp"
#include "services/spot/spot_node_access.hpp"
#include "services/spot/spot_pub.hpp"

bool has_open_spot_node_monitor_child (zlink::spot_node_t *node_)
{
    if (!node_)
        return false;
    if (has_open_service_monitor_for_subject (node_))
        return true;

    zlink::spot_internal_receiver_t *receiver =
      zlink::spot_node_access_t::internal_receiver (node_);
    if (receiver && has_open_service_monitor_for_subject (receiver))
        return true;

    zlink::spot_pub_t *pub = node_->default_pub ();
    return pub && has_open_service_monitor_for_subject (pub);
}

bool in_spot_node_monitor_callback (zlink::spot_node_t *node_)
{
    if (!node_ || !g_current_monitor_handler_state
        || !g_current_monitor_handler_state->service) {
        return false;
    }

    void *subject =
      g_current_monitor_handler_state->snapshot_subject.load (
        std::memory_order_acquire);
    if (!subject)
        return false;
    if (subject == node_)
        return true;

    zlink::spot_internal_receiver_t *receiver =
      zlink::spot_node_access_t::internal_receiver (node_);
    if (receiver && subject == receiver)
        return true;

    zlink::spot_pub_t *pub = node_->default_pub ();
    return pub && subject == pub;
}
