#include "test_helpers.hpp"

#include <cerrno>

int main ()
{
    zlink::context_t ctx;
    zlink::pub_socket_t sock (ctx);

    sock.connect ("tcp://localhost:1234");
    sock.connect ("tcp://[::1]:1234");

    try {
        sock.connect ("tcp://localhost:invalid");
        assert (false);
    } catch (const zlink::connect_error_t &) {
    }
    try {
        sock.connect ("tcp://in val id:1234");
        assert (false);
    } catch (const zlink::connect_error_t &) {
    }
    try {
        sock.connect ("tcp://");
        assert (false);
    } catch (const zlink::connect_error_t &) {
    }

    try {
        sock.connect ("invalid://localhost:1234");
        assert (false);
    } catch (const zlink::connect_error_t &) {
    }
    assert (zlink_errno () == EPROTONOSUPPORT);

    return 0;
}
