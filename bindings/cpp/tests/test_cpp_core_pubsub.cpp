#include "test_helpers.hpp"

namespace {

void test_pubsub_tcp ()
{
    zlink::context_t ctx;

    zlink::socket_t publisher (ctx, zlink::socket_type::pub);
    zlink::socket_t subscriber (ctx, zlink::socket_type::sub);

    const std::string endpoint =
      endpoint_for (transport_case_t{"tcp", ""}, "pubsub");
    assert (publisher.bind (endpoint) == 0);
    assert (subscriber.connect (endpoint) == 0);

    assert (subscriber.set (zlink::socket_option::subscribe, "", 0) == 0);
    sleep_ms (300);

    send_string_expect_success (publisher, "test");
    recv_string_expect_success (subscriber, "test");
}

} // namespace

int main ()
{
    test_pubsub_tcp ();
    return 0;
}
