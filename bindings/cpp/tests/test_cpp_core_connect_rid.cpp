#include "test_helpers.hpp"

#include <cerrno>
#include <cstring>

namespace {

const char *kConnRoutingId = "conn1";
const char *kRoutingIdX = "X";
const char *kRoutingIdY = "Y";
const char *kRoutingIdZ = "Z";

template<typename SocketLike>
void set_zero_linger (SocketLike &sock)
{
    const int zero = 0;
    assert (sock.set_option (zlink::socket_option::linger, zero) == 0);
}

void test_router_to_router (bool named)
{
    zlink::context_t ctx;

    zlink::router_socket_t bind_router (ctx);
    zlink::router_socket_t conn_router (ctx);
    set_zero_linger (bind_router);
    set_zero_linger (conn_router);

    const std::string endpoint =
      unique_inproc ("inproc://cpp-connect-rid-", named ? "named" : "unnamed");
    bind_router.bind (endpoint);

    if (named) {
        assert (bind_router.set_option (zlink::socket_option::routing_id, kRoutingIdX, 1)
                == 0);
        assert (conn_router.set_option (zlink::socket_option::routing_id, kRoutingIdY, 1)
                == 0);
    }

    assert (conn_router.set_option (zlink::socket_option::connect_routing_id,
                             kConnRoutingId,
                             std::strlen (kConnRoutingId))
            == 0);
    conn_router.connect (endpoint);

    assert (raw_send (conn_router, kConnRoutingId, std::strlen (kConnRoutingId),
                      zlink::send_flag::sndmore)
            == static_cast<int> (std::strlen (kConnRoutingId)));
    assert (raw_send (conn_router, "hi 1", 4) == 4);

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
    assert (raw_send (bind_router, "ok", 2) == 2);

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

    zlink::router_socket_t xbind (ctx);
    zlink::router_socket_t zbind (ctx);
    zlink::router_socket_t yconn (ctx);
    set_zero_linger (xbind);
    set_zero_linger (zbind);
    set_zero_linger (yconn);

    const std::string x_endpoint = unique_inproc ("inproc://cpp-rid-x-", "bind");
    const std::string z_endpoint = unique_inproc ("inproc://cpp-rid-z-", "bind");
    xbind.bind (x_endpoint);
    zbind.bind (z_endpoint);

    assert (xbind.set_option (zlink::socket_option::routing_id, kRoutingIdX, 1) == 0);
    assert (yconn.set_option (zlink::socket_option::routing_id, kRoutingIdY, 1) == 0);
    assert (zbind.set_option (zlink::socket_option::routing_id, kRoutingIdZ, 1) == 0);

    assert (yconn.set_option (zlink::socket_option::connect_routing_id, kRoutingIdX, 1)
            == 0);
    yconn.connect (x_endpoint);

    assert (raw_send (yconn, kRoutingIdX, 1, zlink::send_flag::sndmore) == 1);
    assert (raw_send (yconn, "hi 1", 4) == 4);

    sleep_ms (50);

    assert (xbind.set_option (zlink::socket_option::connect_routing_id, kRoutingIdZ, 1)
            == 0);
    xbind.connect (z_endpoint);

    assert (raw_send (xbind, kRoutingIdZ, 1, zlink::send_flag::sndmore) == 1);
    assert (raw_send (xbind, "hi 1", 4) == 4);

    sleep_ms (50);

    char y_buf[32];
    assert (raw_recv (yconn, y_buf, sizeof (y_buf), zlink::recv_flag::dontwait) == -1);
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
