/* SPDX-License-Identifier: MPL-2.0 */

#include "sample_common.hpp"

#include <optional>
#include <thread>

int main ()
{
    zlink::context_t ctx;
    zlink::service::spot_node_t node (ctx);
    zlink::service::spot_t spot = node.create_spot ();
    zlink::pub_socket_t pub_socket (ctx);
    zlink::sub_socket_t sub_socket (ctx);
    assert (node.valid ());
    assert (spot.valid ());
    assert (pub_socket.valid ());
    assert (sub_socket.valid ());

    const std::string service_name = detail::k_spot_service;
    const std::string service_endpoint = detail::unique_tcp ("spot-recv-service");
    assert (pub_socket.bind (service_endpoint) == 0);
    assert (sub_socket.connect (service_endpoint) == 0);
    node.attach_pubsub (service_name, pub_socket, sub_socket);

    const std::string topic = detail::k_spot_topic;
    const std::string sent = detail::k_spot_payload;
    spot.set_subscription (topic);

    const auto deadline =
      std::chrono::steady_clock::now () + std::chrono::seconds (5);
    std::optional<zlink::topic_message_t> inbound;
    while (std::chrono::steady_clock::now () < deadline) {
        zlink::message_t outbound = detail::make_message (sent);
        spot.publish (service_name, topic, outbound);
        try {
            inbound = spot.subscribe (zlink::recv_flags_t::dontwait);
            break;
        }
        catch (const zlink::recv_error_t &err) {
            if (err.result () != zlink::recv_result_t::no_data)
                throw;
            std::this_thread::sleep_for (std::chrono::milliseconds (1));
        }
    }

    assert (inbound.has_value ());
    assert (inbound->service_name ());
    assert (*inbound->service_name () == service_name);
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
