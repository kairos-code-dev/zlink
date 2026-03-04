#include "test_helpers.hpp"

#include <cerrno>
#include <cstring>

namespace {

const char *kConnRoutingId = "conn1";
const char *kRoutingIdX = "X";
const char *kRoutingIdY = "Y";
const char *kRoutingIdZ = "Z";

void set_zero_linger (zlink::socket_t &sock)
{
    const int zero = 0;
    assert (sock.set (zlink::socket_option::linger, zero) == 0);
}

void test_router_to_router (bool named)
{
    zlink::context_t ctx;

    zlink::socket_t bind_router (ctx, zlink::socket_type::router);
    zlink::socket_t conn_router (ctx, zlink::socket_type::router);
    set_zero_linger (bind_router);
    set_zero_linger (conn_router);

    const std::string endpoint =
      unique_inproc ("inproc://cpp-connect-rid-", named ? "named" : "unnamed");
    assert (bind_router.bind (endpoint) == 0);

    if (named) {
        assert (bind_router.set (zlink::socket_option::routing_id, kRoutingIdX, 1)
                == 0);
        assert (conn_router.set (zlink::socket_option::routing_id, kRoutingIdY, 1)
                == 0);
    }

    assert (conn_router.set (zlink::socket_option::connect_routing_id,
                             kConnRoutingId,
                             std::strlen (kConnRoutingId))
            == 0);
    assert (conn_router.connect (endpoint) == 0);

    assert (conn_router.send (
              kConnRoutingId, std::strlen (kConnRoutingId), zlink::send_flag::sndmore)
            == static_cast<int> (std::strlen (kConnRoutingId)));
    assert (conn_router.send ("hi 1", 4) == 4);

    zlink::message_t route_from_conn;
    zlink::message_t payload;
    assert (recv_msg_with_timeout (bind_router, route_from_conn, 2000) >= 0);
    assert (recv_msg_with_timeout (bind_router, payload, 2000) >= 0);

    if (named) {
        assert (route_from_conn.size () == 1);
        assert (std::memcmp (route_from_conn.data (), kRoutingIdY, 1) == 0);
    } else {
        assert (route_from_conn.size () == 16);
    }
    assert (payload.size () == 4);
    assert (std::memcmp (payload.data (), "hi 1", 4) == 0);

    assert (bind_router.send (route_from_conn, zlink::send_flag::sndmore) >= 0);
    assert (bind_router.send ("ok", 2) == 2);

    char recv_buf[32];
    std::memset (recv_buf, 0, sizeof (recv_buf));
    const int rid_rc = recv_with_timeout (conn_router, recv_buf, sizeof (recv_buf), 2000);
    assert (rid_rc == static_cast<int> (std::strlen (kConnRoutingId)));
    assert (std::memcmp (recv_buf, kConnRoutingId, std::strlen (kConnRoutingId)) == 0);

    std::memset (recv_buf, 0, sizeof (recv_buf));
    assert (recv_with_timeout (conn_router, recv_buf, sizeof (recv_buf), 2000) == 2);
    assert (std::memcmp (recv_buf, "ok", 2) == 0);
}

void test_router_to_router_while_receiving ()
{
    zlink::context_t ctx;

    zlink::socket_t xbind (ctx, zlink::socket_type::router);
    zlink::socket_t zbind (ctx, zlink::socket_type::router);
    zlink::socket_t yconn (ctx, zlink::socket_type::router);
    set_zero_linger (xbind);
    set_zero_linger (zbind);
    set_zero_linger (yconn);

    const std::string x_endpoint = unique_inproc ("inproc://cpp-rid-x-", "bind");
    const std::string z_endpoint = unique_inproc ("inproc://cpp-rid-z-", "bind");
    assert (xbind.bind (x_endpoint) == 0);
    assert (zbind.bind (z_endpoint) == 0);

    assert (xbind.set (zlink::socket_option::routing_id, kRoutingIdX, 1) == 0);
    assert (yconn.set (zlink::socket_option::routing_id, kRoutingIdY, 1) == 0);
    assert (zbind.set (zlink::socket_option::routing_id, kRoutingIdZ, 1) == 0);

    assert (yconn.set (zlink::socket_option::connect_routing_id, kRoutingIdX, 1)
            == 0);
    assert (yconn.connect (x_endpoint) == 0);

    assert (yconn.send (kRoutingIdX, 1, zlink::send_flag::sndmore) == 1);
    assert (yconn.send ("hi 1", 4) == 4);

    sleep_ms (50);

    assert (xbind.set (zlink::socket_option::connect_routing_id, kRoutingIdZ, 1)
            == 0);
    assert (xbind.connect (z_endpoint) == 0);

    assert (xbind.send (kRoutingIdZ, 1, zlink::send_flag::sndmore) == 1);
    assert (xbind.send ("hi 1", 4) == 4);

    sleep_ms (50);

    char y_buf[32];
    assert (yconn.recv (y_buf, sizeof (y_buf), zlink::recv_flag::dontwait) == -1);
    assert (zlink_errno () == EAGAIN);

    zlink::message_t route;
    zlink::message_t payload;
    assert (recv_msg_with_timeout (zbind, route, 2000) >= 0);
    assert (recv_msg_with_timeout (zbind, payload, 2000) >= 0);
    assert (route.size () == 1);
    assert (std::memcmp (route.data (), kRoutingIdX, 1) == 0);
    assert (payload.size () == 4);
    assert (std::memcmp (payload.data (), "hi 1", 4) == 0);
}

} // namespace

int main ()
{
    test_router_to_router (false);
    test_router_to_router (true);
    test_router_to_router_while_receiving ();
    return 0;
}
