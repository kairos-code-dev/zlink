#include "test_helpers.hpp"

int main ()
{
    zlink::context_t ctx;
    zlink::socket_t socket (ctx, zlink::socket_type::pub);

    assert (socket.connect ("tcp://127.0.0.1:0;localhost:1234") == 0);
    assert (socket.connect ("tcp://localhost:5555;localhost:1235") == 0);
    assert (socket.connect ("tcp://lo:5555;localhost:1235") == 0);

    return 0;
}
