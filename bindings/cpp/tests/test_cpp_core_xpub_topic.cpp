#include "test_helpers.hpp"

#include <cstring>

namespace {

const char short_topic[] =
  "ABCDEFGHIJKLMNOPABCDEFGHIJKLMNOPABCDEFGHIJKLMNOPABCDEFGHIJKLMNOP"
  "ABCDEFGHIJKLMNOPABCDEFGHIJKLMNOPABCDEFGHIJKLMNOPABCDEFGHIJKLMNOP"
  "ABCDEFGHIJKLMNOPABCDEFGHIJKLMNOPABCDEFGHIJKLMNOPABCDEFGHIJKLMNOP"
  "ABCDEFGHIJKLMNOPABCDEFGHIJKLMNOPABCDEFGHIJKLMNOPABCDE";

const char long_topic[] =
  "ABCDEFGHIJKLMNOPABCDEFGHIJKLMNOPABCDEFGHIJKLMNOPABCDEFGHIJKLMNOP"
  "ABCDEFGHIJKLMNOPABCDEFGHIJKLMNOPABCDEFGHIJKLMNOPABCDEFGHIJKLMNOP"
  "ABCDEFGHIJKLMNOPABCDEFGHIJKLMNOPABCDEFGHIJKLMNOPABCDEFGHIJKLMNOP"
  "ABCDEFGHIJKLMNOPABCDEFGHIJKLMNOPABCDEFGHIJKLMNOPABCDEF";

template <size_t SIZE>
void test_subscribe_cancel (zlink::socket_t &xpub_,
                            zlink::socket_t &sub_,
                            const char (&topic_)[SIZE])
{
    const size_t topic_len = SIZE - 1;
    assert (sub_.set (zlink::socket_option::subscribe, topic_, topic_len) == 0);

    std::vector<char> buffer (topic_len + 5);
    int rc =
      recv_with_timeout (xpub_, buffer.data (), buffer.size (), 4000);
    assert (rc == static_cast<int> (topic_len + 1));
    assert (static_cast<unsigned char> (buffer[0]) == 1);
    assert (std::memcmp (buffer.data () + 1, topic_, topic_len) == 0);

    assert (sub_.set (zlink::socket_option::unsubscribe, topic_, topic_len) == 0);
    rc = recv_with_timeout (xpub_, buffer.data (), buffer.size (), 4000);
    assert (rc == static_cast<int> (topic_len + 1));
    assert (static_cast<unsigned char> (buffer[0]) == 0);
    assert (std::memcmp (buffer.data () + 1, topic_, topic_len) == 0);
}

} // namespace

int main ()
{
    zlink::context_t ctx;
    zlink::socket_t xpub (ctx, zlink::socket_type::xpub);
    zlink::socket_t sub (ctx, zlink::socket_type::sub);

    const std::string endpoint =
      endpoint_for (transport_case_t{"tcp", ""}, "xpub-topic");
    assert (xpub.bind (endpoint) == 0);
    assert (sub.connect (endpoint) == 0);

    test_subscribe_cancel (xpub, sub, short_topic);
    test_subscribe_cancel (xpub, sub, long_topic);
    return 0;
}
