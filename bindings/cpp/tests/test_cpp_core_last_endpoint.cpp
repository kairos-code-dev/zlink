#include "test_helpers.hpp"

#include <cstring>

static void bind_and_verify (zlink::socket_t &sock_, const std::string &endpoint_)
{
    assert (sock_.bind (endpoint_) == 0);

    char reported[255];
    std::memset (reported, 0, sizeof (reported));
    size_t size = sizeof (reported);
    assert (sock_.get (zlink::socket_option::last_endpoint, reported, &size) == 0);
    assert (std::string (reported) == endpoint_);
}

int main ()
{
    zlink::context_t ctx;
    zlink::socket_t router (ctx, zlink::socket_type::router);

    const int linger = 0;
    assert (router.set (zlink::socket_option::linger, linger) == 0);

    const std::string ep1 = unique_inproc ("inproc://cpp-last-endpoint-", "1");
    const std::string ep2 = unique_inproc ("inproc://cpp-last-endpoint-", "2");

    bind_and_verify (router, ep1);
    bind_and_verify (router, ep2);

    return 0;
}
