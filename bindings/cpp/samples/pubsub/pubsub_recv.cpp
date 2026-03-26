/* SPDX-License-Identifier: MPL-2.0 */

#include "../common/sample_common.hpp"

int main ()
{
    zlink::context_t ctx;
    zlink::socket_t publisher (ctx, zlink::socket_type::xpub);
    zlink::socket_t subscriber (ctx, zlink::socket_type::sub);
    zlink::monitor_handle_t pub_monitor (publisher, zlink::monitor_event::all);
    zlink::monitor_handle_t sub_monitor (subscriber, zlink::monitor_event::all);

    const std::string endpoint = zlink_cpp_sample::unique_tcp ("pubsub-recv");
    assert (publisher.bind (endpoint) == 0);
    assert (subscriber.connect (endpoint) == 0);
    assert (zlink_cpp_sample::wait_for_socket_monitor_event (
      pub_monitor,
      static_cast<uint64_t> (zlink::monitor_event::connection_ready_changed),
      2000, 1));
    assert (zlink_cpp_sample::wait_for_socket_monitor_event (
      sub_monitor,
      static_cast<uint64_t> (zlink::monitor_event::connection_ready_changed),
      2000, 1));

    assert (subscriber.set_subscription ("topic:alpha") == 0);

    bool subscribed = false;
    std::string topic;
    assert (publisher.subscription_event (subscribed, topic) == 0);
    assert (subscribed);
    assert (topic == "topic:alpha");

    zlink::message_t outbound =
      zlink_cpp_sample::make_message ("pubsub-recv");
    assert (publisher.publish ("topic:alpha", outbound) == 0);

    zlink::message_t inbound;
    std::string inbound_topic;
    assert (subscriber.subscribe (inbound_topic, inbound) == 0);
    assert (inbound_topic == "topic:alpha");
    assert (inbound.to_string () == "pubsub-recv");
    return 0;
}
