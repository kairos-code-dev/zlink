#include "test_helpers.hpp"

#include <cassert>

namespace {

void test_socket_send_msg_consumes_on_failure ()
{
    zlink::context_t ctx;
    zlink::socket_t sender (ctx, zlink::socket_type::pair);

    zlink::message_t msg (4);
    assert (msg.valid ());

    assert (sender.send (msg, zlink::send_flag::dontwait) == -1);
    assert (!msg.valid ());
}

void test_stream_send_msg_consumes_on_failure ()
{
    zlink::context_t ctx;
    zlink::socket_t stream (ctx, zlink::socket_type::stream);

    zlink::message_t msg (3);
    assert (msg.valid ());

    assert (stream.stream_send_msg (1u, msg, zlink::send_flag::dontwait) == -1);
    assert (!msg.valid ());
}

} // namespace

int main ()
{
    test_socket_send_msg_consumes_on_failure ();
    test_stream_send_msg_consumes_on_failure ();
    return 0;
}
