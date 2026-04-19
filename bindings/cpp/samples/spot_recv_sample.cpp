/* SPDX-License-Identifier: MPL-2.0 */

#include "sample_common.hpp"

#include <cerrno>
#include <optional>
#include <thread>

int main ()
{
    zlink::context_t ctx;
    zlink::service::registry_t registry (ctx);
    zlink::service::discovery_t discovery (
      ctx, zlink::service_type::spot, detail::k_spot_service);
    zlink::service::spot_node_t publisher_node (ctx);
    zlink::service::spot_node_t subscriber_node (ctx);
    zlink::service::spot_t publisher = publisher_node.create_spot ();
    zlink::service::spot_t subscriber = subscriber_node.create_spot ();
    assert (discovery.valid ());
    assert (publisher_node.valid ());
    assert (subscriber_node.valid ());
    assert (publisher.valid ());
    assert (subscriber.valid ());

    const std::string service_name = detail::k_spot_service;
    const std::string topic = detail::k_spot_topic;
    const std::string sent = detail::k_spot_payload;
    const std::string registry_pub = detail::unique_tcp ("spot-reg-pub");
    const std::string registry_router = detail::unique_tcp ("spot-reg-router");
    const std::string publisher_endpoint = detail::unique_tcp ("spot-recv-pub");
    const std::string subscriber_endpoint = detail::unique_tcp ("spot-recv-sub");

    registry.bind (registry_pub.c_str (), registry_router.c_str ());
    discovery.connect_registry (registry_router.c_str ());
    publisher_node.attach_discovery (discovery);
    subscriber_node.attach_discovery (discovery);
    publisher_node.bind (publisher_endpoint.c_str ());
    subscriber_node.bind (subscriber_endpoint.c_str ());
    subscriber.set_subscription (topic);

    std::optional<zlink::topic_message_t> inbound;
    const auto deadline =
      std::chrono::steady_clock::now () + std::chrono::seconds (5);
    while (std::chrono::steady_clock::now () < deadline) {
        std::vector<zlink::message_t> outbound;
        outbound.push_back (detail::make_message (sent));
        publisher.publish (service_name, topic, outbound);
        try {
            inbound = subscriber.subscribe (zlink::recv_flags_t::dontwait);
            break;
        } catch (const zlink::recv_error_t &e) {
            const int errnum = zlink_errno ();
            if (e.result () != zlink::recv_result_t::no_data
                && errnum != ENOENT) {
                throw;
            }
            std::this_thread::sleep_for (std::chrono::milliseconds (10));
        }
    }
    assert (inbound.has_value ());
    assert (inbound->topic () == topic);
    assert (inbound->parts ().size () == 1);
    const std::string received = inbound->parts ()[0].to_string ();
    assert (received == sent);
    std::printf (
      "[spot/recv] service: \"%s\" tick: 1 publish: \"%s/%s\" -> recv: \"%s/%s\"\n",
      service_name.c_str (), topic.c_str (), sent.c_str (),
      inbound->topic ().c_str (), received.c_str ());
    return 0;
}
