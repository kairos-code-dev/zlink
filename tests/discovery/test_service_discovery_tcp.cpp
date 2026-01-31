/* SPDX-License-Identifier: MPL-2.0 */

#include "../testutil_unity.hpp"
#include "../testutil.hpp"

#include <errno.h>
#include <inttypes.h>
#include <string.h>
#include <stdlib.h>
#include <vector>

static bool test_debug_enabled ()
{
    return getenv ("ZLINK_TEST_DEBUG") != NULL;
}

static void dump_peers (void *socket_, const char *label_)
{
    if (!test_debug_enabled () || !socket_ || !label_)
        return;

    size_t count = 0;
    if (zlink_socket_peers (socket_, NULL, &count) != 0) {
        fprintf (stderr, "[%s] peers: <error>\n", label_);
        return;
    }
    std::vector<zlink_peer_info_t> peers;
    peers.resize (count);
    if (count > 0 && zlink_socket_peers (socket_, &peers[0], &count) != 0) {
        fprintf (stderr, "[%s] peers: <error>\n", label_);
        return;
    }
    fprintf (stderr, "[%s] peers: %zu\n", label_, count);
    for (size_t i = 0; i < count; ++i) {
        const zlink_peer_info_t &info = peers[i];
        fprintf (stderr,
                 "[%s] peer[%zu] rid_size=%u remote=%s sent=%" PRIu64
                 " recv=%" PRIu64 " rid=0x",
                 label_, i, static_cast<unsigned> (info.routing_id.size),
                 info.remote_addr, static_cast<uint64_t> (info.msgs_sent),
                 static_cast<uint64_t> (info.msgs_received));
        for (uint8_t j = 0; j < info.routing_id.size; ++j)
            fprintf (stderr, "%02x",
                     static_cast<unsigned> (info.routing_id.data[j]));
        fprintf (stderr, "\n");
    }
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

static void setup_provider (void *ctx,
                            void **provider_out,
                            const char *bind_ep,
                            const char *registry_router,
                            const char *service_name)
{
    void *provider = zlink_provider_new (ctx);
    TEST_ASSERT_NOT_NULL (provider);
    TEST_ASSERT_SUCCESS_ERRNO (zlink_provider_bind (provider, bind_ep));
    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_provider_connect_registry (provider, registry_router));
    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_provider_register (provider, service_name, bind_ep, 1));
    *provider_out = provider;
}

static bool wait_for_provider (void *discovery_,
                               const char *service_name_,
                               int timeout_ms_)
{
    const int sleep_ms = 25;
    const int max_attempts = timeout_ms_ / sleep_ms;
    zlink_provider_info_t providers[4];
    for (int i = 0; i < max_attempts; ++i) {
        size_t count = 4;
        if (zlink_discovery_get_providers (discovery_, service_name_,
                                           providers, &count)
            == 0
            && count > 0)
            return true;
        msleep (sleep_ms);
    }
    return false;
}

static void dump_providers (void *discovery_, const char *service_name_)
{
    if (!test_debug_enabled ())
        return;
    zlink_provider_info_t providers[4];
    size_t count = 4;
    if (zlink_discovery_get_providers (discovery_, service_name_, providers,
                                       &count)
        != 0) {
        fprintf (stderr, "[providers] <error>\n");
        return;
    }
    fprintf (stderr, "[providers] count=%zu\n", count);
    for (size_t i = 0; i < count; ++i) {
        const zlink_provider_info_t &info = providers[i];
        fprintf (stderr, "[providers] %s endpoint=%s rid_size=%u rid=0x",
                 info.service_name, info.endpoint,
                 static_cast<unsigned> (info.routing_id.size));
        for (uint8_t j = 0; j < info.routing_id.size; ++j)
            fprintf (stderr, "%02x",
                     static_cast<unsigned> (info.routing_id.data[j]));
        fprintf (stderr, "\n");
    }
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

static int recv_gateway_with_timeout (void *gateway_,
                                      zlink_msg_t **parts_out_,
                                      size_t *count_out_,
                                      char *service_out_,
                                      uint64_t *request_id_out_,
                                      int timeout_ms_)
{
    const int sleep_ms = 5;
    int elapsed = 0;
    while (elapsed <= timeout_ms_) {
        if (zlink_gateway_recv (gateway_, parts_out_, count_out_,
                                ZLINK_DONTWAIT, service_out_,
                                request_id_out_)
            == 0)
            return 0;
        if (errno != EAGAIN)
            return -1;
        msleep (sleep_ms);
        elapsed += sleep_ms;
    }
    errno = EAGAIN;
    return -1;
}

static void test_gateway_send_recv_tcp ()
{
    void *ctx = zlink_ctx_new ();
    TEST_ASSERT_NOT_NULL (ctx);

    char reg_pub[64];
    char reg_router[64];
    char provider_ep[64];
    snprintf (reg_pub, sizeof (reg_pub), "tcp://127.0.0.1:%d",
              test_port (5600));
    snprintf (reg_router, sizeof (reg_router), "tcp://127.0.0.1:%d",
              test_port (5601));
    snprintf (provider_ep, sizeof (provider_ep), "tcp://127.0.0.1:%d",
              test_port (5602));

    void *registry = NULL;
    setup_registry (ctx, &registry, reg_pub, reg_router);
    void *discovery = zlink_discovery_new (ctx);
    TEST_ASSERT_NOT_NULL (discovery);
    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_discovery_connect_registry (discovery, reg_pub));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_discovery_subscribe (discovery, "svc"));
    void *provider = NULL;
    setup_provider (ctx, &provider, provider_ep, reg_router, "svc");
    TEST_ASSERT_TRUE (wait_for_provider (discovery, "svc", 2000));
    dump_providers (discovery, "svc");

    void *gateway = zlink_gateway_new (ctx, discovery);
    TEST_ASSERT_NOT_NULL (gateway);

    dump_peers (gateway, "gateway-before-send");

    zlink_msg_t req;
    zlink_msg_init_size (&req, 5);
    memcpy (zlink_msg_data (&req), "hello", 5);

    uint64_t request_id = 0;
    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_gateway_send (gateway, "svc", &req, 1, 0, &request_id));
    TEST_ASSERT_TRUE (request_id != 0);

    zlink_msg_t req2;
    zlink_msg_init_size (&req2, 5);
    memcpy (zlink_msg_data (&req2), "hello", 5);
    uint64_t request_id2 = 0;
    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_gateway_send (gateway, "svc", &req2, 1, 0, &request_id2));
    TEST_ASSERT_TRUE (request_id2 != 0);

    void *router = zlink_provider_router (provider);
    TEST_ASSERT_NOT_NULL (router);
    const int probe = 1;
    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_setsockopt (router, ZLINK_PROBE_ROUTER, &probe, sizeof (probe)));

    dump_peers (router, "provider-after-send");
    if (test_debug_enabled ()) {
        for (int i = 0; i < 10; ++i) {
            msleep (100);
            dump_peers (router, "provider-wait");
        }
    }

    zlink_msg_t rid;
    zlink_msg_t reqid;
    zlink_msg_t payload;
    zlink_msg_init (&rid);
    zlink_msg_init (&reqid);
    zlink_msg_init (&payload);

    TEST_ASSERT_SUCCESS_ERRNO (recv_msg_with_timeout (&rid, router, 2000));
    TEST_ASSERT_SUCCESS_ERRNO (recv_msg_with_timeout (&reqid, router, 2000));
    TEST_ASSERT_SUCCESS_ERRNO (recv_msg_with_timeout (&payload, router, 2000));

    TEST_ASSERT_EQUAL_INT (5, (int) zlink_msg_size (&payload));
    TEST_ASSERT_EQUAL_MEMORY ("hello", zlink_msg_data (&payload), 5);

    zlink_msg_t reply;
    zlink_msg_init_size (&reply, 5);
    memcpy (zlink_msg_data (&reply), "world", 5);

    TEST_ASSERT_SUCCESS_ERRNO (zlink_msg_send (&rid, router, ZLINK_SNDMORE));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_msg_send (&reqid, router, ZLINK_SNDMORE));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_msg_send (&reply, router, 0));

    TEST_ASSERT_SUCCESS_ERRNO (zlink_msg_close (&rid));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_msg_close (&reqid));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_msg_close (&payload));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_msg_close (&reply));

    zlink_msg_t *reply_parts = NULL;
    size_t reply_count = 0;
    char service_name[256];
    uint64_t recv_id = 0;
    TEST_ASSERT_SUCCESS_ERRNO (
      recv_gateway_with_timeout (gateway, &reply_parts, &reply_count,
                                 service_name, &recv_id, 2000));
    TEST_ASSERT_EQUAL_UINT64 (request_id, recv_id);
    TEST_ASSERT_EQUAL_STRING ("svc", service_name);
    TEST_ASSERT_EQUAL_INT (1, (int) reply_count);
    TEST_ASSERT_EQUAL_INT (5, (int) zlink_msg_size (&reply_parts[0]));
    TEST_ASSERT_EQUAL_MEMORY ("world", zlink_msg_data (&reply_parts[0]), 5);
    zlink_msgv_close (reply_parts, reply_count);

    TEST_ASSERT_SUCCESS_ERRNO (zlink_gateway_destroy (&gateway));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_provider_destroy (&provider));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_discovery_destroy (&discovery));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_registry_destroy (&registry));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_ctx_term (ctx));
}

int main (int, char **)
{
    setup_test_environment ();

    UNITY_BEGIN ();
    RUN_TEST (test_gateway_send_recv_tcp);
    return UNITY_END ();
}
