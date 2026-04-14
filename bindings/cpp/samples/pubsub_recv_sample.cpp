/* SPDX-License-Identifier: MPL-2.0 */

#include "sample_common.hpp"

int main ()
{
    zlink::context_t ctx;
    zlink::xpub_socket_t publisher (ctx);
    zlink::sub_socket_t subscriber (ctx);
    zlink::monitor_handle_t pub_monitor = publisher.monitor_handle ();
    zlink::monitor_handle_t sub_monitor = subscriber.monitor_handle ();

    assert (publisher.bind ("tcp://127.0.0.1:0") == 0);
    const std::string endpoint = publisher.options ().last_endpoint ();
    assert (!endpoint.empty ());
    assert (subscriber.connect (endpoint) == 0);
    assert (detail::wait_connected (pub_monitor, sub_monitor));

    const std::string topic = detail::k_pubsub_topic;
    subscriber.set_subscription (topic);

    const zlink::subscription_event_t event =
      publisher.receive_subscription_event ();
    assert (event.subscribed);
    assert (event.topic == topic);

    const std::string sent = detail::k_pubsub_payload;
    zlink::message_t outbound = detail::make_message (sent);
    publisher.publish (topic, outbound);

    const zlink::topic_message_t inbound = subscriber.subscribe ();
    assert (inbound.topic () == topic);
    assert (inbound.parts ().size () == 1);
    const std::string received = inbound.parts ()[0].to_string ();
    assert (received == detail::k_pubsub_payload);
    std::printf ("[pubsub/recv] publish: \"%s/%s\" → subscribe: \"%s/%s\"\n",
                 topic.c_str (), sent.c_str (), topic.c_str (), received.c_str ());
    return 0;
}
