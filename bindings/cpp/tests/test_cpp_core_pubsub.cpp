#include "test_helpers.hpp"

namespace {

void test_pubsub_tcp ()
{
    zlink::context_t ctx;

    zlink::pub_socket_t publisher (ctx);
    zlink::sub_socket_t subscriber (ctx);

    const std::string endpoint =
      endpoint_for (transport_case_t{"tcp", ""}, "pubsub");
    assert (publisher.bind (endpoint) == 0);
    assert (subscriber.connect (endpoint) == 0);

    assert (subscriber.set_option (zlink::socket_option::subscribe, "", 0) == 0);
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
