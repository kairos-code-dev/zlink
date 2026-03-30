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

    assert (subscriber.set_subscription ("topic:alpha") == 0);

    const zlink::subscription_event_t event =
      publisher.receive_subscription_event ();
    assert (event.subscribed);
    assert (event.topic == "topic:alpha");

    zlink::message_t outbound =
      detail::make_message ("pubsub-recv");
    publisher.publish ("topic:alpha", outbound);

    const zlink::subscribed_t inbound = subscriber.subscribe ();
    assert (inbound.topic == "topic:alpha");
    assert (inbound.parts.size () == 1);
    assert (inbound.parts[0].to_string () == "pubsub-recv");
    return 0;
}
