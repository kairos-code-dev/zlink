#include "test_helpers.hpp"

#include <cassert>
#include <cstdlib>
#include <cstring>

namespace {

void counting_free_fn (void *data_, void *hint_) noexcept
{
    int *count = static_cast<int *> (hint_);
    if (count)
        ++(*count);
    std::free (data_);
}

void test_socket_send_msg_consumes_on_failure ()
{
    zlink::context_t ctx;
    zlink::pair_socket_t sender (ctx);

    zlink::message_t msg (4);
    assert (msg.valid ());

    assert (sender.send (msg, zlink::send_flag::dontwait) == -1);
    assert (!msg.valid ());
}

void test_stream_send_msg_consumes_on_failure ()
{
    zlink::context_t ctx;
    zlink::stream_socket_t stream (ctx);

    zlink::message_t msg (3);
    assert (msg.valid ());

    const zlink_routing_id_t routing_id = routing_id_from_uint32 (1u);
    assert (stream.send (routing_id, msg, zlink::send_flag::dontwait) == -1);
    assert (!msg.valid ());
}

void test_socket_send_zero_consumes_on_failure ()
{
    zlink::context_t ctx;
    zlink::pair_socket_t sender (ctx);

    int free_count = 0;
    char *payload = static_cast<char *> (std::malloc (4));
    assert (payload != NULL);
    std::memcpy (payload, "ping", 4);

    assert (sender.send_zero (payload, 4, &counting_free_fn, &free_count,
                              zlink::send_flag::dontwait)
            == -1);
    assert (free_count == 1);
}

void test_socket_send_zero_invalid_input_does_not_consume ()
{
    zlink::context_t ctx;
    zlink::pair_socket_t sender (ctx);

    int free_count = 0;
    assert (sender.send_zero (NULL, 1, &counting_free_fn, &free_count,
                              zlink::send_flag::dontwait)
            == -1);
    assert (free_count == 0);
}

} // namespace

int main ()
{
    test_socket_send_msg_consumes_on_failure ();
    test_stream_send_msg_consumes_on_failure ();
    test_socket_send_zero_consumes_on_failure ();
    test_socket_send_zero_invalid_input_does_not_consume ();
    return 0;
}
