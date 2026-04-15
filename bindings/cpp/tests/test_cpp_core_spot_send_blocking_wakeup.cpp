/* SPDX-License-Identifier: MPL-2.0 */

#include "test_helpers.hpp"

#include <chrono>
#include <cstring>

namespace
{
const int kSendTimeoutMs = 1000;
const int kRecvTimeoutMs = 200;
const int kReadyTimeoutMs = 2000;
const char *kTopic = "spot:smoke";

struct spot_fixture_t
{
    void *ctx;
    void *pub_node;
    void *sub_node;
    void *spot_pub;
    void *spot_sub;

    spot_fixture_t () :
        ctx (NULL),
        pub_node (NULL),
        sub_node (NULL),
        spot_pub (NULL),
        spot_sub (NULL)
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

void configure_pub (void *spot_pub_, int sndtimeo_ms_)
{
    const int linger = 0;
    assert (zlink_spot_pub_set_option (
              spot_pub_, ZLINK_SPOT_PUB_OPT_LINGER, &linger, sizeof (linger))
            == 0);
    assert (zlink_spot_pub_set_option (
              spot_pub_, ZLINK_SPOT_PUB_OPT_SNDTIMEO, &sndtimeo_ms_,
              sizeof (sndtimeo_ms_))
            == 0);
}

void configure_sub (void *spot_sub_)
{
    const int linger = 0;
    assert (zlink_spot_sub_set_option (
              spot_sub_, ZLINK_SPOT_SUB_OPT_RCVTIMEO, &kRecvTimeoutMs,
              sizeof (kRecvTimeoutMs))
            == 0);
    assert (zlink_spot_sub_set_option (
              spot_sub_, ZLINK_SPOT_SUB_OPT_LINGER, &linger, sizeof (linger))
            == 0);
}

bool wait_until_ready (void *spot_pub_, void *spot_sub_)
{
    const char probe = 'r';
    const auto started = std::chrono::steady_clock::now ();
    while (std::chrono::steady_clock::now () - started
           < std::chrono::milliseconds (kReadyTimeoutMs)) {
        if (zlink_spot_pub_publish_bytes (spot_pub_, kTopic, &probe, 1, 0)
            == 0) {
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

bool recv_expected_payload (void *spot_sub_,
                            const char *expected_,
                            size_t expected_size_)
{
    const auto started = std::chrono::steady_clock::now ();
    while (std::chrono::steady_clock::now () - started
           < std::chrono::milliseconds (kReadyTimeoutMs)) {
        zlink_msg_t *parts = NULL;
        size_t count = 0;
        char topic[128];
        size_t topic_len = sizeof (topic);
        const int rc = zlink_spot_sub_recv (
          spot_sub_, &parts, &count, ZLINK_DONTWAIT, topic, &topic_len);
        if (rc != 0) {
            if (zlink_errno () == EAGAIN) {
                sleep_ms (1);
                continue;
            }
            return false;
        }

        const bool match =
          count == 1 && topic_len == std::strlen (kTopic)
          && std::memcmp (topic, kTopic, topic_len) == 0
          && zlink_msg_size (&parts[0]) == expected_size_
          && std::memcmp (zlink_msg_data (&parts[0]), expected_, expected_size_)
               == 0;
        zlink_multipart_close (parts, count);
        if (match)
            return true;
    }
    return false;
}

void setup_fixture (spot_fixture_t *fixture_)
{
    fixture_->ctx = zlink_ctx_new ();
    assert (fixture_->ctx != NULL);

    fixture_->pub_node = zlink_spot_node_new (fixture_->ctx, NULL, NULL, NULL);
    assert (fixture_->pub_node != NULL);
    fixture_->sub_node = zlink_spot_node_new (fixture_->ctx, NULL, NULL, NULL);
    assert (fixture_->sub_node != NULL);

    fixture_->spot_pub = zlink_spot_pub_new (fixture_->pub_node);
    assert (fixture_->spot_pub != NULL);
    fixture_->spot_sub = zlink_spot_sub_new (fixture_->sub_node);
    assert (fixture_->spot_sub != NULL);

    configure_pub (fixture_->spot_pub, kSendTimeoutMs);
    configure_sub (fixture_->spot_sub);

    const std::string endpoint =
      unique_inproc ("inproc://cpp-", "spot-send-smoke");
    assert (zlink_spot_node_bind (fixture_->pub_node, endpoint.c_str ()) == 0);
    assert (zlink_spot_node_connect_peer (fixture_->sub_node, endpoint.c_str ())
            == 0);
    assert (zlink_spot_sub_subscribe (fixture_->spot_sub, kTopic) == 0);
    assert (wait_until_ready (fixture_->spot_pub, fixture_->spot_sub));
}

void test_spot_send_and_recv_smoke ()
{
    spot_fixture_t fixture;
    setup_fixture (&fixture);

    const char payload[] = "payload";
    assert (zlink_spot_pub_publish_bytes (
              fixture.spot_pub, kTopic, payload, sizeof (payload) - 1, 0)
            == 0);
    assert (recv_expected_payload (
      fixture.spot_sub, payload, sizeof (payload) - 1));

    cleanup_fixture (&fixture);
}
} // namespace

int main()
{
    test_spot_send_and_recv_smoke ();
    return 0;
}
