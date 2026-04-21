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
void test_subscribe_cancel (zlink::xpub_socket_t &xpub_,
                            zlink::sub_socket_t &sub_,
                            const char (&topic_)[SIZE])
{
    const size_t topic_len = SIZE - 1;
    assert (sub_.set_option (zlink::socket_option::subscribe, topic_, topic_len) == 0);

    std::vector<char> buffer (topic_len + 5);
    int rc =
      recv_with_timeout (xpub_, buffer.data (), buffer.size (), 4000);
    assert (rc == static_cast<int> (topic_len + 1));
    assert (static_cast<unsigned char> (buffer[0]) == 1);
    assert (std::memcmp (buffer.data () + 1, topic_, topic_len) == 0);

    assert (sub_.set_option (zlink::socket_option::unsubscribe, topic_, topic_len) == 0);
    rc = recv_with_timeout (xpub_, buffer.data (), buffer.size (), 4000);
    assert (rc == static_cast<int> (topic_len + 1));
    assert (static_cast<unsigned char> (buffer[0]) == 0);
    assert (std::memcmp (buffer.data () + 1, topic_, topic_len) == 0);
}

} // namespace

int main ()
{
    zlink::context_t ctx;
    zlink::xpub_socket_t xpub (ctx);
    zlink::sub_socket_t sub (ctx);

    const std::string endpoint =
      endpoint_for (transport_case_t{"tcp", ""}, "xpub-topic");
    xpub.bind (endpoint);
    sub.connect (endpoint);

    test_subscribe_cancel (xpub, sub, short_topic);
    test_subscribe_cancel (xpub, sub, long_topic);
    return 0;
}
