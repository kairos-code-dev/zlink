/* SPDX-License-Identifier: MPL-2.0 */

#include "sample_common.hpp"

#include <optional>
#include <thread>

int main ()
{
    zlink::context_t ctx;
    zlink::service::registry_t registry (ctx);
    zlink::service::discovery_t discovery (
      ctx, zlink::auto_connect_type::spot_mesh, detail::k_spot_service);
    zlink::service::spot_node_t topic_node (ctx);
    zlink::service::spot_t topic_spot = topic_node.create_spot ();
    zlink::timer_t timer = zlink::timer_t::from_spot (topic_spot);
    assert (registry.valid ());
    assert (discovery.valid ());
    assert (topic_node.valid ());
    assert (topic_spot.valid ());
    assert (timer.valid ());

    const std::string topic = detail::k_spot_topic;
    const std::string publish_payload = detail::k_spot_payload;
    const std::string registry_pub = detail::unique_tcp ("spot-recv-reg-pub");
    const std::string registry_router =
      detail::unique_tcp ("spot-recv-reg-router");
    const std::string topic_endpoint = detail::unique_tcp ("spot-recv-topic");

    registry.bind (registry_pub, registry_router);
    discovery.connect_registry (registry_router);
    topic_node.attach_discovery (discovery);
    topic_node.bind (topic_endpoint);
    topic_spot.set_subscription (topic);

    zlink::message_t published = detail::make_message (publish_payload);
    topic_spot.publish (topic).message (published).submit ();
    zlink::topic_message_t inbound;
    bool received_topic = false;
    const std::chrono::steady_clock::time_point subscribe_deadline =
      std::chrono::steady_clock::now () + std::chrono::seconds (5);
    while (std::chrono::steady_clock::now () < subscribe_deadline) {
        const int rc = topic_spot.subscribe (inbound, ZLINK_DONTWAIT);
        if (rc == static_cast<int> (zlink::recv_result_t::ok)) {
            received_topic = true;
            break;
        }
        assert (rc == static_cast<int> (zlink::recv_result_t::no_data));
        std::this_thread::sleep_for (std::chrono::milliseconds (10));
    }
    assert (received_topic);
    assert (inbound.topic () == topic);
    assert (inbound.parts ().size () == 1);
    assert (inbound.parts ()[0].to_string () == publish_payload);
    inbound.close ();

    timer.start (std::chrono::milliseconds (20), 1);
    std::optional<uint64_t> fire_count = timer.recv ();
    assert (fire_count && *fire_count == 1);

    char timer_tick[32];
    std::snprintf (
      timer_tick, sizeof (timer_tick), "tick-%llu",
      static_cast<unsigned long long> (*fire_count));

    std::printf (
      "[spot/recv] channel: \"%s\" tick: 1 publish: \"%s/%s\" -> recv: \"%s/%s\"\n",
      detail::k_spot_service, topic.c_str (), publish_payload.c_str (),
      topic.c_str (), publish_payload.c_str ());
    std::printf (
      "[spot/recv] channel: \"%s\" tick: 1 timer: \"%s\" -> next-round\n",
      detail::k_spot_service, timer_tick);
    return 0;
}
