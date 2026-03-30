#include "test_helpers.hpp"

#include <cerrno>

int main ()
{
    zlink::context_t ctx;
    zlink::pub_socket_t sock (ctx);

    assert (sock.connect ("tcp://localhost:1234") == 0);
    assert (sock.connect ("tcp://[::1]:1234") == 0);

    assert (sock.connect ("tcp://localhost:invalid") == -1);
    assert (sock.connect ("tcp://in val id:1234") == -1);
    assert (sock.connect ("tcp://") == -1);

    assert (sock.connect ("invalid://localhost:1234") == -1);
    assert (zlink_errno () == EPROTONOSUPPORT);

    return 0;
}
