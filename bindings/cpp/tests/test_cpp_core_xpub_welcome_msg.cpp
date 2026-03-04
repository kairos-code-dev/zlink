#include "test_helpers.hpp"

#include <cstdint>
#include <cstring>

int main ()
{
    zlink::context_t ctx;
    zlink::socket_t pub (ctx, zlink::socket_type::xpub);
    zlink::socket_t sub (ctx, zlink::socket_type::sub);

    const std::string endpoint =
      unique_inproc ("inproc://cpp-xpub-welcome-", "msg");
    assert (pub.bind (endpoint) == 0);
    assert (pub.set (zlink::socket_option::xpub_welcome_msg, "W", 1) == 0);

    assert (sub.set (zlink::socket_option::subscribe, "W", 1) == 0);
    assert (sub.connect (endpoint) == 0);

    char sub_cmd[8];
    std::memset (sub_cmd, 0, sizeof (sub_cmd));
    const int rc = recv_with_timeout (pub, sub_cmd, sizeof (sub_cmd), 2000);
    assert (rc == 2);
    assert (static_cast<uint8_t> (sub_cmd[0]) == 1);
    assert (sub_cmd[1] == 'W');

    recv_string_expect_success (sub, "W");
    return 0;
}
