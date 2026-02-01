/* SPDX-License-Identifier: MPL-2.0 */

#include "../testutil_unity.hpp"
#include "../testutil.hpp"

#include <string.h>

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

static void setup_provider_tcp (void *ctx,
                                void **provider_out,
                                const char *bind_ep,
                                const char *registry_router,
                                const char *service_name,
                                char *advertise_out,
                                size_t advertise_len)
{
    void *provider = zlink_provider_new (ctx);
    TEST_ASSERT_NOT_NULL (provider);
    TEST_ASSERT_SUCCESS_ERRNO (zlink_provider_bind (provider, bind_ep));

    void *router = zlink_provider_router (provider);
    TEST_ASSERT_NOT_NULL (router);
    if (getenv ("ZLINK_TEST_DEBUG")) {
        int peer_count = zlink_socket_peer_count (router);
        fprintf (stderr, "[provider] peer_count=%d\n", peer_count);
        for (int i = 0; i < peer_count; ++i) {
            zlink_routing_id_t rid_out;
            if (zlink_socket_peer_routing_id (router, i, &rid_out) == 0) {
                fprintf (stderr, "[provider] peer[%d] rid_size=%u data=0x",
                         i, rid_out.size);
                for (uint8_t j = 0; j < rid_out.size; ++j)
                    fprintf (stderr, "%02x", rid_out.data[j]);
                fprintf (stderr, "\n");
            } else {
                fprintf (stderr, "[provider] peer[%d] rid=<err errno=%d>\n", i,
                         errno);
            }
        }
    }
    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_getsockopt (router, ZLINK_LAST_ENDPOINT, advertise_out,
                        &advertise_len));

    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_provider_connect_registry (provider, registry_router));
    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_provider_register (provider, service_name, advertise_out, 1));
    *provider_out = provider;
}

static int recv_msg_with_timeout (zlink_msg_t *msg_,
                                  void *socket_,
                                  int timeout_ms_)
{
    const int sleep_ms = 5;
    int elapsed = 0;
    while (elapsed <= timeout_ms_) {
        if (zlink_msg_recv (msg_, socket_, ZLINK_DONTWAIT) == 0)
            return 0;
        if (errno != EAGAIN)
            return -1;
        msleep (sleep_ms);
        elapsed += sleep_ms;
    }
    errno = EAGAIN;
    return -1;
}

static void drain_monitor (void *monitor_, const char *label_)
{
    if (!monitor_ || !getenv ("ZLINK_TEST_DEBUG"))
        return;
    zlink_monitor_event_t event;
    while (zlink_monitor_recv (monitor_, &event, ZLINK_DONTWAIT) == 0) {
        fprintf (stderr,
                 "[%s] event=%llu value=%llu local=%s remote=%s\n",
                 label_ ? label_ : "monitor",
                 static_cast<unsigned long long> (event.event),
                 static_cast<unsigned long long> (event.value),
                 event.local_addr,
                 event.remote_addr);
    }
}

static bool wait_for_peer_count (void *socket_, int expected_, int timeout_ms_)
{
    const int sleep_ms = 25;
    int elapsed = 0;
    while (elapsed <= timeout_ms_) {
        const int count = zlink_socket_peer_count (socket_);
        if (count >= expected_)
            return true;
        msleep (sleep_ms);
        elapsed += sleep_ms;
    }
    return false;
}

static void test_discovery_router_send_tcp ()
{
    void *ctx = zlink_ctx_new ();
    TEST_ASSERT_NOT_NULL (ctx);

    void *registry = NULL;
    setup_registry (ctx, &registry, "inproc://reg-pub-dr",
                    "inproc://reg-router-dr");

    void *discovery = zlink_discovery_new (ctx);
    TEST_ASSERT_NOT_NULL (discovery);
    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_discovery_connect_registry (discovery, "inproc://reg-pub-dr"));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_discovery_subscribe (discovery, "svc"));

    void *provider = NULL;
    char advertise_ep[256] = {0};
    setup_provider_tcp (ctx, &provider, "tcp://127.0.0.1:*",
                        "inproc://reg-router-dr", "svc", advertise_ep,
                        sizeof (advertise_ep));

    msleep (200);

    zlink_provider_info_t providers[4];
    size_t count = 4;
    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_discovery_get_providers (discovery, "svc", providers, &count));
    TEST_ASSERT_EQUAL_INT (1, (int) count);
    TEST_ASSERT_EQUAL_STRING (advertise_ep, providers[0].endpoint);
    if (getenv ("ZLINK_TEST_DEBUG")) {
        fprintf (stderr, "[provider] advertised=%s\n", advertise_ep);
        fprintf (stderr, "[discovery] endpoint=%s\n", providers[0].endpoint);
        fprintf (stderr, "[discovery] rid_size=%u data=0x",
                 static_cast<unsigned> (providers[0].routing_id.size));
        for (uint8_t j = 0; j < providers[0].routing_id.size; ++j)
            fprintf (stderr, "%02x", providers[0].routing_id.data[j]);
        fprintf (stderr, "\n");
    }

    void *router = zlink_provider_router (provider);
    TEST_ASSERT_NOT_NULL (router);
    void *router_mon =
      zlink_socket_monitor_open (router, ZLINK_EVENT_ACCEPTED
                                           | ZLINK_EVENT_CONNECTED
                                           | ZLINK_EVENT_CONNECT_DELAYED
                                           | ZLINK_EVENT_CONNECT_RETRIED
                                           | ZLINK_EVENT_DISCONNECTED
                                           | ZLINK_EVENT_HANDSHAKE_FAILED_NO_DETAIL
                                           | ZLINK_EVENT_HANDSHAKE_FAILED_PROTOCOL
                                           | ZLINK_EVENT_HANDSHAKE_FAILED_AUTH);

    void *client = zlink_socket (ctx, ZLINK_ROUTER);
    TEST_ASSERT_NOT_NULL (client);
    // Ensure client routing_id is distinct from provider's routing_id (auto 0x...01).
    unsigned char client_rid[5] = {0, 0, 0, 0, 2};
    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_setsockopt (client, ZLINK_ROUTING_ID, client_rid,
                        sizeof (client_rid)));
    void *client_mon =
      zlink_socket_monitor_open (client, ZLINK_EVENT_CONNECTED
                                            | ZLINK_EVENT_CONNECT_DELAYED
                                            | ZLINK_EVENT_CONNECT_RETRIED
                                            | ZLINK_EVENT_DISCONNECTED
                                            | ZLINK_EVENT_HANDSHAKE_FAILED_NO_DETAIL
                                            | ZLINK_EVENT_HANDSHAKE_FAILED_PROTOCOL
                                            | ZLINK_EVENT_HANDSHAKE_FAILED_AUTH);
    TEST_ASSERT_SUCCESS_ERRNO (zlink_connect (client, providers[0].endpoint));

    msleep (200);
    drain_monitor (client_mon, "client");
    drain_monitor (router_mon, "provider");
    wait_for_peer_count (client, 1, 2000);

    if (getenv ("ZLINK_TEST_DEBUG")) {
        int peer_count = zlink_socket_peer_count (client);
        fprintf (stderr, "[client] peer_count=%d\n", peer_count);
        for (int i = 0; i < peer_count; ++i) {
            zlink_routing_id_t rid_out;
            if (zlink_socket_peer_routing_id (client, i, &rid_out) == 0) {
                fprintf (stderr, "[client] peer[%d] rid_size=%u data=0x",
                         i, rid_out.size);
                for (uint8_t j = 0; j < rid_out.size; ++j)
                    fprintf (stderr, "%02x", rid_out.data[j]);
                fprintf (stderr, "\n");
            } else {
                fprintf (stderr, "[client] peer[%d] rid=<err errno=%d>\n", i,
                         errno);
            }
        }
    }

    zlink_msg_t rid;
    zlink_msg_t payload;
    zlink_msg_init_size (&rid, providers[0].routing_id.size);
    memcpy (zlink_msg_data (&rid), providers[0].routing_id.data,
            providers[0].routing_id.size);
    TEST_ASSERT_SUCCESS_ERRNO (zlink_msg_send (&rid, client, ZLINK_SNDMORE));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_msg_close (&rid));

    zlink_msg_init_size (&payload, 5);
    memcpy (zlink_msg_data (&payload), "hello", 5);
    TEST_ASSERT_SUCCESS_ERRNO (zlink_msg_send (&payload, client, 0));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_msg_close (&payload));

    wait_for_peer_count (router, 1, 2000);

    zlink_msg_t recv_rid;
    zlink_msg_t recv_payload;
    zlink_msg_init (&recv_rid);
    zlink_msg_init (&recv_payload);
    TEST_ASSERT_SUCCESS_ERRNO (
      recv_msg_with_timeout (&recv_rid, router, 2000));
    TEST_ASSERT_SUCCESS_ERRNO (
      recv_msg_with_timeout (&recv_payload, router, 2000));
    TEST_ASSERT_EQUAL_INT (5, (int) zlink_msg_size (&recv_payload));
    TEST_ASSERT_EQUAL_MEMORY ("hello", zlink_msg_data (&recv_payload), 5);
    TEST_ASSERT_SUCCESS_ERRNO (zlink_msg_close (&recv_rid));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_msg_close (&recv_payload));

    if (router_mon)
        zlink_close (router_mon);
    if (client_mon)
        zlink_close (client_mon);
    TEST_ASSERT_SUCCESS_ERRNO (zlink_close (client));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_provider_destroy (&provider));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_discovery_destroy (&discovery));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_registry_destroy (&registry));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_ctx_term (ctx));
}

int main (void)
{
    UNITY_BEGIN ();
    RUN_TEST (test_discovery_router_send_tcp);
    return UNITY_END ();
}
