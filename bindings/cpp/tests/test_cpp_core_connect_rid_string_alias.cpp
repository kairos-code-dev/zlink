#include "test_helpers.hpp"

#include <cerrno>
#include <cstring>

int main ()
{
    zlink::context_t ctx;
    zlink::stream_socket_t socket (ctx);

    const int zero = 0;
    assert (socket.set_option (zlink::socket_option::linger, zero) == 0);

    const char *alias = "stream-alias";
    assert (socket.set_option (zlink::socket_option::connect_routing_id, alias,
                        std::strlen (alias))
            == -1);
    assert (zlink_errno () == EOPNOTSUPP);

    const unsigned char fixed_id[4] = {'R', 'I', 'D', '1'};
    assert (socket.set_option (zlink::socket_option::connect_routing_id, fixed_id,
                        sizeof (fixed_id))
            == -1);
    assert (zlink_errno () == EOPNOTSUPP);

    return 0;
}
