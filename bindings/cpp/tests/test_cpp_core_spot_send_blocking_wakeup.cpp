#include "test_helpers.hpp"

#include <chrono>
#include <cstring>
#include <future>

namespace
{
const int kSocketHwm = 8;
const int kProbeWaitMs = 100;
const int kSendTimeoutMs = 1200;
const int kTimeoutSendMs = 500;
const int kRecvTimeoutMs = 200;
const int kReadyTimeoutMs = 3000;
const int kMaxFillMsgs = 20000;
const int kBackpressureStreak = 32;
const size_t kMsgSize = 262144;
const char *kTopic = "spot:blocking:wakeup";
const char *kEndpointWake = "inproc://spot_send_blocking_wakeup";
const char *kEndpointTimeout = "inproc://spot_send_blocking_timeout";

struct spot_fixture_t
{
    void *ctx;
    void *pub_node;
    void *sub_node;
    void *spot_pub;
    void *spot_sub;

    spot_fixture_t ()
        : ctx (NULL), pub_node (NULL), sub_node (NULL), spot_pub (NULL), spot_sub (NULL)
    {
    }
};

void cleanup_fixture (spot_fixture_t *fixture_)
{
    if (!fixture_)
        return;

    if (fixture_->spot_pub)
        assert (zlink_spot_pub_destroy (&fixture_->spot_pub) == 0);
    if (fixture_->spot_sub)
        assert (zlink_spot_sub_destroy (&fixture_->spot_sub) == 0);
    if (fixture_->pub_node)
        assert (zlink_spot_node_destroy (&fixture_->pub_node) == 0);
    if (fixture_->sub_node)
        assert (zlink_spot_node_destroy (&fixture_->sub_node) == 0);
    if (fixture_->ctx)
        assert (zlink_ctx_term (fixture_->ctx) == 0);
}

void configure_pub_node (void *node_, int sndtimeo_ms_)
{
    const int pub_mode = ZLINK_SPOT_NODE_PUB_MODE_SYNC;
    const int linger = 0;
    const int xpub_nodrop = 1;
    assert (zlink_spot_node_setsockopt (
              node_, ZLINK_SPOT_NODE_SOCKET_NODE, ZLINK_SPOT_NODE_OPT_PUB_MODE,
              &pub_mode, sizeof (pub_mode))
            == 0);
    assert (zlink_spot_node_setsockopt (
              node_, ZLINK_SPOT_NODE_SOCKET_PUB, ZLINK_SNDHWM, &kSocketHwm,
              sizeof (kSocketHwm))
            == 0);
    assert (zlink_spot_node_setsockopt (
              node_, ZLINK_SPOT_NODE_SOCKET_PUB, ZLINK_LINGER, &linger,
              sizeof (linger))
            == 0);
    assert (zlink_spot_node_setsockopt (
              node_, ZLINK_SPOT_NODE_SOCKET_PUB, ZLINK_XPUB_NODROP, &xpub_nodrop,
              sizeof (xpub_nodrop))
            == 0);
    assert (zlink_spot_node_setsockopt (
              node_, ZLINK_SPOT_NODE_SOCKET_PUB, ZLINK_SNDTIMEO, &sndtimeo_ms_,
              sizeof (sndtimeo_ms_))
            == 0);
}

void configure_sub_node (void *node_)
{
    const int nodrop = 1;
    const int linger = 0;
    assert (zlink_spot_node_setsockopt (
              node_, ZLINK_SPOT_NODE_SOCKET_SUB, ZLINK_RCVHWM, &kSocketHwm,
              sizeof (kSocketHwm))
            == 0);
    assert (zlink_spot_node_setsockopt (
              node_, ZLINK_SPOT_NODE_SOCKET_SUB, ZLINK_RCVTIMEO, &kRecvTimeoutMs,
              sizeof (kRecvTimeoutMs))
            == 0);
    assert (zlink_spot_node_setsockopt (
              node_, ZLINK_SPOT_NODE_SOCKET_SUB, ZLINK_LINGER, &linger,
              sizeof (linger))
            == 0);
    assert (zlink_spot_node_setsockopt (
              node_, ZLINK_SPOT_NODE_SOCKET_SUB, ZLINK_XPUB_NODROP, &nodrop,
              sizeof (nodrop))
            == 0);
}

void set_pub_send_timeout (void *node_, int sndtimeo_ms_)
{
    assert (zlink_spot_node_setsockopt (
              node_, ZLINK_SPOT_NODE_SOCKET_PUB, ZLINK_SNDTIMEO, &sndtimeo_ms_,
              sizeof (sndtimeo_ms_))
            == 0);
}

bool recv_one (void *spot_sub_)
{
    zlink_msg_t *parts = NULL;
    size_t count = 0;
    const int rc =
      zlink_spot_sub_recv (spot_sub_, &parts, &count, 0, NULL, NULL);
    if (rc != 0)
        return false;

    if (parts)
        zlink_multipart_close (parts, count);
    return true;
}

bool wait_until_ready (void *spot_pub_, void *spot_sub_)
{
    const char probe = 'R';
    const auto deadline = std::chrono::steady_clock::now ()
                          + std::chrono::milliseconds (kReadyTimeoutMs);
    while (std::chrono::steady_clock::now () < deadline) {
        if (zlink_spot_pub_publish_bytes (spot_pub_, kTopic, &probe, 1, 0) == 0) {
            zlink_msg_t *parts = NULL;
            size_t count = 0;
            const int recv_rc = zlink_spot_sub_recv (
              spot_sub_, &parts, &count, ZLINK_DONTWAIT, NULL, NULL);
            if (recv_rc == 0) {
                if (parts)
                    zlink_multipart_close (parts, count);
                return true;
            }
            if (parts)
                zlink_multipart_close (parts, count);
            if (zlink_errno () != EAGAIN)
                return false;
        } else if (zlink_errno () != EAGAIN) {
            return false;
        }
        sleep_ms (1);
    }
    return false;
}

int fill_until_eagain (void *spot_pub_)
{
    char payload[kMsgSize];
    std::memset (payload, 'f', sizeof (payload));

    int sent = 0;
    int eagain_streak = 0;
    for (int i = 0; i < kMaxFillMsgs; ++i) {
        if (zlink_spot_pub_publish_bytes (spot_pub_, kTopic, payload,
                                          sizeof (payload), 0)
            == 0) {
            ++sent;
            eagain_streak = 0;
            continue;
        }
        if (zlink_errno () == EAGAIN) {
            ++eagain_streak;
            if (eagain_streak >= kBackpressureStreak && sent > 0)
                return sent;
            continue;
        }
        return -1;
    }
    return -1;
}

void assert_backpressured_now (void *spot_pub_)
{
    char payload[kMsgSize];
    std::memset (payload, 'p', sizeof (payload));
    for (int i = 0; i < kBackpressureStreak; ++i) {
        assert (zlink_spot_pub_publish_bytes (
                  spot_pub_, kTopic, payload, sizeof (payload), 0)
                == -1);
        assert (zlink_errno () == EAGAIN);
    }
}

void setup_fixture (spot_fixture_t *fixture_, const char *endpoint_)
{
    fixture_->ctx = zlink_ctx_new ();
    assert (fixture_->ctx != NULL);

    fixture_->pub_node = zlink_spot_node_new (fixture_->ctx);
    assert (fixture_->pub_node != NULL);
    fixture_->sub_node = zlink_spot_node_new (fixture_->ctx);
    assert (fixture_->sub_node != NULL);

    configure_pub_node (fixture_->pub_node, 0);
    configure_sub_node (fixture_->sub_node);

    assert (zlink_spot_node_bind (fixture_->pub_node, endpoint_) == 0);
    assert (zlink_spot_node_connect_peer_pub (fixture_->sub_node, endpoint_) == 0);

    fixture_->spot_pub = zlink_spot_pub_new (fixture_->pub_node);
    assert (fixture_->spot_pub != NULL);
    fixture_->spot_sub = zlink_spot_sub_new (fixture_->sub_node);
    assert (fixture_->spot_sub != NULL);
    assert (zlink_spot_sub_subscribe (fixture_->spot_sub, kTopic) == 0);

    assert (wait_until_ready (fixture_->spot_pub, fixture_->spot_sub));
}

void test_spot_blocking_send_wakes_after_receiver_drains ()
{
    spot_fixture_t fixture;
    setup_fixture (&fixture, kEndpointWake);

    const int filled = fill_until_eagain (fixture.spot_pub);
    assert (filled > 0);
    assert_backpressured_now (fixture.spot_pub);
    sleep_ms (kProbeWaitMs);
    assert_backpressured_now (fixture.spot_pub);

    set_pub_send_timeout (fixture.pub_node, kSendTimeoutMs);
    char payload[kMsgSize];
    std::memset (payload, 'a', sizeof (payload));

    std::future<int> send_future = std::async (std::launch::async, [&] () {
        return zlink_spot_pub_publish_bytes (
          fixture.spot_pub, kTopic, payload, sizeof (payload), 0);
    });

    const std::future_status state_before_drain =
      send_future.wait_for (std::chrono::milliseconds (kProbeWaitMs));

    for (int i = 0; i < kSocketHwm * 8; ++i) {
        if (send_future.wait_for (std::chrono::milliseconds (0))
            == std::future_status::ready)
            break;
        if (!recv_one (fixture.spot_sub))
            break;
    }

    const std::future_status state_after_drain =
      send_future.wait_for (std::chrono::milliseconds (kSendTimeoutMs));
    if (state_after_drain != std::future_status::ready)
        send_future.wait ();
    const int send_rc = send_future.get ();

    assert (state_before_drain == std::future_status::timeout);
    assert (state_after_drain == std::future_status::ready);
    assert (send_rc == 0);

    cleanup_fixture (&fixture);
}

void test_spot_blocking_send_times_out_without_receiver_reads ()
{
    spot_fixture_t fixture;
    setup_fixture (&fixture, kEndpointTimeout);

    const int filled = fill_until_eagain (fixture.spot_pub);
    assert (filled > 0);
    assert_backpressured_now (fixture.spot_pub);
    sleep_ms (kProbeWaitMs);
    assert_backpressured_now (fixture.spot_pub);

    set_pub_send_timeout (fixture.pub_node, kTimeoutSendMs);
    char payload[kMsgSize];
    std::memset (payload, 'b', sizeof (payload));

    void *stopwatch = zlink_stopwatch_start ();
    const int send_rc =
      zlink_spot_pub_publish_bytes (fixture.spot_pub, kTopic, payload,
                                    sizeof (payload), 0);
    const unsigned int elapsed_ms = zlink_stopwatch_stop (stopwatch) / 1000;

    assert (send_rc == -1);
    assert (zlink_errno () == EAGAIN);
    assert (elapsed_ms >= static_cast<unsigned int> (kTimeoutSendMs - 120));

    cleanup_fixture (&fixture);
}
} // namespace

int main ()
{
    test_spot_blocking_send_wakes_after_receiver_drains ();
    test_spot_blocking_send_times_out_without_receiver_reads ();
    return 0;
}
