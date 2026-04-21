/* SPDX-License-Identifier: MPL-2.0 */

#include "sample_common.hpp"

#include <optional>
#include <thread>

int main ()
{
    zlink::context_t ctx;
    zlink::service::registry_t registry (ctx);
    zlink::service::discovery_t discovery (
      ctx, zlink::service_type::spot, detail::k_spot_service);
    zlink::service::spot_node_t topic_node (ctx);
    zlink::service::spot_node_t routed_node (ctx);
    zlink::service::spot_t topic_spot = topic_node.create_spot ();
    zlink::service::spot_t routed_spot = routed_node.create_spot ();
    zlink::router_socket_t router (ctx);
    zlink::timer_t timer = zlink::timer_t::from_spot (topic_spot);
    assert (registry.valid ());
    assert (discovery.valid ());
    assert (topic_node.valid ());
    assert (routed_node.valid ());
    assert (topic_spot.valid ());
    assert (routed_spot.valid ());
    assert (router.valid ());
    assert (timer.valid ());

    const std::string service_name = detail::k_spot_service;
    const std::string topic = detail::k_spot_topic;
    const std::string publish_payload = detail::k_spot_payload;
    const std::string send_payload = "hello-spot-send";
    const std::string registry_pub = detail::unique_tcp ("spot-recv-reg-pub");
    const std::string registry_router =
      detail::unique_tcp ("spot-recv-reg-router");
    const std::string topic_endpoint = detail::unique_tcp ("spot-recv-topic");
    const std::string routed_endpoint = detail::unique_tcp ("spot-recv-routed");

    registry.bind (registry_pub, registry_router);
    discovery.connect_registry (registry_router);
    topic_node.attach_discovery (discovery);
    routed_node.attach_discovery (discovery);
    topic_node.bind (topic_endpoint);
    routed_node.bind (routed_endpoint);

    routed_node.set_routing_id (zlink::routing_id_t::from_bytes (
      reinterpret_cast<const uint8_t *> ("spot-node-sample"), 16));
    routed_spot.set_routing_id (zlink::routing_id_t::from_bytes (
      reinterpret_cast<const uint8_t *> ("spot-route-sample"), 17));
    router.set_routing_id (zlink::routing_id_t::from_bytes (
      reinterpret_cast<const uint8_t *> ("router-dispatch"), 15));
    topic_spot.set_subscription (topic);

    const zlink::routing_id_t node_routing_id = routed_node.routing_id ();
    const zlink::routing_id_t routed_spot_routing_id = routed_spot.routing_id ();

    zlink::message_t published = detail::make_message (publish_payload);
    topic_spot.publish (service_name, topic, published);
    std::optional<zlink::topic_message_t> inbound;
    const std::chrono::steady_clock::time_point subscribe_deadline =
      std::chrono::steady_clock::now () + std::chrono::seconds (5);
    while (std::chrono::steady_clock::now () < subscribe_deadline) {
        inbound = topic_spot.subscribe (zlink::recv_flags_t::dontwait);
        if (inbound.has_value ())
            break;
        std::this_thread::sleep_for (std::chrono::milliseconds (10));
    }
    assert (inbound.has_value ());
    assert (inbound->service_name ());
    assert (*inbound->service_name () == service_name);
    assert (inbound->topic () == topic);
    assert (inbound->parts ().size () == 1);
    assert (inbound->parts ()[0].to_string () == publish_payload);
    inbound->close ();

    try {
        (void) routed_spot.recv_routed (zlink::recv_flags_t::dontwait);
        assert (false);
    } catch (const zlink::recv_error_t &error) {
        assert (error.result () == zlink::recv_result_t::no_data);
    }

    zlink::message_t routed = detail::make_message (send_payload);
    router.send_to_spot (node_routing_id, routed_spot_routing_id, routed);
    std::optional<zlink::received_t> routed_received;
    const std::chrono::steady_clock::time_point routed_deadline =
      std::chrono::steady_clock::now () + std::chrono::seconds (5);
    while (std::chrono::steady_clock::now () < routed_deadline) {
        try {
            routed_received = routed_spot.recv_routed (zlink::recv_flags_t::dontwait);
            break;
        } catch (const zlink::recv_error_t &error) {
            assert (error.result () == zlink::recv_result_t::no_data);
        }
        std::this_thread::sleep_for (std::chrono::milliseconds (10));
    }
    assert (routed_received.has_value ());
    assert (routed_received->parts ().size () == 1);
    assert (routed_received->parts ()[0].to_string () == send_payload);
    routed_received->close ();

    timer.start (20 * 1000 * 1000ULL, 1);
    uint64_t fire_count = 0;
    timer.recv (&fire_count);
    assert (fire_count == 1);

    char timer_tick[32];
    std::snprintf (
      timer_tick, sizeof (timer_tick), "tick-%llu",
      static_cast<unsigned long long> (fire_count));

    std::printf (
      "[spot/recv] service: \"%s\" tick: 1 publish: \"%s/%s\" -> recv: \"%s/%s\"\n",
      service_name.c_str (), topic.c_str (), publish_payload.c_str (),
      topic.c_str (), publish_payload.c_str ());
    std::printf (
      "[spot/recv] service: \"%s\" tick: 1 send: \"%s\" -> recv: \"%s\"\n",
      service_name.c_str (), send_payload.c_str (), send_payload.c_str ());
    std::printf (
      "[spot/recv] service: \"%s\" tick: 1 timer: \"%s\" -> next-round\n",
      service_name.c_str (), timer_tick);
    return 0;
}
