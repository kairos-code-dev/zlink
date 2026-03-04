#include "test_helpers.hpp"

#include <cstring>

int main ()
{
    zlink::context_t ctx;
    zlink::socket_t pub (ctx, zlink::socket_type::pub);
    zlink::socket_t sub (ctx, zlink::socket_type::sub);

    assert (pub.bind ("inproc://cpp-getsockopt-memset") == 0);
    assert (sub.connect ("inproc://cpp-getsockopt-memset") == 0);

    int64_t more = -1;
    size_t more_size = sizeof (more);
    std::memset (&more, 0xFF, sizeof (more));

    assert (zlink_getsockopt (sub.handle (), ZLINK_RCVMORE, &more, &more_size) == 0);
    assert (more_size == sizeof (int));
    assert (more == 0);

    return 0;
}
