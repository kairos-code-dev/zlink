#include "test_helpers.hpp"

int main ()
{
    zlink::context_t ctx;
    zlink::pub_socket_t socket (ctx);

    socket.connect ("tcp://127.0.0.1:0;localhost:1234");
    socket.connect ("tcp://localhost:5555;localhost:1235");
    socket.connect ("tcp://lo:5555;localhost:1235");

    return 0;
}
