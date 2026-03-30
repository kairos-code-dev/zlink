#include "test_helpers.hpp"

#include <cstring>

namespace {

void run_router_multiple_dealers (const std::string &endpoint_)
{
    zlink::context_t ctx;
    zlink::router_socket_t router (ctx);
    zlink::dealer_socket_t dealer1 (ctx);
    zlink::dealer_socket_t dealer2 (ctx);

    assert (dealer1.set_option (zlink::socket_option::routing_id, "D1", 2) == 0);
    assert (dealer2.set_option (zlink::socket_option::routing_id, "D2", 2) == 0);

    assert (router.bind (endpoint_) == 0);
    std::string bound = endpoint_;
    if (endpoint_.find ("*") != std::string::npos)
        bound = bound_endpoint (router);

    assert (dealer1.connect (bound) == 0);
    assert (dealer2.connect (bound) == 0);

    sleep_ms (300);

    send_string_expect_success (dealer1, "from_dealer1");
    send_string_expect_success (dealer2, "from_dealer2");

    char identity[32];
    char msg[64];

    int id_size = recv_with_timeout (router, identity, sizeof (identity), 2000);
    assert (id_size == 2);
    int msg_size = recv_with_timeout (router, msg, sizeof (msg), 2000);
    assert (msg_size > 0);

    id_size = recv_with_timeout (router, identity, sizeof (identity), 2000);
    assert (id_size == 2);
    msg_size = recv_with_timeout (router, msg, sizeof (msg), 2000);
    assert (msg_size > 0);

    assert (router.send ("D1", 2, zlink::send_flag::sndmore) == 2);
    send_string_expect_success (router, "reply_to_d1");

    assert (router.send ("D2", 2, zlink::send_flag::sndmore) == 2);
    send_string_expect_success (router, "reply_to_d2");

    recv_string_expect_success (dealer1, "reply_to_d1");
    recv_string_expect_success (dealer2, "reply_to_d2");
}

} // namespace

int main ()
{
    run_router_multiple_dealers ("tcp://127.0.0.1:*");

#if defined(ZLINK_HAVE_IPC)
    run_router_multiple_dealers ("ipc://*");
#endif

    run_router_multiple_dealers (
      unique_inproc ("inproc://cpp-router-multi-", "dealers"));

    return 0;
}
