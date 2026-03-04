#include "test_helpers.hpp"

#include <cerrno>

int main ()
{
    zlink::context_t ctx;
    zlink::socket_t pub (ctx, zlink::socket_type::xpub);
    zlink::socket_t sub (ctx, zlink::socket_type::sub);

    const int hwm = 64;
    const int nodrop = 1;
    const int rcvtimeo = 50;
    const int zero = 0;

    assert (pub.set (zlink::socket_option::linger, zero) == 0);
    assert (sub.set (zlink::socket_option::linger, zero) == 0);

    assert (pub.set (zlink::socket_option::sndhwm, hwm) == 0);
    assert (pub.set (zlink::socket_option::xpub_nodrop, nodrop) == 0);

    const std::string endpoint = unique_inproc ("inproc://cpp-xpub-nodrop-", "ep");
    assert (pub.bind (endpoint) == 0);

    assert (sub.connect (endpoint) == 0);
    assert (sub.set (zlink::socket_option::subscribe, "", 0) == 0);
    assert (sub.set (zlink::socket_option::rcvtimeo, rcvtimeo) == 0);

    char sub_cmd[8];
    assert (recv_with_timeout (pub, sub_cmd, sizeof (sub_cmd), 2000) >= 1);

    int send_count = 0;
    const int max_attempts = 256;
    for (int i = 0; i < max_attempts; ++i) {
        const int rc = zlink_send (pub.handle (), NULL, 0, ZLINK_DONTWAIT);
        if (rc == 0) {
            ++send_count;
            continue;
        }
        assert (zlink_errno () == EAGAIN);
        break;
    }
    assert (send_count > 0);

    int recv_count = 0;
    int idle_rounds = 0;
    while (true) {
        const int rc = zlink_recv (sub.handle (), NULL, 0, ZLINK_DONTWAIT);
        if (rc == -1) {
            assert (zlink_errno () == EAGAIN);
            if (++idle_rounds > 200)
                break;
            sleep_ms (1);
            continue;
        }
        assert (rc == 0);
        idle_rounds = 0;
        ++recv_count;
    }

    assert (send_count == recv_count);
    return 0;
}
