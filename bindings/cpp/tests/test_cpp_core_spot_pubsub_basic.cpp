/* SPDX-License-Identifier: MPL-2.0 */

#include "test_helpers.hpp"

#include <cstring>

static zlink::message_t make_msg (const char *data_, size_t size_)
{
    zlink::message_t msg (size_);
    std::memcpy (msg.data (), data_, size_);
    return msg;
}

static bool recv_spot_with_timeout (zlink::service::spot_t &spot_,
                                    zlink::topic_message_t &message_,
                                    int timeout_ms_)
{
    const auto deadline =
      std::chrono::steady_clock::now () + std::chrono::milliseconds (timeout_ms_);
    while (std::chrono::steady_clock::now () < deadline) {
        try {
            message_ = spot_.subscribe (zlink::recv_flags_t::dontwait);
            return true;
        }
        catch (const zlink::recv_error_t &err) {
            if (err.result () != zlink::recv_result_t::no_data
                && err.result () != zlink::recv_result_t::busy)
                return false;
        }
        sleep_ms (5);
    }
    return false;
}

static void test_spot_peer_pubsub ()
{
    const std::vector<transport_case_t> cases = transport_cases ();
    for (size_t i = 0; i < cases.size (); ++i) {
        const transport_case_t &tc = cases[i];
        if (!transport_supported (tc))
            continue;

        zlink::context_t ctx;
        zlink::service::spot_node_t node_a (ctx);
        zlink::service::spot_node_t node_b (ctx);

        std::string endpoint_a = endpoint_for (tc, "spot-peer-a");
        std::string endpoint_b = endpoint_for (tc, "spot-peer-b");
        try {
            node_a.bind (endpoint_a.c_str ());
        } catch (const zlink::zlink_error_t &) {
            continue;
        }
        try {
            node_b.bind (endpoint_b.c_str ());
        } catch (const zlink::zlink_error_t &) {
            continue;
        }

        const std::string channel_name_a = "spot-peer";
        zlink::service::discovery_t discovery_a (
          ctx, zlink::auto_connect_type::spot_mesh, channel_name_a);
        assert (discovery_a.valid ());
        node_a.attach_discovery (discovery_a);

        node_b.connect_peer (endpoint_a.c_str ());

        zlink::service::spot_t spot_a (node_a);
        zlink::service::spot_t spot_b (node_b);
        assert (spot_a.valid ());
        assert (spot_b.valid ());

        spot_b.set_subscription ("peer:topic");
        sleep_ms (150);

        std::vector<zlink::message_t> parts;
        parts.push_back (make_msg ("pong", 4));
        spot_a.publish ("peer:topic")
          .message (parts[0])
          .submit ();

        zlink::topic_message_t received;
        assert (recv_spot_with_timeout (spot_b, received, 4000));
        assert (received.topic () == "peer:topic");
        assert (received.parts ().size () == 1);
        assert (received.parts ()[0].size () == 4);
        assert (std::memcmp (received.parts ()[0].data (), "pong", 4) == 0);
    }
}

static void test_spot_multipart_peer_pubsub ()
{
    zlink::context_t ctx;
    zlink::service::spot_node_t node_a (ctx);
   zlink::service::spot_node_t node_b (ctx);

    const std::string endpoint = unique_inproc ("inproc://cpp-", "spot-multipart");
    node_a.bind (endpoint.c_str ());
    node_b.connect_peer (endpoint.c_str ());

    const std::string channel_name_a = "spot-multipart";
    zlink::service::discovery_t discovery_a (
      ctx, zlink::auto_connect_type::spot_mesh, channel_name_a);
    assert (discovery_a.valid ());
    node_a.attach_discovery (discovery_a);

    zlink::service::spot_t spot_a (node_a);
    zlink::service::spot_t spot_b (node_b);
    assert (spot_a.valid ());
    assert (spot_b.valid ());

    spot_b.set_subscription ("mp:topic");
    sleep_ms (100);

    std::vector<zlink::message_t> parts;
    parts.push_back (make_msg ("one", 3));
    parts.push_back (make_msg ("two", 3));
    spot_a.publish ("mp:topic")
      .message (parts[0])
      .message (parts[1])
      .submit ();

    zlink::topic_message_t received;
    assert (recv_spot_with_timeout (spot_b, received, 2000));
    assert (received.topic () == "mp:topic");
    assert (received.parts ().size () == 2);
    assert (std::memcmp (received.parts ()[0].data (), "one", 3) == 0);
    assert (std::memcmp (received.parts ()[1].data (), "two", 3) == 0);
}

int main ()
{
    test_spot_peer_pubsub ();
    test_spot_multipart_peer_pubsub ();
    return 0;
}
