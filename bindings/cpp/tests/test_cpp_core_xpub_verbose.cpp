#include "test_helpers.hpp"

#include <cerrno>
#include <cstring>

namespace {

const uint8_t unsubscribe_a_msg[] = {0, 'A'};
const uint8_t subscribe_a_msg[] = {1, 'A'};
const uint8_t subscribe_b_msg[] = {1, 'B'};

const char test_endpoint[] = "inproc://soname";
const char topic_a[] = "A";
const char topic_b[] = "B";

template<typename SocketLike>
void recv_array_expect_success (SocketLike &socket_,
                                const uint8_t *expected_,
                                size_t size_)
{
    unsigned char buf[32];
    std::memset (buf, 0, sizeof (buf));
    const int rc = recv_with_timeout (socket_, buf, sizeof (buf), 2000);
    const size_t expected_size = size_ > 0 ? size_ : 2;
    assert (rc == static_cast<int> (expected_size));
    assert (std::memcmp (buf, expected_, expected_size) == 0);
}

template<typename SocketLike>
void expect_no_msg (SocketLike &socket_)
{
    char buf[8];
    std::memset (buf, 0, sizeof (buf));
    assert (raw_recv (socket_, buf, sizeof (buf), zlink::recv_flag::dontwait) == -1);
    assert (zlink_errno () == EAGAIN);
}

void test_xpub_verbose_one_sub ()
{
    zlink::context_t ctx;
    zlink::xpub_socket_t pub (ctx);
    zlink::sub_socket_t sub (ctx);
    assert (pub.bind (test_endpoint) == 0);
    assert (sub.connect (test_endpoint) == 0);

    assert (sub.set_option (zlink::socket_option::subscribe, topic_a, 1) == 0);
    recv_array_expect_success (pub, subscribe_a_msg, 2);

    assert (sub.set_option (zlink::socket_option::subscribe, topic_b, 1) == 0);
    recv_array_expect_success (pub, subscribe_b_msg, 2);

    assert (sub.set_option (zlink::socket_option::subscribe, topic_a, 1) == 0);
    expect_no_msg (pub);

    const int verbose = 1;
    assert (pub.set_option (zlink::socket_option::xpub_verbose, verbose) == 0);
    assert (sub.set_option (zlink::socket_option::subscribe, topic_a, 1) == 0);
    recv_array_expect_success (pub, subscribe_a_msg, 2);

    send_string_expect_success (pub, topic_a);
    send_string_expect_success (pub, topic_b);
    recv_string_expect_success (sub, topic_a);
    recv_string_expect_success (sub, topic_b);
}

void create_xpub_with_2_subs (zlink::context_t &ctx_,
                              zlink::xpub_socket_t &pub_,
                              zlink::sub_socket_t &sub0_,
                              zlink::sub_socket_t &sub1_)
{
    assert (pub_.bind (test_endpoint) == 0);
    assert (sub0_.connect (test_endpoint) == 0);
    assert (sub1_.connect (test_endpoint) == 0);
    (void) ctx_;
}

void create_duplicate_subscription (zlink::xpub_socket_t &pub_,
                                    zlink::sub_socket_t &sub0_,
                                    zlink::sub_socket_t &sub1_)
{
    assert (sub0_.set_option (zlink::socket_option::subscribe, topic_a, 1) == 0);
    recv_array_expect_success (pub_, subscribe_a_msg, 2);

    assert (sub1_.set_option (zlink::socket_option::subscribe, topic_a, 1) == 0);
    expect_no_msg (pub_);
}

void test_xpub_verbose_two_subs ()
{
    zlink::context_t ctx;
    zlink::xpub_socket_t pub (ctx);
    zlink::sub_socket_t sub0 (ctx);
    zlink::sub_socket_t sub1 (ctx);
    create_xpub_with_2_subs (ctx, pub, sub0, sub1);
    create_duplicate_subscription (pub, sub0, sub1);

    assert (sub0.set_option (zlink::socket_option::subscribe, topic_b, 1) == 0);
    recv_array_expect_success (pub, subscribe_b_msg, 2);

    const int verbose = 1;
    assert (pub.set_option (zlink::socket_option::xpub_verbose, verbose) == 0);
    assert (sub1.set_option (zlink::socket_option::subscribe, topic_a, 1) == 0);
    recv_array_expect_success (pub, subscribe_a_msg, 2);

    send_string_expect_success (pub, topic_a);
    send_string_expect_success (pub, topic_b);

    recv_string_expect_success (sub0, topic_a);
    recv_string_expect_success (sub1, topic_a);
    recv_string_expect_success (sub0, topic_b);
}

void test_xpub_verboser_one_sub ()
{
    zlink::context_t ctx;
    zlink::xpub_socket_t pub (ctx);
    zlink::sub_socket_t sub (ctx);
    assert (pub.bind (test_endpoint) == 0);
    assert (sub.connect (test_endpoint) == 0);

    assert (sub.set_option (zlink::socket_option::unsubscribe, topic_a, 1) == 0);
    expect_no_msg (pub);

    assert (sub.set_option (zlink::socket_option::subscribe, topic_a, 1) == 0);
    recv_array_expect_success (pub, subscribe_a_msg, 2);

    assert (sub.set_option (zlink::socket_option::subscribe, topic_a, 1) == 0);
    expect_no_msg (pub);

    assert (sub.set_option (zlink::socket_option::unsubscribe, topic_a, 1) == 0);
    assert (sub.set_option (zlink::socket_option::unsubscribe, topic_a, 1) == 0);
    recv_array_expect_success (pub, unsubscribe_a_msg, 2);
    expect_no_msg (pub);

    assert (sub.set_option (zlink::socket_option::unsubscribe, topic_a, 1) == 0);
    expect_no_msg (pub);

    const int verboser = 1;
    assert (pub.set_option (zlink::socket_option::xpub_verboser, verboser) == 0);

    assert (sub.set_option (zlink::socket_option::subscribe, topic_a, 1) == 0);
    recv_array_expect_success (pub, subscribe_a_msg, 2);
    send_string_expect_success (pub, topic_a);
    recv_string_expect_success (sub, topic_a);

    assert (sub.set_option (zlink::socket_option::unsubscribe, topic_a, 1) == 0);
    recv_array_expect_success (pub, unsubscribe_a_msg, 2);

    assert (sub.set_option (zlink::socket_option::unsubscribe, topic_a, 1) == 0);
    expect_no_msg (pub);
}

void test_xpub_verboser_two_subs ()
{
    zlink::context_t ctx;
    zlink::xpub_socket_t pub (ctx);
    zlink::sub_socket_t sub0 (ctx);
    zlink::sub_socket_t sub1 (ctx);
    create_xpub_with_2_subs (ctx, pub, sub0, sub1);
    create_duplicate_subscription (pub, sub0, sub1);

    assert (sub0.set_option (zlink::socket_option::unsubscribe, topic_a, 1) == 0);
    expect_no_msg (pub);
    assert (sub1.set_option (zlink::socket_option::unsubscribe, topic_a, 1) == 0);
    recv_array_expect_success (pub, unsubscribe_a_msg, 2);
    expect_no_msg (pub);

    const int verboser = 1;
    assert (pub.set_option (zlink::socket_option::xpub_verboser, verboser) == 0);

    assert (sub0.set_option (zlink::socket_option::subscribe, topic_a, 1) == 0);
    assert (sub1.set_option (zlink::socket_option::subscribe, topic_a, 1) == 0);
    recv_array_expect_success (pub, subscribe_a_msg, 2);
    recv_array_expect_success (pub, subscribe_a_msg, 2);

    send_string_expect_success (pub, topic_a);
    recv_string_expect_success (sub0, topic_a);
    recv_string_expect_success (sub1, topic_a);

    assert (sub1.set_option (zlink::socket_option::unsubscribe, topic_a, 1) == 0);
    recv_array_expect_success (pub, unsubscribe_a_msg, 2);
    assert (sub0.set_option (zlink::socket_option::unsubscribe, topic_a, 1) == 0);
    recv_array_expect_success (pub, unsubscribe_a_msg, 2);

    assert (sub1.set_option (zlink::socket_option::unsubscribe, topic_a, 1) == 0);
    expect_no_msg (pub);
}

} // namespace

int main ()
{
    test_xpub_verbose_one_sub ();
    test_xpub_verbose_two_subs ();
    test_xpub_verboser_one_sub ();
    test_xpub_verboser_two_subs ();
    return 0;
}
