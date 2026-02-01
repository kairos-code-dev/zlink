/* SPDX-License-Identifier: MPL-2.0 */

#include "testutil.hpp"
#include "testutil_unity.hpp"

#include "../src/discovery/protocol.hpp"
#ifndef ZLINK_BUILD_TESTS
#define ZLINK_BUILD_TESTS 1
#endif
#include "../src/discovery/gateway.hpp"
#include "../src/core/msg.hpp"

#include <cinttypes>
#include <string.h>

SETUP_TEARDOWN_TESTCONTEXT

static void set_routing_id_5 (void *socket_, uint32_t id_)
{
    unsigned char rid[5];
    rid[0] = 0;
    rid[1] = static_cast<unsigned char> ((id_ >> 24) & 0xff);
    rid[2] = static_cast<unsigned char> ((id_ >> 16) & 0xff);
    rid[3] = static_cast<unsigned char> ((id_ >> 8) & 0xff);
    rid[4] = static_cast<unsigned char> (id_ & 0xff);
    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_setsockopt (socket_, ZLINK_ROUTING_ID, rid, sizeof (rid)));
}

static void setup_registry (void *ctx,
                            void **registry_out,
                            const char *pub_ep,
                            const char *router_ep)
{
    void *registry = zlink_registry_new (ctx);
    TEST_ASSERT_NOT_NULL (registry);
    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_registry_set_endpoints (registry, pub_ep, router_ep));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_registry_start (registry));
    *registry_out = registry;
}

static int recv_msg_with_timeout (zlink_msg_t *msg_,
                                  void *socket_,
                                  int timeout_ms_)
{
    zlink_pollitem_t item;
    item.socket = socket_;
    item.fd = 0;
    item.events = ZLINK_POLLIN;
    item.revents = 0;

    const int slice_ms = 50;
    int elapsed = 0;
    while (elapsed <= timeout_ms_) {
        const int rc = zlink_poll (&item, 1, slice_ms);
        if (rc < 0)
            return -1;
        if (rc > 0 && (item.revents & ZLINK_POLLIN)) {
            if (zlink_msg_recv (msg_, socket_, 0) == 0)
                return 0;
            return -1;
        }
        elapsed += slice_ms;
    }
    errno = EAGAIN;
    return -1;
}

static int send_msg_with_timeout (zlink_msg_t *msg_,
                                  void *socket_,
                                  int flags_,
                                  int timeout_ms_)
{
    const int sleep_ms = 5;
    int elapsed = 0;
    while (elapsed <= timeout_ms_) {
        if (zlink_msg_send (msg_, socket_, flags_ | ZLINK_DONTWAIT) == 0)
            return 0;
        if (errno != EAGAIN)
            return -1;
        msleep (sleep_ms);
        elapsed += sleep_ms;
    }
    errno = EAGAIN;
    return -1;
}


static int send_frame_with_timeout (void *socket_,
                                    const void *data_,
                                    size_t size_,
                                    int flags_,
                                    int timeout_ms_)
{
    zlink_msg_t msg;
    if (zlink_msg_init_size (&msg, size_) != 0)
        return -1;
    if (size_ > 0 && data_)
        memcpy (zlink_msg_data (&msg), data_, size_);
    const int rc = send_msg_with_timeout (&msg, socket_, flags_, timeout_ms_);
    if (rc != 0)
        zlink_msg_close (&msg);
    return rc;
}

static int send_frame_blocking (void *socket_,
                                const void *data_,
                                size_t size_,
                                int flags_)
{
    return zlink_send (socket_, data_, size_, flags_);
}

static int send_u16_blocking (void *socket_, uint16_t value_, int flags_)
{
    return send_frame_blocking (socket_, &value_, sizeof (value_), flags_);
}

static int send_u32_blocking (void *socket_, uint32_t value_, int flags_)
{
    return send_frame_blocking (socket_, &value_, sizeof (value_), flags_);
}

static int send_string_blocking (void *socket_, const char *value_, int flags_)
{
    const size_t size = value_ ? strlen (value_) : 0;
    return send_frame_blocking (socket_, value_, size, flags_);
}

static int send_u16_with_timeout (void *socket_,
                                  uint16_t value_,
                                  int flags_,
                                  int timeout_ms_)
{
    return send_frame_with_timeout (socket_, &value_, sizeof (value_), flags_,
                                    timeout_ms_);
}

static int send_u32_with_timeout (void *socket_,
                                  uint32_t value_,
                                  int flags_,
                                  int timeout_ms_)
{
    return send_frame_with_timeout (socket_, &value_, sizeof (value_), flags_,
                                    timeout_ms_);
}

static int send_string_with_timeout (void *socket_,
                                     const char *value_,
                                     int flags_,
                                     int timeout_ms_)
{
    const size_t size = value_ ? strlen (value_) : 0;
    return send_frame_with_timeout (socket_, value_, size, flags_, timeout_ms_);
}

static void step_log (const char *msg_)
{
    if (getenv ("ZLINK_TEST_DEBUG")) {
        fprintf (stderr, "[fixed] %s\n", msg_ ? msg_ : "");
        fflush (stderr);
    }
}

static void print_errno (const char *label_)
{
    if (getenv ("ZLINK_TEST_DEBUG")) {
        fprintf (stderr, "[fixed] %s errno=%d (%s)\n", label_, errno,
                 strerror (errno));
        fflush (stderr);
    }
}

static int recv_register_ack (void *dealer_, uint32_t *status_out_)
{
    using namespace zlink::discovery_protocol;
    unsigned char buf[512];

    int rc = zlink_recv (dealer_, buf, sizeof (buf), 0);
    if (rc < 0) {
        print_errno ("ack recv frame1");
        return -1;
    }
    if (rc != static_cast<int> (sizeof (uint16_t))) {
        errno = EPROTO;
        return -1;
    }
    uint16_t msg_id = 0;
    memcpy (&msg_id, buf, sizeof (uint16_t));
    if (msg_id != msg_register_ack) {
        errno = EPROTO;
        return -1;
    }

    rc = zlink_recv (dealer_, buf, sizeof (buf), 0);
    if (rc < 0) {
        print_errno ("ack recv frame2");
        return -1;
    }
    uint8_t status = 0xFF;
    if (rc == static_cast<int> (sizeof (uint8_t)))
        memcpy (&status, buf, sizeof (uint8_t));
    if (status_out_)
        *status_out_ = static_cast<uint32_t> (status);

    rc = zlink_recv (dealer_, buf, sizeof (buf), 0);
    if (rc < 0) {
        print_errno ("ack recv frame3");
        return -1;
    }

    rc = zlink_recv (dealer_, buf, sizeof (buf), 0);
    if (rc < 0) {
        print_errno ("ack recv frame4");
        return -1;
    }

    if (status != 0) {
        errno = EINVAL;
        return -1;
    }
    return 0;
}

void test_router_fixed_endpoint_send ()
{
    void *ctx = get_test_context ();
    TEST_ASSERT_NOT_NULL (ctx);
    const char *service_name = "svc";

    void *registry = NULL;
    step_log ("setup registry");
    setup_registry (ctx, &registry, "inproc://reg-pub-fixed",
                    "inproc://reg-router-fixed");
    // inproc requires bind-before-connect; give registry worker time to bind.
    msleep (100);

    void *discovery = zlink_discovery_new (ctx);
    TEST_ASSERT_NOT_NULL (discovery);
    step_log ("connect discovery");
    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_discovery_connect_registry (discovery, "inproc://reg-pub-fixed"));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_discovery_subscribe (discovery, "svc"));

    void *provider_router = test_context_socket (ZLINK_ROUTER);
    TEST_ASSERT_NOT_NULL (provider_router);
    step_log ("bind provider router");
    // Provider ROUTER that will receive the client message.
    const char provider_rid[] = "PROV1";
    int probe = 1;
    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_setsockopt (provider_router, ZLINK_PROBE_ROUTER, &probe,
                        sizeof (probe)));
    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_setsockopt (provider_router, ZLINK_ROUTING_ID, provider_rid,
                        sizeof (provider_rid) - 1));
    // Bind on a fresh loopback endpoint (we'll also advertise this).
    char advertise_ep[256] = {0};
    bind_loopback_ipv4 (provider_router, advertise_ep, sizeof (advertise_ep));

    void *provider_dealer = test_context_socket (ZLINK_DEALER);
    TEST_ASSERT_NOT_NULL (provider_dealer);
    step_log ("connect provider dealer");
    // Dealer connects to registry to register the service+endpoint.
    int dealer_probe = 1;
    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_setsockopt (provider_dealer, ZLINK_PROBE_ROUTER, &dealer_probe,
                        sizeof (dealer_probe)));
    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_setsockopt (provider_dealer, ZLINK_ROUTING_ID, provider_rid,
                        sizeof (provider_rid) - 1));
    int dealer_timeout = 2000;
    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_setsockopt (provider_dealer, ZLINK_RCVTIMEO, &dealer_timeout,
                        sizeof (dealer_timeout)));
    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_setsockopt (provider_dealer, ZLINK_SNDTIMEO, &dealer_timeout,
                        sizeof (dealer_timeout)));
    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_connect (provider_dealer, "inproc://reg-router-fixed"));

    // send register frames
    step_log ("register service");
    TEST_ASSERT_SUCCESS_ERRNO (send_u16_blocking (
      provider_dealer, zlink::discovery_protocol::msg_register,
      ZLINK_SNDMORE));
    TEST_ASSERT_SUCCESS_ERRNO (send_string_blocking (
      provider_dealer, "svc", ZLINK_SNDMORE));
    TEST_ASSERT_SUCCESS_ERRNO (send_string_blocking (
      provider_dealer, advertise_ep, ZLINK_SNDMORE));
    TEST_ASSERT_SUCCESS_ERRNO (send_u32_blocking (provider_dealer, 1, 0));
    uint32_t status = 0;
    zlink_pollitem_t ack_item;
    ack_item.socket = provider_dealer;
    ack_item.fd = 0;
    ack_item.events = ZLINK_POLLIN;
    ack_item.revents = 0;
    const int poll_rc = zlink_poll (&ack_item, 1, 2000);
    if (getenv ("ZLINK_TEST_DEBUG")) {
        fprintf (stderr, "[fixed] ack poll rc=%d revents=%d\n", poll_rc,
                 ack_item.revents);
    }
    TEST_ASSERT_SUCCESS_ERRNO (recv_register_ack (provider_dealer, &status));

    msleep (200);

    zlink_provider_info_t provider_info;
    memset (&provider_info, 0, sizeof (provider_info));
    size_t count = 1;
    step_log ("get providers");
    // Discovery should now report the provider we registered.
    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_discovery_get_providers (discovery, "svc", &provider_info, &count));
    TEST_ASSERT_EQUAL_INT (1, (int) count);
    TEST_ASSERT_EQUAL_STRING (advertise_ep, provider_info.endpoint);
    TEST_ASSERT_EQUAL_INT (sizeof (provider_rid) - 1,
                           (int) provider_info.routing_id.size);
    TEST_ASSERT_EQUAL_MEMORY (provider_rid, provider_info.routing_id.data,
                              sizeof (provider_rid) - 1);
    if (getenv ("ZLINK_TEST_DEBUG")) {
        fprintf (stderr,
                 "[fixed] provider bound ep=%s rid=%s rid_size=%zu rid_hex=0x",
                 advertise_ep, provider_rid, sizeof (provider_rid) - 1);
        for (size_t i = 0; i < sizeof (provider_rid) - 1; ++i)
            fprintf (stderr, "%02x",
                     static_cast<unsigned char> (provider_rid[i]));
        fprintf (stderr, "\n");
        fprintf (stderr,
                 "[fixed] discovery ep=%s rid=",
                 provider_info.endpoint);
        fwrite (provider_info.routing_id.data, 1,
                provider_info.routing_id.size, stderr);
        fprintf (stderr, " rid_size=%u rid_hex=0x",
                 (unsigned) provider_info.routing_id.size);
        for (uint8_t i = 0; i < provider_info.routing_id.size; ++i)
            fprintf (stderr, "%02x",
                     static_cast<unsigned> (provider_info.routing_id.data[i]));
        fprintf (stderr, "\n");
        fflush (stderr);
    }

    step_log ("create gateway socket");
    zlink::ctx_t *ctx_impl = static_cast<zlink::ctx_t *> (ctx);
    zlink::gateway_t gateway (ctx_impl, NULL);
    zlink::socket_base_t *gateway_socket =
      gateway.get_router_socket (service_name);
    TEST_ASSERT_NOT_NULL (gateway_socket);

    step_log ("create client socket");
    void *client = test_context_socket (ZLINK_ROUTER);
    step_log ("connect client");
    // Client ROUTER connects directly to the advertised endpoint.
    TEST_ASSERT_SUCCESS_ERRNO (zlink_connect (client, provider_info.endpoint));

    int timeout_ms = 2000;
    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_setsockopt (client, ZLINK_SNDTIMEO, &timeout_ms,
                        sizeof (timeout_ms)));
    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_setsockopt (provider_router, ZLINK_RCVTIMEO, &timeout_ms,
                        sizeof (timeout_ms)));
    msleep (200);

    // Send [destination routing id][payload] via gateway router socket.
    zlink_msg_t rid;
    zlink_msg_t payload;
    step_log ("send payload");
    zlink_msg_init_size (&rid, provider_info.routing_id.size);
    memcpy (zlink_msg_data (&rid), provider_info.routing_id.data,
            provider_info.routing_id.size);
    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_msg_send (&rid, client, ZLINK_SNDMORE));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_msg_close (&rid));

    zlink_msg_init_size (&payload, 5);
    memcpy (zlink_msg_data (&payload), "hello", 5);
    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_msg_send (&payload, client, 0));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_msg_close (&payload));

    void *router = provider_router;
    TEST_ASSERT_NOT_NULL (router);

    // Provider receives [gateway routing id][payload].
    zlink_msg_t recv_rid;
    zlink_msg_t recv_payload;
    zlink_msg_init (&recv_rid);
    zlink_msg_init (&recv_payload);
    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_msg_recv (&recv_rid, router, 0));
    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_msg_recv (&recv_payload, router, 0));
    TEST_ASSERT_EQUAL_INT (5, (int) zlink_msg_size (&recv_payload));
    TEST_ASSERT_EQUAL_MEMORY ("hello", zlink_msg_data (&recv_payload), 5);
    TEST_ASSERT_SUCCESS_ERRNO (zlink_msg_close (&recv_rid));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_msg_close (&recv_payload));

    step_log ("cleanup");
    test_context_socket_close_zero_linger (client);
    gateway.destroy ();
    test_context_socket_close_zero_linger (provider_dealer);
    test_context_socket_close_zero_linger (provider_router);
    TEST_ASSERT_SUCCESS_ERRNO (zlink_discovery_destroy (&discovery));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_registry_destroy (&registry));
}

int main (void)
{
    UNITY_BEGIN ();
    RUN_TEST (test_router_fixed_endpoint_send);
    return UNITY_END ();
}
