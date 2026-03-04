#include "test_helpers.hpp"

#include <vector>

namespace {

void test_fair_queue_in (const std::string &endpoint_)
{
    zlink::context_t ctx;
    zlink::socket_t receiver (ctx, zlink::socket_type::router);
    assert (receiver.bind (endpoint_) == 0);

    std::string connect_endpoint = endpoint_;
    if (endpoint_.find ("*") != std::string::npos)
        connect_endpoint = bound_endpoint (receiver);

    const unsigned char services = 5;
    std::vector<zlink::socket_t> senders;
    senders.reserve (services);

    for (unsigned char peer = 0; peer < services; ++peer) {
        senders.push_back (zlink::socket_t (ctx, zlink::socket_type::dealer));
        char rid[2] = {static_cast<char> ('A' + peer), '\0'};
        assert (senders.back ().set (zlink::socket_option::routing_id, rid, 2) == 0);
        assert (senders.back ().connect (connect_endpoint) == 0);
    }

    sleep_ms (300);

    // Core parity: send/recv one warm-up request twice.
    assert (senders[0].send ("M", 2) == 2);
    {
        zlink::message_t id;
        zlink::message_t body;
        assert (receiver.recv (id) == 2);
        assert (receiver.recv (body) == 2);
        assert (id.size () == 2);
        assert (body.size () == 2);
        assert (static_cast<const char *> (id.data ())[0] == 'A');
        assert (std::memcmp (body.data (), "M", 1) == 0);
    }

    assert (senders[0].send ("M", 2) == 2);
    {
        zlink::message_t id;
        zlink::message_t body;
        assert (receiver.recv (id) == 2);
        assert (receiver.recv (body) == 2);
        assert (id.size () == 2);
        assert (body.size () == 2);
        assert (static_cast<const char *> (id.data ())[0] == 'A');
        assert (std::memcmp (body.data (), "M", 1) == 0);
    }

    int sum = 0;
    for (unsigned char peer = 0; peer < services; ++peer) {
        assert (senders[peer].send ("M", 2) == 2);
        sum += 'A' + peer;
    }

    for (unsigned char peer = 0; peer < services; ++peer) {
        zlink::message_t id;
        zlink::message_t body;
        assert (receiver.recv (id) == 2);
        assert (receiver.recv (body) == 2);
        const char first = static_cast<const char *> (id.data ())[0];
        sum -= first;
        assert (body.size () == 2);
        assert (std::memcmp (body.data (), "M", 1) == 0);
    }

    assert (sum == 0);
}

} // namespace

int main ()
{
    test_fair_queue_in ("tcp://127.0.0.1:*");
    test_fair_queue_in (unique_inproc ("inproc://cpp-spec-router-", "fq"));
    return 0;
}
