#include "test_helpers.hpp"

#include <cstdint>

int main ()
{
    zlink::context_t ctx;
    zlink::router_socket_t router (ctx);
    zlink::dealer_socket_t dealer (ctx);

    const int mandatory = 1;
    const int sndhwm = 1;
    const int linger = 1;
    assert (router.set_option (zlink::socket_option::router_mandatory, mandatory) == 0);
    assert (router.set_option (zlink::socket_option::sndhwm, sndhwm) == 0);
    assert (router.set_option (zlink::socket_option::linger, linger) == 0);

    const std::string endpoint =
      endpoint_for (transport_case_t{"tcp", ""}, "router-mandatory-hwm");
    assert (router.bind (endpoint) == 0);

    const int rcvhwm = 1;
    assert (dealer.set_option (zlink::socket_option::routing_id, "X", 1) == 0);
    assert (dealer.set_option (zlink::socket_option::rcvhwm, rcvhwm) == 0);
    assert (dealer.connect (endpoint) == 0);

    send_string_expect_success (dealer, "Hello");
    recv_string_expect_success (router, "X");

    const int buf_size = 65536;
    const std::vector<uint8_t> buf (static_cast<size_t> (buf_size), 0);

    int i = 0;
    for (; i < 100000; ++i) {
        const int rc = router.send ("X", 1, zlink::send_flag::dontwait
                                            | zlink::send_flag::sndmore);
        if (rc == -1 && zlink_errno () == EAGAIN)
            break;
        assert (rc == 1);
        assert (router.send (buf.data (), buf.size (), zlink::send_flag::dontwait)
                == buf_size);
    }
    assert (i < 10);

    sleep_ms (1000);
    for (; i < 100000; ++i) {
        const int rc = router.send ("X", 1, zlink::send_flag::dontwait
                                            | zlink::send_flag::sndmore);
        if (rc == -1 && zlink_errno () == EAGAIN)
            break;
        assert (rc == 1);
        assert (router.send (buf.data (), buf.size (), zlink::send_flag::dontwait)
                == buf_size);
    }
    assert (i < 20);

    return 0;
}
