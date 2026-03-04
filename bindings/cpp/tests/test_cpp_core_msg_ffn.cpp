#include "test_helpers.hpp"

#include <cstring>

namespace {

void ffn (void *data_, void *hint_)
{
    (void) data_;
    std::memcpy (hint_, "freed", 5);
}

void test_msg_init_ffn ()
{
    zlink::context_t ctx;
    zlink::socket_t router (ctx, zlink::socket_type::router);
    zlink::socket_t dealer (ctx, zlink::socket_type::dealer);

    const std::string endpoint =
      endpoint_for (transport_case_t{"tcp", ""}, "msg-ffn");
    assert (router.bind (endpoint) == 0);
    assert (dealer.connect (endpoint) == 0);

    zlink_msg_t msg;
    zlink_msg_t msg2;
    char hint[5];
    char data[255];
    std::memset (data, 0, sizeof (data));
    std::memcpy (data, "data", 4);

    std::memcpy (hint, "hint", 4);
    assert (zlink_msg_init_data (&msg, data, sizeof (data), ffn, hint) == 0);
    assert (zlink_msg_close (&msg) == 0);
    sleep_ms (300);
    assert (std::memcmp (hint, "freed", 5) == 0);

    std::memcpy (hint, "hint", 4);
    assert (zlink_msg_init (&msg2) == 0);
    assert (zlink_msg_init_data (&msg, data, sizeof (data), ffn, hint) == 0);
    assert (zlink_msg_copy (&msg2, &msg) == 0);
    assert (zlink_msg_close (&msg2) == 0);
    assert (zlink_msg_close (&msg) == 0);
    sleep_ms (300);
    assert (std::memcmp (hint, "freed", 5) == 0);

    std::memcpy (hint, "hint", 4);
    assert (zlink_msg_init_data (&msg, data, sizeof (data), ffn, hint) == 0);
    assert (zlink_msg_send (&msg, dealer.handle (), 0) >= 0);

    char buf[255];
    std::memset (buf, 0, sizeof (buf));
    assert (recv_with_timeout (router, buf, sizeof (buf), 2000) >= 0);
    assert (recv_with_timeout (router, buf, sizeof (buf), 2000) == 255);
    assert (std::memcmp (data, buf, 4) == 0);
    sleep_ms (300);
    assert (std::memcmp (hint, "freed", 5) == 0);
    assert (zlink_msg_close (&msg) == 0);

    std::memcpy (hint, "hint", 4);
    assert (zlink_msg_init (&msg2) == 0);
    assert (zlink_msg_init_data (&msg, data, sizeof (data), ffn, hint) == 0);
    assert (zlink_msg_copy (&msg2, &msg) == 0);
    assert (zlink_msg_send (&msg, dealer.handle (), 0) >= 0);
    assert (recv_with_timeout (router, buf, sizeof (buf), 2000) >= 0);
    assert (recv_with_timeout (router, buf, sizeof (buf), 2000) == 255);
    assert (std::memcmp (data, buf, 4) == 0);
    assert (zlink_msg_close (&msg2) == 0);
    assert (zlink_msg_close (&msg) == 0);
    sleep_ms (300);
    assert (std::memcmp (hint, "freed", 5) == 0);
}

} // namespace

int main ()
{
    test_msg_init_ffn ();
    return 0;
}
