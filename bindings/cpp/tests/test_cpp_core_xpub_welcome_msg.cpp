#include "test_helpers.hpp"

#include <cstdint>
#include <cstring>

int main ()
{
    zlink::context_t ctx;
    zlink::xpub_socket_t pub (ctx);
    zlink::sub_socket_t sub (ctx);

    const std::string endpoint =
      unique_inproc ("inproc://cpp-xpub-welcome-", "msg");
    pub.bind (endpoint);
    assert (pub.set_option (zlink::socket_option::xpub_welcome_msg, "W", 1) == 0);

    assert (sub.set_option (zlink::socket_option::subscribe, "W", 1) == 0);
    sub.connect (endpoint);

    char sub_cmd[8];
    std::memset (sub_cmd, 0, sizeof (sub_cmd));
    const int rc = recv_with_timeout (pub, sub_cmd, sizeof (sub_cmd), 2000);
    assert (rc == 2);
    assert (static_cast<uint8_t> (sub_cmd[0]) == 1);
    assert (sub_cmd[1] == 'W');

    recv_string_expect_success (sub, "W");
    return 0;
}
