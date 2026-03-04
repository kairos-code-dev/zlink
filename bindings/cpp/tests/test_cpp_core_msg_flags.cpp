#include "test_helpers.hpp"

namespace {

void test_more ()
{
    zlink::context_t ctx;
    zlink::socket_t server (ctx, zlink::socket_type::router);
    zlink::socket_t client (ctx, zlink::socket_type::dealer);

    const std::string endpoint = unique_inproc ("inproc://cpp-msg-flags-", "more");
    assert (server.bind (endpoint) == 0);
    assert (client.connect (endpoint) == 0);

    send_string_expect_success (client, "A", zlink::send_flag::sndmore);
    send_string_expect_success (client, "B");

    zlink::message_t msg;
    assert (recv_msg_with_timeout (server, msg, 2000) >= 0);
    assert (msg.more ());

    assert (recv_msg_with_timeout (server, msg, 2000) == 1);
    assert (msg.more ());

    assert (recv_msg_with_timeout (server, msg, 2000) == 1);
    assert (!msg.more ());
}

void test_shared_refcounted ()
{
    zlink_msg_t msg_a;
    assert (zlink_msg_init_size (&msg_a, 1024) == 0);
    assert (zlink_msg_get (&msg_a, ZLINK_SHARED) == 0);

    zlink_msg_t msg_b;
    assert (zlink_msg_init (&msg_b) == 0);
    assert (zlink_msg_copy (&msg_b, &msg_a) == 0);
    assert (zlink_msg_get (&msg_b, ZLINK_SHARED) == 1);

    assert (zlink_msg_close (&msg_a) == 0);
    assert (zlink_msg_close (&msg_b) == 0);
}

void test_shared_const ()
{
    zlink_msg_t msg;
    assert (zlink_msg_init_data (&msg, (void *) "TEST", 5, 0, 0) == 0);
    assert (zlink_msg_get (&msg, ZLINK_SHARED) == 1);
    assert (zlink_msg_close (&msg) == 0);
}

} // namespace

int main ()
{
    test_more ();
    test_shared_refcounted ();
    test_shared_const ();
    return 0;
}
