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
                                    std::vector<zlink::message_t> &parts_,
                                    std::string &topic_,
                                    int timeout_ms_)
{
    const auto deadline =
      std::chrono::steady_clock::now () + std::chrono::milliseconds (timeout_ms_);
    while (std::chrono::steady_clock::now () < deadline) {
        if (spot_.recv (parts_, topic_, zlink::recv_flag::dontwait) == 0)
            return true;
        if (zlink_errno () != EAGAIN)
            return false;
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
        if (node_a.bind (endpoint_a.c_str ()) != 0)
            continue;
        if (node_b.bind (endpoint_b.c_str ()) != 0)
            continue;

        assert (node_b.connect_peer_pub (endpoint_a.c_str ()) == 0);

        zlink::service::spot_t spot_a (node_a);
        zlink::service::spot_t spot_b (node_b);
        assert (spot_a.valid ());
        assert (spot_b.valid ());

        assert (spot_b.set_subscription ("peer:topic") == 0);
        sleep_ms (150);

        std::vector<zlink::message_t> parts;
        parts.push_back (make_msg ("pong", 4));
        spot_a.publish ("peer:topic", parts);

        std::vector<zlink::message_t> recv_parts;
        std::string topic;
        assert (recv_spot_with_timeout (spot_b, recv_parts, topic, 4000));
        assert (topic == "peer:topic");
        assert (recv_parts.size () == 1);
        assert (recv_parts[0].size () == 4);
        assert (std::memcmp (recv_parts[0].data (), "pong", 4) == 0);
    }
}

static void test_spot_multipart_peer_pubsub ()
{
    zlink::context_t ctx;
    zlink::service::spot_node_t node_a (ctx);
    zlink::service::spot_node_t node_b (ctx);

    const std::string endpoint = unique_inproc ("inproc://cpp-", "spot-multipart");
    assert (node_a.bind (endpoint.c_str ()) == 0);
    assert (node_b.connect_peer_pub (endpoint.c_str ()) == 0);

    zlink::service::spot_t spot_a (node_a);
    zlink::service::spot_t spot_b (node_b);
    assert (spot_a.valid ());
    assert (spot_b.valid ());

    assert (spot_b.set_subscription ("mp:topic") == 0);
    sleep_ms (100);

    std::vector<zlink::message_t> parts;
    parts.push_back (make_msg ("one", 3));
    parts.push_back (make_msg ("two", 3));
    spot_a.publish ("mp:topic", parts);

    std::vector<zlink::message_t> recv_parts;
    std::string topic;
    assert (recv_spot_with_timeout (spot_b, recv_parts, topic, 2000));
    assert (topic == "mp:topic");
    assert (recv_parts.size () == 2);
    assert (std::memcmp (recv_parts[0].data (), "one", 3) == 0);
    assert (std::memcmp (recv_parts[1].data (), "two", 3) == 0);
}

int main ()
{
    test_spot_peer_pubsub ();
    test_spot_multipart_peer_pubsub ();
    return 0;
}
