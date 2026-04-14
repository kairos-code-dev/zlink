/* SPDX-License-Identifier: MPL-2.0 */

#include "sample_common.hpp"

int main ()
{
    zlink::context_t ctx;
    zlink::service::spot_node_t pub_node (ctx);
    zlink::service::spot_node_t sub_node (ctx);
    assert (pub_node.valid ());
    assert (sub_node.valid ());

    zlink::service::spot_t pub_spot = pub_node.create_spot ();
    zlink::service::spot_t sub_spot = sub_node.create_spot ();
    assert (pub_spot.valid ());
    assert (sub_spot.valid ());

    pub_node.bind ("tcp://127.0.0.1:0");
    const std::string endpoint = pub_node.last_endpoint ();
    assert (!endpoint.empty ());
    sub_node.connect_peer (endpoint);

    const std::string topic = detail::k_spot_topic;
    sub_spot.set_subscription (topic);

    const std::string sent = detail::k_spot_payload;
    zlink::message_t outbound = detail::make_message (sent);
    pub_spot.publish (topic, outbound);

    const zlink::topic_message_t inbound = sub_spot.subscribe ();
    assert (inbound.topic () == topic);
    assert (inbound.parts ().size () == 1);
    const std::string received = inbound.parts ()[0].to_string ();
    assert (received == detail::k_spot_payload);
    std::printf (
      "[spot/recv] publish: \"%s/%s\" → subscribe: \"%s/%s\"\n",
      topic.c_str (), sent.c_str (), inbound.topic ().c_str (),
      received.c_str ());
    sub_spot.close ();
    pub_spot.close ();
    sub_node.close ();
    pub_node.close ();
    return 0;
}
