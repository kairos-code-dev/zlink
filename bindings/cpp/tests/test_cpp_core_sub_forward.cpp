#include "test_helpers.hpp"

#include <cstring>

namespace {

void test_sub_forward ()
{
    zlink::context_t ctx;

    zlink::socket_t xpub (ctx, zlink::socket_type::xpub);
    zlink::socket_t xsub (ctx, zlink::socket_type::xsub);
    zlink::socket_t pub (ctx, zlink::socket_type::pub);
    zlink::socket_t sub (ctx, zlink::socket_type::sub);

    const std::string endpoint_upstream =
      endpoint_for (transport_case_t{"tcp", ""}, "sub-forward-up");
    const std::string endpoint_downstream =
      endpoint_for (transport_case_t{"tcp", ""}, "sub-forward-down");

    assert (xpub.bind (endpoint_upstream) == 0);
    assert (xsub.bind (endpoint_downstream) == 0);

    assert (pub.connect (endpoint_downstream) == 0);
    assert (sub.connect (endpoint_upstream) == 0);
    assert (sub.set (zlink::socket_option::subscribe, "", 0) == 0);

    char buff[32];
    std::memset (buff, 0, sizeof (buff));

    const int sub_size = recv_with_timeout (xpub, buff, sizeof (buff), 2000);
    assert (sub_size >= 1);
    assert (xsub.send (buff, static_cast<size_t> (sub_size)) == sub_size);

    sleep_ms (300);

    assert (pub.send ("", 0) == 0);

    const int msg_size = recv_with_timeout (xsub, buff, sizeof (buff), 2000);
    assert (msg_size == 0);
    assert (xpub.send (buff, 0) == 0);

    assert (recv_with_timeout (sub, buff, sizeof (buff), 2000) == 0);
}

} // namespace

int main ()
{
    test_sub_forward ();
    return 0;
}
