#include "test_helpers.hpp"

#include <cassert>
#include <cstdint>
#include <cstring>

namespace {

void test_typed_socket_options ()
{
    zlink::context_t ctx;

    zlink::dealer_socket_t dealer (ctx);
    assert (dealer.set (zlink::socket_options::linger, 0) == 0);
    assert (dealer.set (zlink::socket_options::sndhwm, 7) == 0);

    int sndhwm = 0;
    assert (dealer.get (zlink::socket_options::sndhwm, sndhwm) == 0);
    assert (sndhwm == 7);

    assert (dealer.set (zlink::socket_options::routing_id, std::string ("typed-rid"))
            == 0);

    uint64_t affinity = 0;
    assert (dealer.get (zlink::socket_options::affinity, affinity) == 0);

    assert (dealer.set (zlink::socket_options::zmp_metadata, 1) == 0);
    int metadata = 0;
    assert (dealer.get (zlink::socket_options::zmp_metadata, metadata) == 0);
    assert (metadata == 1);

    zlink_fd_t fd = 0;
    assert (dealer.get (zlink::socket_options::fd, fd) == 0);
}

void test_typed_stream_notify ()
{
    zlink::context_t ctx;
    zlink::stream_socket_t stream (ctx);

    assert (stream.set (zlink::socket_options::stream_notify, 1) == 0);
    int stream_notify = 0;
    assert (stream.get (zlink::socket_options::stream_notify, stream_notify) == 0);
    assert (stream_notify == 1);
}

void test_typed_binary_string_get ()
{
    zlink::context_t ctx;
    zlink::dealer_socket_t dealer (ctx);

    const std::string routing_id ("RID\0", 4);
    assert (dealer.set (zlink::socket_options::routing_id, routing_id) == 0);

    std::string got;
    assert (dealer.get (zlink::socket_options::routing_id, got) == 0);
    assert (got.size () == routing_id.size ());
    assert (std::memcmp (got.data (), routing_id.data (), routing_id.size ()) == 0);
}

void test_typed_string_get ()
{
    zlink::context_t ctx;
    zlink::pair_socket_t pair (ctx);

    assert (pair.bind ("inproc://typed-socket-options") == 0);

    std::string endpoint;
    assert (pair.get (zlink::socket_options::last_endpoint, endpoint) == 0);
    assert (!endpoint.empty ());
}

} // namespace

int main ()
{
    test_typed_socket_options ();
    test_typed_stream_notify ();
    test_typed_binary_string_get ();
    test_typed_string_get ();
    return 0;
}
