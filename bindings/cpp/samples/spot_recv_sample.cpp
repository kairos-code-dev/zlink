/* SPDX-License-Identifier: MPL-2.0 */

#include "sample_common.hpp"

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

    zlink::service_monitor_handle_t sub_monitor =
      sub_spot.monitor_open (
        zlink::service_monitor_event::spot_filter_applied
        | zlink::service_monitor_event::spot_subscription_ready_changed
        | zlink::service_monitor_event::error);
    assert (sub_monitor.valid ());
    zlink::service_monitor_handle_t pub_monitor =
      pub_spot.monitor_open (
        zlink::service_monitor_event::spot_first_delivery_ready_changed
        | zlink::service_monitor_event::error);
    assert (pub_monitor.valid ());

    assert (pub_node.bind ("tcp://127.0.0.1:0") == 0);
    const std::string endpoint = pub_node.last_endpoint ();
    assert (!endpoint.empty ());
    assert (sub_node.connect_peer (endpoint) == 0);

    const std::string topic = detail::k_spot_topic;
    assert (sub_spot.set_subscription (topic) == 0);
    assert (detail::wait_spot_ready (sub_monitor, pub_monitor, endpoint));

    const std::string sent = detail::k_spot_payload;
    zlink::message_t outbound = detail::make_message (sent);
    pub_spot.publish (topic, outbound);

    const zlink::subscribed_t inbound = sub_spot.subscribe ();
    assert (inbound.topic == topic);
    assert (inbound.parts.size () == 1);
    const std::string received = inbound.parts[0].to_string ();
    assert (received == detail::k_spot_payload);
    std::printf (
      "[spot/recv] publish: \"%s/%s\" → subscribe: \"%s/%s\"\n",
      topic.c_str (), sent.c_str (), inbound.topic.c_str (), received.c_str ());
    assert (pub_monitor.close () == 0);
    assert (sub_monitor.close () == 0);
    assert (sub_spot.close () == 0);
    assert (pub_spot.close () == 0);
    assert (sub_node.close () == 0);
    assert (pub_node.close () == 0);
    return 0;
}
