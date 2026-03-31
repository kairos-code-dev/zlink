/* SPDX-License-Identifier: MPL-2.0 */

#include "../common/sample_common.hpp"

int main ()
{
    zlink::context_t ctx;
    zlink::service::spot_node_t node (ctx);
    zlink::service::spot_t spot (node);
    assert (spot.valid ());
    zlink::service_monitor_handle_t sub_monitor (
      spot, zlink::service_monitor_event::spot_filter_applied
              | zlink::service_monitor_event::error);
    assert (sub_monitor.valid ());
    zlink::service_monitor_handle_t pub_monitor (
      spot.handle (), zlink::service_monitor_event::spot_first_delivery_ready_changed
                        | zlink::service_monitor_event::error);
    assert (pub_monitor.valid ());

    assert (spot.subscribe ("topic:alpha") == 0);
    assert (detail::wait_for_service_monitor_event (
      sub_monitor,
      static_cast<uint32_t> (
        zlink::service_monitor_event::spot_filter_applied),
      10000));
    assert (detail::wait_for_service_monitor_state (
      pub_monitor, ZLINK_MONITOR_STATE_SEND_READY, 10000));

    zlink::message_t outbound =
      detail::make_message ("spot-recv");
    spot.publish ("topic:alpha", outbound);

    const zlink::subscribed_t inbound = spot.receive ();
    assert (inbound.topic == "topic:alpha");
    assert (inbound.parts.size () == 1);
    assert (inbound.parts[0].to_string () == "spot-recv");
    assert (pub_monitor.close () == 0);
    assert (sub_monitor.close () == 0);
    assert (spot.destroy () == 0);
    assert (ctx.shutdown () == 0);
    assert (ctx.term () == 0);
    return 0;
}
