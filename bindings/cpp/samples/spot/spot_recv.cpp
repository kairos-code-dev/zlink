/* SPDX-License-Identifier: MPL-2.0 */

#include "../common/sample_common.hpp"

int main ()
{
    zlink::context_t ctx;
    zlink::service::spot_t spot (ctx);
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
    assert (zlink_cpp_sample::wait_for_service_monitor_event (
      sub_monitor,
      static_cast<uint32_t> (
        zlink::service_monitor_event::spot_filter_applied),
      10000));
    assert (zlink_cpp_sample::wait_for_service_monitor_state (
      pub_monitor, ZLINK_MONITOR_STATE_SEND_READY, 10000));

    zlink::message_t outbound =
      zlink_cpp_sample::make_message ("spot-recv");
    assert (spot.publish ("topic:alpha", outbound) == 0);

    zlink::message_t inbound;
    std::string topic;
    assert (spot.recv (inbound, topic) == 0);
    assert (topic == "topic:alpha");
    assert (inbound.to_string () == "spot-recv");
    assert (pub_monitor.close () == 0);
    assert (sub_monitor.close () == 0);
    assert (spot.destroy () == 0);
    assert (ctx.shutdown () == 0);
    assert (ctx.term () == 0);
    return 0;
}
