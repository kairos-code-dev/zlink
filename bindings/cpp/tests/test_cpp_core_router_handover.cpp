#include "test_helpers.hpp"

#include <cerrno>
#include <cstring>

namespace {

template<typename SocketLike>
void expect_recv_eagain (SocketLike &socket_, int timeout_ms_)
{
    assert (socket_.set_option (zlink::socket_option::rcvtimeo, timeout_ms_) == 0);
    char buffer[255];
    std::memset (buffer, 0, sizeof (buffer));
    assert (raw_recv (socket_, buffer, sizeof (buffer), zlink::recv_flag::none) == -1);
    assert (zlink_errno () == EAGAIN);
}

void test_with_handover ()
{
    zlink::context_t ctx;
    zlink::router_socket_t router (ctx);
    const std::string endpoint =
      endpoint_for (transport_case_t{"tcp", ""}, "router-handover-on");
    assert (router.bind (endpoint) == 0);

    const int handover = 1;
    assert (router.set_option (zlink::socket_option::router_handover, handover) == 0);

    zlink::dealer_socket_t dealer_one (ctx);
    assert (dealer_one.set_option (zlink::socket_option::routing_id, "X", 1) == 0);
    assert (dealer_one.connect (endpoint) == 0);
    send_string_expect_success (dealer_one, "Hello");
    recv_string_expect_success (router, "X");
    recv_string_expect_success (router, "Hello");

    zlink::dealer_socket_t dealer_two (ctx);
    assert (dealer_two.set_option (zlink::socket_option::routing_id, "X", 1) == 0);
    assert (dealer_two.connect (endpoint) == 0);
    send_string_expect_success (dealer_two, "Hello");
    recv_string_expect_success (router, "X");
    recv_string_expect_success (router, "Hello");

    send_string_expect_success (router, "X", zlink::send_flag::sndmore);
    send_string_expect_success (router, "Hello");

    expect_recv_eagain (dealer_one, 300);
    recv_string_expect_success (dealer_two, "Hello");
}

void test_without_handover ()
{
    zlink::context_t ctx;
    zlink::router_socket_t router (ctx);
    const std::string endpoint =
      endpoint_for (transport_case_t{"tcp", ""}, "router-handover-off");
    assert (router.bind (endpoint) == 0);

    zlink::dealer_socket_t dealer_one (ctx);
    assert (dealer_one.set_option (zlink::socket_option::routing_id, "X", 1) == 0);
    assert (dealer_one.connect (endpoint) == 0);
    send_string_expect_success (dealer_one, "Hello");
    recv_string_expect_success (router, "X");
    recv_string_expect_success (router, "Hello");

    zlink::dealer_socket_t dealer_two (ctx);
    assert (dealer_two.set_option (zlink::socket_option::routing_id, "X", 1) == 0);
    assert (dealer_two.connect (endpoint) == 0);
    send_string_expect_success (dealer_two, "Hello");

    expect_recv_eagain (router, 300);

    send_string_expect_success (router, "X", zlink::send_flag::sndmore);
    send_string_expect_success (router, "Hello");

    expect_recv_eagain (dealer_two, 300);
    recv_string_expect_success (dealer_one, "Hello");
}

} // namespace

int main ()
{
    test_with_handover ();
    test_without_handover ();
    return 0;
}
