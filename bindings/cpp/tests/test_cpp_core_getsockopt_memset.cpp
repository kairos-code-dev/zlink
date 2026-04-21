#include "test_helpers.hpp"

#include <cstring>

int main ()
{
    zlink::context_t ctx;
    zlink::pub_socket_t pub (ctx);
    zlink::sub_socket_t sub (ctx);

    pub.bind ("inproc://cpp-getsockopt-memset");
    sub.connect ("inproc://cpp-getsockopt-memset");

    int64_t more = -1;
    size_t more_size = sizeof (more);
    std::memset (&more, 0xFF, sizeof (more));

    assert (zlink_getsockopt (sub.handle (), ZLINK_RCVMORE, &more, &more_size) == 0);
    assert (more_size == sizeof (int));
    assert (more == 0);

    return 0;
}
