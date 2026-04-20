#include "test_helpers.hpp"

#include <zlink_c.h>

#include <cerrno>

int main ()
{
    zlink::context_t ctx;
    zlink::xpub_socket_t pub (ctx);
    zlink::sub_socket_t sub (ctx);

    const int hwm = 64;
    const int nodrop = 1;
    const int rcvtimeo = 50;
    const int zero = 0;

    assert (pub.set_option (zlink::socket_option::linger, zero) == 0);
    assert (sub.set_option (zlink::socket_option::linger, zero) == 0);

    assert (pub.set_option (zlink::socket_option::sndhwm, hwm) == 0);
    assert (pub.set_option (zlink::socket_option::xpub_nodrop, nodrop) == 0);

    const std::string endpoint = unique_inproc ("inproc://cpp-xpub-nodrop-", "ep");
    assert (pub.bind (endpoint) == 0);

    assert (sub.connect (endpoint) == 0);
    assert (sub.set_option (zlink::socket_option::subscribe, "", 0) == 0);
    assert (sub.set_option (zlink::socket_option::rcvtimeo, rcvtimeo) == 0);

    char sub_cmd[8];
    assert (recv_with_timeout (pub, sub_cmd, sizeof (sub_cmd), 2000) >= 1);

    int send_count = 0;
    const int max_attempts = 256;
    for (int i = 0; i < max_attempts; ++i) {
        zlink_msg_t part;
        assert (zlink_msg_init (&part) == 0);
        const zlink_submit_result_t rc = zlink_send_part (
          pub.handle (), &part,
          static_cast<zlink_send_flags_t> (ZLINK_DONTWAIT), ZLINK_PART_FINAL);
        if (rc == ZLINK_SUBMIT_OK) {
            ++send_count;
            continue;
        }
        assert (zlink_msg_close (&part) == 0);
        assert (zlink_errno () == EAGAIN);
        break;
    }
    assert (send_count > 0);

    int recv_count = 0;
    int idle_rounds = 0;
    while (true) {
        const zlink_routing_id_t *source_rid = NULL;
        zlink_msg_t part;
        assert (zlink_msg_init (&part) == 0);
        zlink_part_flag_t has_more = ZLINK_PART_FINAL;
        const zlink_recv_result_t rc = zlink_recv_part (
          sub.handle (), &source_rid, &part, &has_more,
          static_cast<zlink_recv_flags_t> (ZLINK_DONTWAIT));
        if (rc != ZLINK_RECV_OK) {
            assert (zlink_msg_close (&part) == 0);
            assert (zlink_errno () == EAGAIN);
            if (++idle_rounds > 200)
                break;
            sleep_ms (1);
            continue;
        }
        assert (!has_more);
        assert (zlink_msg_size (&part) == 0);
        assert (zlink_msg_close (&part) == 0);
        idle_rounds = 0;
        ++recv_count;
    }

    assert (send_count == recv_count);
    return 0;
}
