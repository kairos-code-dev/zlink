/* SPDX-License-Identifier: MPL-2.0 */

#include "../common/sample_common.hpp"

int main ()
{
    zlink::context_t ctx;
    zlink::xpub_socket_t publisher (ctx);
    zlink::sub_socket_t subscriber (ctx);
    zlink::monitor_handle_t pub_monitor = publisher.monitor_handle ();
    zlink::monitor_handle_t sub_monitor = subscriber.monitor_handle ();

    const std::string endpoint = detail::unique_tcp ("pubsub-recv");
    assert (publisher.bind (endpoint) == 0);
    assert (subscriber.connect (endpoint) == 0);
    assert (detail::wait_for_socket_monitor_event (
      pub_monitor,
      static_cast<uint64_t> (zlink::monitor_event::connection_ready_changed),
      2000, 1));
    assert (detail::wait_for_socket_monitor_event (
      sub_monitor,
      static_cast<uint64_t> (zlink::monitor_event::connection_ready_changed),
      2000, 1));

    const std::string topic = "prices";
    assert (subscriber.set_subscription (topic) == 0);

    const zlink::subscription_event_t event =
      publisher.receive_subscription_event ();
    assert (event.subscribed);
    assert (event.topic == topic);

    const std::string sent = "101.25";
    zlink::message_t outbound = detail::make_message (sent);
    publisher.publish (topic, outbound);

    const zlink::subscribed_t inbound = subscriber.subscribe ();
    assert (inbound.topic == topic);
    assert (inbound.parts.size () == 1);
    const std::string received = inbound.parts[0].to_string ();
    assert (received == "101.25");
    std::printf ("[pubsub/recv] publish: \"%s/%s\" → subscribe: \"%s/%s\"\n",
                 topic.c_str (), sent.c_str (), topic.c_str (), received.c_str ());
    return 0;
}
