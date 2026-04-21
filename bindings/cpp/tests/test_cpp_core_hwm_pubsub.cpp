#include "test_helpers.hpp"

#include <cstring>

namespace
{
const int kSettleTimeMs = 300;

void test_default_socket_hwm_is_1000 ()
{
    zlink::context_t ctx;
    zlink::pair_socket_t socket (ctx);

    int sndhwm = 0;
    int rcvhwm = 0;
    assert (socket.get_option (zlink::socket_option::sndhwm, &sndhwm) == 0);
    assert (socket.get_option (zlink::socket_option::rcvhwm, &rcvhwm) == 0);
    assert (sndhwm == 1000);
    assert (rcvhwm == 1000);
}

int test_defaults (int send_hwm_, int msg_cnt_, const char *bind_endpoint_)
{
    zlink::context_t ctx;

    zlink::xpub_socket_t pub_socket (ctx);
    assert (pub_socket.set_option (zlink::socket_option::sndhwm, send_hwm_) == 0);
    pub_socket.bind (bind_endpoint_);
    const std::string pub_endpoint = bound_endpoint (pub_socket);

    zlink::sub_socket_t sub_socket (ctx);
    assert (sub_socket.set_option (zlink::socket_option::rcvhwm, send_hwm_) == 0);
    assert (sub_socket.set_option (zlink::socket_option::subscribe, "", 0) == 0);
    sub_socket.connect (pub_endpoint);

    const unsigned char subscription_to_all_topics[] = {1};
    zlink::message_t subscription;
    assert (recv_msg_with_timeout (pub_socket, subscription, 3000) == 1);
    assert (std::memcmp (subscription.data (), subscription_to_all_topics, 1) == 0);

    int send_count = 0;
    while (send_count < msg_cnt_
           && raw_send (pub_socket, "test message", 13,
                        zlink::send_flag::dontwait)
                == 13)
        ++send_count;

    assert (send_count >= send_hwm_);
    sleep_ms (kSettleTimeMs);

    int recv_count = 0;
    char dummy[64];
    while (raw_recv (sub_socket, dummy, sizeof (dummy),
                     zlink::recv_flag::dontwait)
           == 13)
        ++recv_count;

    assert (recv_count > 0);
    assert (recv_count <= send_count);
    return recv_count;
}

template<typename SocketLike>
int receive (SocketLike &socket_, int *is_termination_)
{
    int recv_count = 0;
    *is_termination_ = 0;

    char buffer[255];
    int len = -1;
    while ((len = raw_recv (socket_, buffer, sizeof (buffer))) >= 0) {
        ++recv_count;
        if (len == 3 && std::strncmp (buffer, "end", len) == 0) {
            *is_termination_ = 1;
            return recv_count;
        }
    }

    return recv_count;
}

int test_blocking (int send_hwm_, int msg_cnt_, const char *bind_endpoint_)
{
    zlink::context_t ctx;

    zlink::xpub_socket_t pub_socket (ctx);
    assert (pub_socket.set_option (zlink::socket_option::sndhwm, send_hwm_) == 0);
    pub_socket.bind (bind_endpoint_);
    const std::string pub_endpoint = bound_endpoint (pub_socket);

    zlink::sub_socket_t sub_socket (ctx);
    assert (sub_socket.set_option (zlink::socket_option::rcvhwm, send_hwm_) == 0);

    const int wait = 1;
    assert (pub_socket.set_option (zlink::socket_option::xpub_nodrop, wait) == 0);
    const int timeout_ms = 10;
    assert (sub_socket.set_option (zlink::socket_option::rcvtimeo, timeout_ms) == 0);
    assert (sub_socket.set_option (zlink::socket_option::subscribe, "", 0) == 0);
    sub_socket.connect (pub_endpoint);

    const unsigned char subscription_to_all_topics[] = {1};
    zlink::message_t subscription;
    assert (recv_msg_with_timeout (pub_socket, subscription, 3000) == 1);
    assert (std::memcmp (subscription.data (), subscription_to_all_topics, 1) == 0);

    int send_count = 0;
    int recv_count = 0;
    int blocked_count = 0;
    int is_termination = 0;

    while (send_count < msg_cnt_) {
        const int rc = raw_send (pub_socket, NULL, 0, zlink::send_flag::dontwait);
        if (rc == 0) {
            ++send_count;
        } else {
            assert (rc == -1);
            assert (zlink_errno () == EAGAIN);
            ++blocked_count;
            recv_count += receive (sub_socket, &is_termination);
        }
    }

    assert (blocked_count > 0);

    recv_count += receive (sub_socket, &is_termination);
    assert (raw_send (pub_socket, "end", 3) == 3);
    while (!is_termination)
        recv_count += receive (sub_socket, &is_termination);
    --recv_count;

    assert (send_count == recv_count);
    return recv_count;
}

void test_reset_hwm ()
{
    const int first_count = 9999;
    const int second_count = 1100;
    const int hwm = 11024;

    zlink::context_t ctx;

    zlink::pub_socket_t pub_socket (ctx);
    assert (pub_socket.set_option (zlink::socket_option::sndhwm, hwm) == 0);
    pub_socket.bind ("tcp://127.0.0.1:*");
    const std::string endpoint = bound_endpoint (pub_socket);

    zlink::sub_socket_t sub_socket (ctx);
    assert (sub_socket.set_option (zlink::socket_option::rcvhwm, hwm) == 0);
    sub_socket.connect (endpoint);
    assert (sub_socket.set_option (zlink::socket_option::subscribe, "", 0) == 0);

    sleep_ms (kSettleTimeMs);

    int send_count = 0;
    while (send_count < first_count
           && raw_send (pub_socket, NULL, 0, zlink::send_flag::dontwait) == 0)
        ++send_count;
    assert (send_count == first_count);

    sleep_ms (kSettleTimeMs);

    int recv_count = 0;
    while (raw_recv (sub_socket, NULL, 0, zlink::recv_flag::dontwait) == 0)
        ++recv_count;
    assert (recv_count == first_count);

    sleep_ms (kSettleTimeMs);

    send_count = 0;
    while (send_count < second_count
           && raw_send (pub_socket, NULL, 0, zlink::send_flag::dontwait) == 0)
        ++send_count;
    assert (send_count == second_count);

    sleep_ms (kSettleTimeMs);

    recv_count = 0;
    while (raw_recv (sub_socket, NULL, 0, zlink::recv_flag::dontwait) == 0)
        ++recv_count;
    assert (recv_count == second_count);
}
} // namespace

int main ()
{
    test_default_socket_hwm_is_1000 ();
    assert (test_defaults (1000, 1000, "tcp://127.0.0.1:*") > 0);
    assert (test_defaults (100, 1000, "tcp://127.0.0.1:*") > 0);
    assert (test_blocking (2000, 6000, "tcp://127.0.0.1:*") > 0);
    assert (test_defaults (1000, 1000, "inproc://a") > 0);
    assert (test_defaults (100, 1000, "inproc://a") > 0);
    assert (test_blocking (2000, 6000, "inproc://a") > 0);
#if !defined(ZLINK_HAVE_WINDOWS) && !defined(ZLINK_HAVE_GNU)
    assert (test_defaults (1000, 1000, "ipc://*") > 0);
    assert (test_defaults (100, 1000, "ipc://*") > 0);
    assert (test_blocking (2000, 6000, "ipc://*") > 0);
#endif
    test_reset_hwm ();
    return 0;
}
