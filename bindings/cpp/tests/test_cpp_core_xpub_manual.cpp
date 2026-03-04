#include "test_helpers.hpp"

#include <cstring>

namespace {

void test_basic_manual ()
{
    zlink::context_t ctx;
    zlink::socket_t xpub (ctx, zlink::socket_type::xpub);
    zlink::socket_t xsub (ctx, zlink::socket_type::xsub);

    const int manual = 1;
    assert (xpub.set (zlink::socket_option::xpub_manual, manual) == 0);

    const std::string endpoint = unique_inproc ("inproc://cpp-xpub-manual-", "basic");
    assert (xpub.bind (endpoint) == 0);
    assert (xsub.connect (endpoint) == 0);

    const unsigned char subscription[] = {1, 'A'};
    assert (xsub.send (subscription, sizeof (subscription))
            == static_cast<int> (sizeof (subscription)));

    unsigned char sub_msg[8];
    const int rc = recv_with_timeout (xpub, sub_msg, sizeof (sub_msg), 2000);
    assert (rc == static_cast<int> (sizeof (subscription)));
    assert (std::memcmp (sub_msg, subscription, sizeof (subscription)) == 0);

    assert (xpub.set (zlink::socket_option::subscribe, "B", 1) == 0);

    send_string_expect_success (xpub, "A");
    send_string_expect_success (xpub, "B");

    recv_string_expect_success (xsub, "B");
}

void test_unsubscribe_manual ()
{
    zlink::context_t ctx;
    zlink::socket_t xpub (ctx, zlink::socket_type::xpub);
    zlink::socket_t xsub (ctx, zlink::socket_type::xsub);

    const int manual = 1;
    assert (xpub.set (zlink::socket_option::xpub_manual, manual) == 0);

    const std::string endpoint = unique_inproc ("inproc://cpp-xpub-manual-", "unsub");
    assert (xpub.bind (endpoint) == 0);
    assert (xsub.connect (endpoint) == 0);

    const unsigned char sub_a[] = {1, 'A'};
    const unsigned char sub_b[] = {1, 'B'};
    assert (xsub.send (sub_a, sizeof (sub_a)) == static_cast<int> (sizeof (sub_a)));
    assert (xsub.send (sub_b, sizeof (sub_b)) == static_cast<int> (sizeof (sub_b)));

    unsigned char frame[8];
    assert (recv_with_timeout (xpub, frame, sizeof (frame), 2000)
            == static_cast<int> (sizeof (sub_a)));
    assert (std::memcmp (frame, sub_a, sizeof (sub_a)) == 0);
    assert (xpub.set (zlink::socket_option::subscribe, "XA", 2) == 0);

    assert (recv_with_timeout (xpub, frame, sizeof (frame), 2000)
            == static_cast<int> (sizeof (sub_b)));
    assert (std::memcmp (frame, sub_b, sizeof (sub_b)) == 0);
    assert (xpub.set (zlink::socket_option::subscribe, "XB", 2) == 0);

    const unsigned char unsub_a[] = {0, 'A'};
    assert (xsub.send (unsub_a, sizeof (unsub_a))
            == static_cast<int> (sizeof (unsub_a)));

    assert (recv_with_timeout (xpub, frame, sizeof (frame), 2000)
            == static_cast<int> (sizeof (unsub_a)));
    assert (std::memcmp (frame, unsub_a, sizeof (unsub_a)) == 0);
    assert (xpub.set (zlink::socket_option::unsubscribe, "XA", 2) == 0);

    send_string_expect_success (xpub, "XA");
    send_string_expect_success (xpub, "XB");

    recv_string_expect_success (xsub, "XB");
}

} // namespace

int main ()
{
    test_basic_manual ();
    test_unsubscribe_manual ();
    return 0;
}
