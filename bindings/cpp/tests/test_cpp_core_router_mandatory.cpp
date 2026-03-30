#include "test_helpers.hpp"

#include <cerrno>
#include <cstring>

int main ()
{
    zlink::context_t ctx;

    zlink::router_socket_t router (ctx);
    const std::string endpoint = endpoint_for (transport_case_t{"tcp", ""}, "router-mandatory");
    assert (router.bind (endpoint) == 0);

    assert (router.send ("UNKNOWN", 7, zlink::send_flag::sndmore) == 7);
    assert (router.send ("DATA", 4) == 4);

    const int mandatory = 1;
    assert (router.set_option (zlink::socket_option::router_mandatory, mandatory) == 0);
    assert (router.send ("UNKNOWN", 7, zlink::send_flag::sndmore) == -1);
    assert (zlink_errno () == EHOSTUNREACH);

    zlink::dealer_socket_t dealer (ctx);
    assert (dealer.set_option (zlink::socket_option::routing_id, "X", 1) == 0);
    assert (dealer.connect (endpoint) == 0);

    assert (dealer.send ("Hello", 5) == 5);

    zlink::message_t rid;
    zlink::message_t payload;
    assert (recv_msg_with_timeout (router, rid, 2000) >= 0);
    assert (recv_msg_with_timeout (router, payload, 2000) >= 0);
    assert (rid.size () == 1);
    assert (std::memcmp (rid.data (), "X", 1) == 0);
    assert (payload.size () == 5);
    assert (std::memcmp (payload.data (), "Hello", 5) == 0);

    assert (router.send ("X", 1, zlink::send_flag::sndmore) == 1);
    assert (router.send ("Hello", 5) == 5);

    char recv_buf[16];
    std::memset (recv_buf, 0, sizeof (recv_buf));
    assert (recv_with_timeout (dealer, recv_buf, sizeof (recv_buf), 2000) == 5);
    assert (std::memcmp (recv_buf, "Hello", 5) == 0);

    return 0;
}
