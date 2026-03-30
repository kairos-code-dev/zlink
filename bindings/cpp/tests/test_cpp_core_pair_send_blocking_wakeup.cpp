#include "test_helpers.hpp"

#include <chrono>
#include <cstring>
#include <future>
#include <thread>

namespace
{
const int kPipeHwm = 100;
const int kSocketHwm = kPipeHwm / 2;
const int kLwm = (kPipeHwm + 1) / 2;
const int kSendTimeoutMs = 2000;
const size_t kMsgSize = 64;
const char *kEndpoint = "inproc://pair_send_blocking_wakeup";
const char *kTimeoutEndpoint = "inproc://pair_send_blocking_timeout";

template<typename SocketLike>
void configure_pair_socket (SocketLike &socket_)
{
    assert (socket_.set_option (zlink::socket_option::sndhwm, kSocketHwm) == 0);
    assert (socket_.set_option (zlink::socket_option::rcvhwm, kSocketHwm) == 0);
}

template<typename SocketLike>
void fill_until_hwm (SocketLike &sender_, const char *buffer_)
{
    int sent = 0;
    while (raw_send (sender_, buffer_, kMsgSize, zlink::send_flag::dontwait)
           == static_cast<int> (kMsgSize))
        ++sent;

    assert (sent > 0);
    assert (zlink_errno () == EAGAIN);
}

void test_pair_blocking_send_wakes_only_after_lwm_reads ()
{
    zlink::context_t ctx;
    zlink::pair_socket_t receiver (ctx);
    zlink::pair_socket_t sender (ctx);
    configure_pair_socket (receiver);
    configure_pair_socket (sender);

    assert (sender.set_option (zlink::socket_option::sndtimeo, kSendTimeoutMs) == 0);

    assert (receiver.bind (kEndpoint) == 0);
    assert (sender.connect (kEndpoint) == 0);

    char send_buf[kMsgSize];
    char recv_buf[kMsgSize];
    std::memset (send_buf, 'a', sizeof (send_buf));
    std::memset (recv_buf, 0, sizeof (recv_buf));

    fill_until_hwm (sender, send_buf);

    std::future<int> send_future = std::async (std::launch::async, [&] () {
        return raw_send (sender, send_buf, kMsgSize);
    });

    std::this_thread::sleep_for (std::chrono::milliseconds (100));

    for (int i = 0; i < kLwm - 1; ++i) {
        const int rc = raw_recv (receiver, recv_buf, kMsgSize);
        assert (rc == static_cast<int> (kMsgSize));
    }

    const std::future_status state_before_lwm =
      send_future.wait_for (std::chrono::milliseconds (100));

    const int lwm_rc = raw_recv (receiver, recv_buf, kMsgSize);
    assert (lwm_rc == static_cast<int> (kMsgSize));

    const std::future_status state_after_lwm =
      send_future.wait_for (std::chrono::milliseconds (1500));
    if (state_after_lwm != std::future_status::ready)
        send_future.wait ();
    const int send_rc = send_future.get ();

    assert (state_before_lwm == std::future_status::timeout);
    assert (state_after_lwm == std::future_status::ready);
    assert (send_rc == static_cast<int> (kMsgSize));
}

void test_pair_blocking_send_times_out_without_recv ()
{
    zlink::context_t ctx;
    zlink::pair_socket_t receiver (ctx);
    zlink::pair_socket_t sender (ctx);
    configure_pair_socket (receiver);
    configure_pair_socket (sender);

    assert (sender.set_option (zlink::socket_option::sndtimeo, kSendTimeoutMs) == 0);

    assert (receiver.bind (kTimeoutEndpoint) == 0);
    assert (sender.connect (kTimeoutEndpoint) == 0);

    char send_buf[kMsgSize];
    std::memset (send_buf, 'b', sizeof (send_buf));

    fill_until_hwm (sender, send_buf);

    void *stopwatch = zlink_stopwatch_start ();
    assert (raw_send (sender, send_buf, kMsgSize) == -1);
    const unsigned int elapsed_ms = zlink_stopwatch_stop (stopwatch) / 1000;

    assert (zlink_errno () == EAGAIN);
    assert (elapsed_ms >= static_cast<unsigned int> (kSendTimeoutMs - 250));
}
} // namespace

int main ()
{
    test_pair_blocking_send_wakes_only_after_lwm_reads ();
    test_pair_blocking_send_times_out_without_recv ();
    return 0;
}
