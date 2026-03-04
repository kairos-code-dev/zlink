#include "test_helpers.hpp"

#include <cerrno>

int main ()
{
    zlink::context_t ctx;
    zlink::socket_t stream (ctx, zlink::socket_type::stream);

    char recv_buf[64];
    assert (stream.recv (recv_buf, sizeof (recv_buf), zlink::recv_flag::dontwait)
            == -1);
    assert (zlink_errno () == EAGAIN);

    zlink::message_t msg;
    assert (stream.recv (msg, zlink::recv_flag::dontwait) == -1);
    assert (zlink_errno () == EAGAIN);

    return 0;
}
