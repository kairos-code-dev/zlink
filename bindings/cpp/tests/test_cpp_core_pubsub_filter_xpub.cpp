#include "test_helpers.hpp"

#include <cstring>

namespace {

void test_pubsub_filter_transport (const std::string &bind_endpoint_)
{
    zlink::context_t ctx;
    zlink::socket_t pub (ctx, zlink::socket_type::pub);
    zlink::socket_t sub (ctx, zlink::socket_type::sub);

    assert (pub.bind (bind_endpoint_) == 0);

    std::string connect_endpoint = bind_endpoint_;
    if (bind_endpoint_.find ("tcp://") == 0 || bind_endpoint_.find ("ipc://") == 0)
        connect_endpoint = bound_endpoint (pub);

    assert (sub.connect (connect_endpoint) == 0);
    assert (sub.set (zlink::socket_option::subscribe, "topicA", 6) == 0);
    sleep_ms (300);

    send_string_expect_success (pub, "topicA hello");
    send_string_expect_success (pub, "topicB world");
    send_string_expect_success (pub, "topicA test");

    recv_string_expect_success (sub, "topicA hello");
    recv_string_expect_success (sub, "topicA test");
}

void test_pubsub_xpub_xsub_transport (const std::string &bind_endpoint_)
{
    zlink::context_t ctx;
    zlink::socket_t xpub (ctx, zlink::socket_type::xpub);
    zlink::socket_t xsub (ctx, zlink::socket_type::xsub);

    assert (xpub.bind (bind_endpoint_) == 0);
    std::string endpoint = bind_endpoint_;
    if (bind_endpoint_.find ("tcp://") == 0 || bind_endpoint_.find ("ipc://") == 0)
        endpoint = bound_endpoint (xpub);

    assert (xsub.connect (endpoint) == 0);

    const char sub_msg[] = {0x01, 0};
    assert (xsub.send (sub_msg, 1) == 1);

    char sub_recv[16];
    std::memset (sub_recv, 0, sizeof (sub_recv));
    assert (xpub.recv (sub_recv, sizeof (sub_recv)) >= 0);

    sleep_ms (300);

    const char *msg = "xpub_xsub_test";
    send_string_expect_success (xpub, msg);
    recv_string_expect_success (xsub, msg);
}

} // namespace

int main ()
{
    test_pubsub_filter_transport ("tcp://127.0.0.1:*");
#if defined(ZLINK_HAVE_IPC)
    test_pubsub_filter_transport ("ipc://*");
#endif
    test_pubsub_filter_transport ("inproc://test_pubsub_filter");

    test_pubsub_xpub_xsub_transport ("tcp://127.0.0.1:*");
#if defined(ZLINK_HAVE_IPC)
    test_pubsub_xpub_xsub_transport ("ipc://*");
#endif
    test_pubsub_xpub_xsub_transport ("inproc://test_xpub_xsub");
    return 0;
}
