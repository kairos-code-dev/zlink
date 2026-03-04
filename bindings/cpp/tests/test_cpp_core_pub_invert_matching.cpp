#include "test_helpers.hpp"

#include <cerrno>
#include <cstring>

namespace {

void assert_recv_eagain (zlink::socket_t &socket_)
{
    char buf[64];
    std::memset (buf, 0, sizeof (buf));
    assert (socket_.recv (buf, sizeof (buf), zlink::recv_flag::dontwait) == -1);
    assert (zlink_errno () == EAGAIN);
}

void test_pub_invert_matching ()
{
    zlink::context_t ctx;
    zlink::socket_t pub (ctx, zlink::socket_type::pub);
    zlink::socket_t sub1 (ctx, zlink::socket_type::sub);
    zlink::socket_t sub2 (ctx, zlink::socket_type::sub);

    const std::string endpoint =
      unique_inproc ("inproc://cpp-pub-invert-", "matching");
    assert (pub.bind (endpoint) == 0);
    assert (sub1.connect (endpoint) == 0);
    assert (sub2.connect (endpoint) == 0);

    const char prefix1[] = "prefix1";
    const char prefix2[] = "p2";
    assert (sub1.set (zlink::socket_option::subscribe, prefix1, std::strlen (prefix1))
            == 0);
    assert (sub2.set (zlink::socket_option::subscribe, prefix2, std::strlen (prefix2))
            == 0);

    send_string_expect_success (pub, prefix1);
    sleep_ms (300);
    recv_string_expect_success (sub1, prefix1, zlink::recv_flag::dontwait);
    assert_recv_eagain (sub2);

    send_string_expect_success (pub, prefix2);
    sleep_ms (300);
    recv_string_expect_success (sub2, prefix2, zlink::recv_flag::dontwait);
    assert_recv_eagain (sub1);

    const int invert = 1;
    assert (pub.set (zlink::socket_option::invert_matching, invert) == 0);
    assert (sub1.set (zlink::socket_option::invert_matching, invert) == 0);
    assert (sub2.set (zlink::socket_option::invert_matching, invert) == 0);

    send_string_expect_success (pub, prefix1);
    sleep_ms (300);
    recv_string_expect_success (sub2, prefix1, zlink::recv_flag::dontwait);
    assert_recv_eagain (sub1);

    send_string_expect_success (pub, prefix2);
    sleep_ms (300);
    recv_string_expect_success (sub1, prefix2, zlink::recv_flag::dontwait);
    assert_recv_eagain (sub2);
}

} // namespace

int main ()
{
    test_pub_invert_matching ();
    return 0;
}
