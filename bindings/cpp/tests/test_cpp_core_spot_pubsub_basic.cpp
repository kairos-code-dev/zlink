#include "test_helpers.hpp"

#include <cstring>

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

static zlink::message_t make_msg (const char *data_, size_t size_)
{
    zlink::message_t msg (size_);
    std::memcpy (msg.data (), data_, size_);
    return msg;
}

static void test_spot_local_pubsub ()
{
    zlink::context_t ctx;
    zlink::service::spot_node_t node (ctx);
    zlink::service::spot_t spot (node);
    assert (spot.valid ());

    assert (spot.subscribe ("chat:room1:msg") == 0);

    std::vector<zlink::message_t> parts;
    parts.push_back (make_msg ("hello", 5));
    assert (spot.publish ("chat:room1:msg", parts) == 0);

    std::vector<zlink::message_t> recv_parts;
    std::string topic;
    assert (recv_spot_with_timeout (spot, recv_parts, topic, 2000));
    assert (topic == "chat:room1:msg");
    assert (recv_parts.size () == 1);
    assert (recv_parts[0].size () == 5);
    assert (std::memcmp (recv_parts[0].data (), "hello", 5) == 0);
}

static void test_spot_pattern_subscribe ()
{
    zlink::context_t ctx;
    zlink::service::spot_node_t node (ctx);
    zlink::service::spot_t spot (node);
    assert (spot.valid ());

    assert (spot.subscribe_pattern ("zone:12:*") == 0);

    std::vector<zlink::message_t> parts;
    parts.push_back (make_msg ("ping", 4));
    assert (spot.publish ("zone:12:state", parts) == 0);

    std::vector<zlink::message_t> recv_parts;
    std::string topic;
    assert (recv_spot_with_timeout (spot, recv_parts, topic, 2000));
    assert (topic == "zone:12:state");
    assert (recv_parts.size () == 1);
    assert (recv_parts[0].size () == 4);
    assert (std::memcmp (recv_parts[0].data (), "ping", 4) == 0);
}

static void test_spot_publish_no_subscribers ()
{
    zlink::context_t ctx;
    zlink::service::spot_node_t node (ctx);
    zlink::service::spot_t spot (node);
    assert (spot.valid ());

    std::vector<zlink::message_t> parts;
    parts.push_back (make_msg ("nop", 3));
    assert (spot.publish ("metrics:cpu", parts) == 0);
}

static void test_spot_multipart_publish ()
{
    zlink::context_t ctx;
    zlink::service::spot_node_t node (ctx);
    zlink::service::spot_t spot (node);
    assert (spot.valid ());

    assert (spot.subscribe ("mp:topic") == 0);

    std::vector<zlink::message_t> parts;
    parts.push_back (make_msg ("one", 3));
    parts.push_back (make_msg ("two", 3));
    assert (spot.publish ("mp:topic", parts) == 0);

    std::vector<zlink::message_t> recv_parts;
    std::string topic;
    assert (recv_spot_with_timeout (spot, recv_parts, topic, 2000));
    assert (topic == "mp:topic");
    assert (recv_parts.size () == 2);
    assert (std::memcmp (recv_parts[0].data (), "one", 3) == 0);
    assert (std::memcmp (recv_parts[1].data (), "two", 3) == 0);
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

        zlink::service::spot_t spot_b (node_b);
        zlink::service::spot_t spot_a (node_a);
        assert (spot_b.valid ());
        assert (spot_a.valid ());

        assert (spot_b.subscribe ("peer:topic") == 0);
        sleep_ms (150);

        std::vector<zlink::message_t> parts;
        parts.push_back (make_msg ("pong", 4));
        assert (spot_a.publish ("peer:topic", parts) == 0);

        std::vector<zlink::message_t> recv_parts;
        std::string topic;
        assert (recv_spot_with_timeout (spot_b, recv_parts, topic, 4000));
        assert (topic == "peer:topic");
        assert (recv_parts.size () == 1);
        assert (recv_parts[0].size () == 4);
        assert (std::memcmp (recv_parts[0].data (), "pong", 4) == 0);
    }
}

int main ()
{
    test_spot_local_pubsub ();
    test_spot_pattern_subscribe ();
    test_spot_publish_no_subscribers ();
    test_spot_multipart_publish ();
    test_spot_peer_pubsub ();
    return 0;
}
