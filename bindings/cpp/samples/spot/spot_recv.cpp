/* SPDX-License-Identifier: MPL-2.0 */

#include "../common/sample_common.hpp"


int main ()
{
    zlink::context_t ctx;
    zlink::service::spot_node_t pub_node (ctx);
    zlink::service::spot_node_t sub_node (ctx);
    assert (pub_node.valid ());
    assert (sub_node.valid ());

    zlink::service::spot_t pub_spot (pub_node);
    zlink::service::spot_t sub_spot (sub_node);
    assert (pub_spot.valid ());
    assert (sub_spot.valid ());

    zlink::service_monitor_handle_t sub_monitor (
      sub_spot, zlink::service_monitor_event::spot_filter_applied
                  | zlink::service_monitor_event::spot_subscription_ready_changed
                  | zlink::service_monitor_event::error);
    assert (sub_monitor.valid ());
    zlink::service_monitor_handle_t pub_monitor (
      pub_spot.handle (),
      zlink::service_monitor_event::spot_first_delivery_ready_changed
        | zlink::service_monitor_event::error);
    assert (pub_monitor.valid ());

    const std::string endpoint = detail::unique_tcp ("spot-recv");
    assert (pub_node.bind (endpoint) == 0);
    assert (sub_node.connect_peer (endpoint) == 0);

    const std::string topic = "room:lobby";
    assert (sub_spot.subscribe (topic) == 0);
    assert (detail::wait_for_service_monitor_event (
      sub_monitor,
      static_cast<uint32_t> (
        zlink::service_monitor_event::spot_filter_applied),
      10000));
    assert (detail::wait_for_service_monitor_event_endpoint (
      sub_monitor,
      static_cast<uint32_t> (
        zlink::service_monitor_event::spot_subscription_ready_changed),
      endpoint,
      10000));
    assert (detail::wait_for_service_monitor_state (
      pub_monitor, ZLINK_MONITOR_STATE_SEND_READY, 10000));

    const std::string sent = "hello-spot";
    zlink::message_t outbound = detail::make_message (sent);
    pub_spot.publish (topic, outbound);

    const zlink::subscribed_t inbound = sub_spot.receive ();
    assert (inbound.topic == topic);
    assert (inbound.parts.size () == 1);
    const std::string received = inbound.parts[0].to_string ();
    assert (received == "hello-spot");
    std::printf (
      "[spot/recv] publish: \"%s/%s\" → subscribe: \"%s/%s\"\n",
      topic.c_str (), sent.c_str (), inbound.topic.c_str (), received.c_str ());
    assert (pub_monitor.close () == 0);
    assert (sub_monitor.close () == 0);
    assert (sub_spot.destroy () == 0);
    assert (pub_spot.destroy () == 0);
    assert (sub_node.destroy () == 0);
    assert (pub_node.destroy () == 0);
    return 0;
}
