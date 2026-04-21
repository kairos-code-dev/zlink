#include "test_helpers.hpp"

#include <cstring>
#include <vector>

namespace {

bool transport_available (const std::string &name_)
{
    if (name_ == "tcp" || name_ == "inproc")
        return true;
#if defined(ZLINK_HAVE_IPC)
    if (name_ == "ipc")
        return true;
#endif
    if (name_ == "ws")
        return zlink::has ("ws");
    return false;
}

template<typename SocketLike>
std::string bind_endpoint (SocketLike &socket_,
                           const std::string &transport_,
                           const std::string &tag_)
{
    if (transport_ == "inproc") {
        const std::string endpoint = unique_inproc ("inproc://cpp-matrix-", tag_);
        socket_.bind (endpoint);
        return endpoint;
    }

#if defined(ZLINK_HAVE_IPC)
    if (transport_ == "ipc") {
        socket_.bind ("ipc://*");
        return bound_endpoint (socket_);
    }
#endif

    if (transport_ == "tcp") {
        (void) tag_;
        socket_.bind ("tcp://127.0.0.1:*");
        return bound_endpoint (socket_);
    }

    if (transport_ == "ws") {
        (void) tag_;
        socket_.bind ("ws://127.0.0.1:*");
        return bound_endpoint (socket_);
    }

    assert (false);
    return std::string ();
}

void run_pair (const std::string &transport_)
{
    zlink::context_t ctx;
    zlink::pair_socket_t server (ctx);
    zlink::pair_socket_t client (ctx);

    const std::string endpoint = bind_endpoint (server, transport_, "pair");
    client.connect (endpoint);
    sleep_ms (300);

    send_string_expect_success (client, "pair-hello");
    recv_string_expect_success (server, "pair-hello");
    send_string_expect_success (server, "pair-ack");
    recv_string_expect_success (client, "pair-ack");
}

void run_pubsub (const std::string &transport_)
{
    zlink::context_t ctx;
    zlink::pub_socket_t pub (ctx);
    zlink::sub_socket_t sub (ctx);

    const std::string endpoint = bind_endpoint (pub, transport_, "pubsub");
    sub.connect (endpoint);
    assert (sub.set_option (zlink::socket_option::subscribe, "", 0) == 0);
    sleep_ms (300);

    send_string_expect_success (pub, "pubsub-hello");
    recv_string_expect_success (sub, "pubsub-hello");
}

void run_router_dealer (const std::string &transport_)
{
    zlink::context_t ctx;
    zlink::router_socket_t router (ctx);
    zlink::dealer_socket_t dealer (ctx);

    assert (dealer.set_option (zlink::socket_option::routing_id, "DEALER1", 7) == 0);

    const std::string endpoint = bind_endpoint (router, transport_, "rd");
    dealer.connect (endpoint);
    sleep_ms (300);

    send_string_expect_success (dealer, "dealer-msg");

    char identity[32];
    const int id_size = recv_with_timeout (router, identity, sizeof (identity), 2000);
    assert (id_size == 7);
    assert (std::memcmp (identity, "DEALER1", 7) == 0);
    recv_string_expect_success (router, "dealer-msg");

    assert (raw_send (router, "DEALER1", 7, zlink::send_flag::sndmore) == 7);
    send_string_expect_success (router, "router-reply");
    recv_string_expect_success (dealer, "router-reply");
}

void run_router_router (const std::string &transport_)
{
    zlink::context_t ctx;
    zlink::router_socket_t server (ctx);
    zlink::router_socket_t client (ctx);

    assert (server.set_option (zlink::socket_option::routing_id, "SERVER", 6) == 0);
    assert (client.set_option (zlink::socket_option::routing_id, "CLIENT", 6) == 0);

    const std::string endpoint = bind_endpoint (server, transport_, "rr");
    client.connect (endpoint);
    sleep_ms (300);

    assert (raw_send (client, "SERVER", 6, zlink::send_flag::sndmore) == 6);
    send_string_expect_success (client, "router-msg");

    char identity[32];
    int id_size = recv_with_timeout (server, identity, sizeof (identity), 2000);
    assert (id_size == 6);
    assert (std::memcmp (identity, "CLIENT", 6) == 0);
    recv_string_expect_success (server, "router-msg");

    assert (raw_send (server, "CLIENT", 6, zlink::send_flag::sndmore) == 6);
    send_string_expect_success (server, "router-reply");

    id_size = recv_with_timeout (client, identity, sizeof (identity), 2000);
    assert (id_size == 6);
    assert (std::memcmp (identity, "SERVER", 6) == 0);
    recv_string_expect_success (client, "router-reply");
}

void run_transport_matrix (const std::string &transport_)
{
    if (!transport_available (transport_))
        return;

    run_pair (transport_);
    run_pubsub (transport_);
    run_router_dealer (transport_);
    run_router_router (transport_);
}

} // namespace

int main ()
{
    run_transport_matrix ("tcp");
    run_transport_matrix ("inproc");
#if defined(ZLINK_HAVE_IPC)
    run_transport_matrix ("ipc");
#endif
    run_transport_matrix ("ws");
    return 0;
}
