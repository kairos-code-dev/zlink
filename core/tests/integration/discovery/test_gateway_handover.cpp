/* SPDX-License-Identifier: MPL-2.0 */

#include "../../testutil.hpp"
#include "../../testutil_unity.hpp"

#include <atomic>
#include <string.h>

SETUP_TEARDOWN_TESTCONTEXT

namespace
{
struct gateway_probe_t
{
    gateway_probe_t () : requests (0)
    {
        memset (payload, 0, sizeof (payload));
    }

    std::atomic<int> requests;
    char payload[64];
};

struct gateway_server_t
{
    gateway_server_t () : discovery (NULL), gateway (NULL), probe (NULL) {}

    void *discovery;
    void *gateway;
    gateway_probe_t *probe;
};

gateway_probe_t *g_probe_a = NULL;
gateway_probe_t *g_probe_b = NULL;

bool read_gateway_snapshot (void *gateway_, zlink_monitor_snapshot_t *out_)
{
    if (!gateway_ || !out_)
        return false;

    void *monitor = zlink_gateway_monitor_open (
      gateway_,
      ZLINK_GATEWAY_SERVICE_READY | ZLINK_GATEWAY_SERVICE_LOST
        | ZLINK_GATEWAY_SEND_READY_CHANGED | ZLINK_GATEWAY_ROUTE_UP
        | ZLINK_GATEWAY_ROUTE_DOWN | ZLINK_GATEWAY_MONITOR_EVENT_ERROR,
      &zlink_service_monitor_ignore_handler, NULL);
    if (!monitor)
        return false;

    const int rc = zlink_monitor_snapshot (monitor, out_);
    zlink_service_monitor_close (&monitor);
    return rc == 0;
}

void step_log (const char *msg_)
{
    if (getenv ("ZLINK_TEST_DEBUG")) {
        fprintf (stderr, "[handover] %s\n", msg_ ? msg_ : "");
        fflush (stderr);
    }
}

void discard_gateway_message (const zlink_routing_id_t *,
                              zlink_msg_t *parts_,
                              size_t part_count_,
                          void *)
{
    for (size_t i = 0; i < part_count_; ++i)
        zlink_msg_close (&parts_[i]);
}

void *create_gateway_attached (void *ctx_,
                               void *discovery_,
                               const char *service_name_,
                               const char *routing_id_,
                               zlink_socket_msg_handler_fn handler_)
{
    void *gateway = zlink_gateway_new (ctx_, service_name_);
    if (!gateway)
        return NULL;
    if (routing_id_
        && zlink_gateway_set_routing_id (gateway, routing_id_,
                                         strlen (routing_id_))
             != 0) {
        const int err = errno;
        zlink_gateway_destroy (&gateway);
        errno = err;
        return NULL;
    }
    if (handler_ && zlink_recv_handler (gateway, handler_, NULL) != 0) {
        const int err = errno;
        zlink_gateway_destroy (&gateway);
        errno = err;
        return NULL;
    }
    const int linger = 0;
    if (zlink_gateway_set_option (gateway, ZLINK_GATEWAY_OPT_LINGER, &linger,
                                  sizeof (linger))
        != 0) {
        const int err = errno;
        zlink_gateway_destroy (&gateway);
        errno = err;
        return NULL;
    }
    if (zlink_gateway_attach_discovery (gateway, discovery_) != 0) {
        const int err = errno;
        zlink_gateway_destroy (&gateway);
        errno = err;
        return NULL;
    }
    return gateway;
}

void record_gateway_event (gateway_probe_t *probe_,
                           const zlink_routing_id_t *,
                           zlink_msg_t *parts_,
                           size_t part_count_,
                          void *)
{
    if (!probe_) {
        for (size_t i = 0; i < part_count_; ++i)
            zlink_msg_close (&parts_[i]);
        return;
    }

    if (part_count_ > 0) {
        const size_t size = zlink_msg_size (&parts_[0]);
        const size_t payload_copy =
          size < sizeof (probe_->payload) - 1 ? size : sizeof (probe_->payload) - 1;
        memcpy (probe_->payload, zlink_msg_data (&parts_[0]), payload_copy);
        probe_->payload[payload_copy] = '\0';
    }
    probe_->requests.fetch_add (1);
    for (size_t i = 0; i < part_count_; ++i)
        zlink_msg_close (&parts_[i]);
}

void gateway_handler_a (const zlink_routing_id_t *source_rid_,
                        zlink_msg_t *parts_,
                        size_t part_count_,
                          void *)
{
    record_gateway_event (g_probe_a, source_rid_, parts_, part_count_, NULL);
}

void gateway_handler_b (const zlink_routing_id_t *source_rid_,
                        zlink_msg_t *parts_,
                        size_t part_count_,
                          void *)
{
    record_gateway_event (g_probe_b, source_rid_, parts_, part_count_, NULL);
}

bool wait_for_calls (std::atomic<int> *counter_, int expected_, int timeout_ms_)
{
    const int step_ms = 10;
    const int attempts = timeout_ms_ / step_ms;
    for (int i = 0; i < attempts; ++i) {
        if (counter_->load () >= expected_)
            return true;
        msleep (step_ms);
    }
    return counter_->load () >= expected_;
}

void wait_gateway_ready (void *gateway_, int timeout_ms_)
{
    const int step_ms = 10;
    const int attempts = timeout_ms_ / step_ms;
    for (int i = 0; i < attempts; ++i) {
        zlink_monitor_snapshot_t snapshot;
        memset (&snapshot, 0, sizeof (snapshot));
        if (read_gateway_snapshot (gateway_, &snapshot)
            && (snapshot.state_flags & ZLINK_MONITOR_STATE_SEND_READY) != 0) {
            return;
        }
        msleep (step_ms);
    }
    TEST_FAIL_MESSAGE ("gateway send ready timeout");
}

void send_gateway_with_timeout (void *gateway_,
                                const char *payload_,
                                int timeout_ms_)
{
    const int step_ms = 5;
    const int attempts = timeout_ms_ / step_ms;
    for (int i = 0; i < attempts; ++i) {
        zlink_msg_t part;
        TEST_ASSERT_SUCCESS_ERRNO (
          zlink_msg_init_size (&part, strlen (payload_)));
        memcpy (zlink_msg_data (&part), payload_, strlen (payload_));
        if (zlink_gateway_send (gateway_, &part, 1, ZLINK_DONTWAIT) == 0)
            return;
        TEST_ASSERT_SUCCESS_ERRNO (zlink_msg_close (&part));
        if (errno != EAGAIN && errno != EHOSTUNREACH)
            break;
        msleep (step_ms);
    }
    TEST_FAIL_MESSAGE ("gateway send timeout");
}

void setup_registry (void *ctx_,
                     void **registry_out_,
                     const char *pub_ep_,
                     const char *router_ep_)
{
    void *registry = zlink_registry_new (ctx_);
    TEST_ASSERT_NOT_NULL (registry);
    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_registry_set_broadcast_interval (registry, 50));
    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_registry_bind (registry, pub_ep_, router_ep_));
    msleep (10);
    *registry_out_ = registry;
}

void make_registry_endpoint (char *endpoint_out_,
                             size_t endpoint_size_,
                             int port_seed_)
{
    snprintf (endpoint_out_, endpoint_size_, "tcp://127.0.0.1:%d",
              test_port (port_seed_));
}

void bind_gateway_with_timeout (void *gateway_,
                                const char *endpoint_,
                                int timeout_ms_)
{
    const int step_ms = 10;
    const int attempts = timeout_ms_ / step_ms;
    for (int i = 0; i < attempts; ++i) {
        if (zlink_gateway_bind (gateway_, endpoint_) == 0)
            return;
        if (errno != EAGAIN)
            break;
        msleep (step_ms);
    }
    TEST_FAIL_MESSAGE ("gateway bind timeout");
}

void *create_started_registry_with_port_seed (void *ctx_,
                                               int *port_seed_,
                                               char *pub_ep_out_,
                                               size_t pub_size_,
                                               char *router_ep_out_,
                                               size_t router_size_)
{
    for (int attempt = 0; attempt < 32; ++attempt) {
        void *registry = zlink_registry_new (ctx_);
        if (!registry)
            return NULL;
        snprintf (pub_ep_out_, pub_size_, "tcp://127.0.0.1:%d",
                  test_port (*port_seed_));
        snprintf (router_ep_out_, router_size_, "tcp://127.0.0.1:%d",
                  test_port (*port_seed_ + 1));
        if (zlink_registry_set_broadcast_interval (registry, 50) == 0
            && zlink_registry_bind (registry, pub_ep_out_, router_ep_out_)
                 == 0) {
            *port_seed_ += 2;
            return registry;
        }
        zlink_registry_destroy (&registry);
        *port_seed_ += 2;
    }
    errno = EADDRINUSE;
    return NULL;
}

void bind_gateway_with_port_seed (void *gateway_,
                                   const char *transport_,
                                   int *port_seed_,
                                   char *endpoint_out_,
                                   size_t endpoint_size_,
                                   int timeout_ms_)
{
    for (int port_attempt = 0; port_attempt < 32; ++port_attempt) {
        snprintf (endpoint_out_, endpoint_size_, "%s://127.0.0.1:%d",
                  transport_, test_port (*port_seed_));
        const int step_ms = 10;
        const int attempts = timeout_ms_ / step_ms;
        for (int i = 0; i < attempts; ++i) {
            if (zlink_gateway_bind (gateway_, endpoint_out_) == 0) {
                *port_seed_ += 1;
                return;
            }
            if (errno == EADDRINUSE)
                break;
            if (errno != EAGAIN)
                break;
            msleep (step_ms);
        }
        if (errno != EADDRINUSE)
            break;
        *port_seed_ += 1;
    }
    TEST_FAIL_MESSAGE ("gateway bind with port seed timeout");
}

void init_gateway_server (gateway_server_t *server_,
                          void *ctx_,
                          const char *registry_ep_,
                          const char *routing_id_,
                          const char *bind_ep_,
                          const char *service_name_,
                          zlink_socket_msg_handler_fn handler_,
                          gateway_probe_t *probe_)
{
    (void) handler_;
    step_log ("server discovery new");
    server_->discovery =
      zlink_discovery_new (ctx_, ZLINK_SERVICE_TYPE_GATEWAY);
    TEST_ASSERT_NOT_NULL (server_->discovery);
    step_log ("server discovery connect");
    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_discovery_connect_registry (server_->discovery, registry_ep_));
    step_log ("server gateway new");
    server_->gateway =
      create_gateway_attached (ctx_, server_->discovery, service_name_, routing_id_,
                         handler_);
    TEST_ASSERT_NOT_NULL (server_->gateway);
    server_->probe = probe_;

    step_log ("server gateway bind");
    bind_gateway_with_timeout (server_->gateway, bind_ep_, 3000);
    step_log ("server gateway bind done");
}

void init_gateway_server_with_port_seed (gateway_server_t *server_,
                                          void *ctx_,
                                          const char *registry_ep_,
                                          const char *routing_id_,
                                          int *port_seed_,
                                          char *bind_ep_out_,
                                          size_t bind_ep_size_,
                                          const char *service_name_,
                                          zlink_socket_msg_handler_fn handler_,
                                          gateway_probe_t *probe_)
{
    step_log ("server discovery new");
    server_->discovery =
      zlink_discovery_new (ctx_, ZLINK_SERVICE_TYPE_GATEWAY);
    TEST_ASSERT_NOT_NULL (server_->discovery);
    step_log ("server discovery connect");
    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_discovery_connect_registry (server_->discovery, registry_ep_));
    step_log ("server gateway new");
    server_->gateway =
      create_gateway_attached (ctx_, server_->discovery, service_name_, routing_id_,
                         handler_);
    TEST_ASSERT_NOT_NULL (server_->gateway);
    server_->probe = probe_;

    step_log ("server gateway bind");
    bind_gateway_with_port_seed (server_->gateway, "tcp", port_seed_,
                                  bind_ep_out_, bind_ep_size_, 3000);
    step_log ("server gateway bind done");
}

void destroy_gateway_server (gateway_server_t *server_)
{
    if (server_->gateway)
        TEST_ASSERT_SUCCESS_ERRNO (zlink_gateway_destroy (&server_->gateway));
    if (server_->discovery)
        TEST_ASSERT_SUCCESS_ERRNO (zlink_discovery_destroy (&server_->discovery));
}
}

void test_gateway_handover_provider_restart ()
{
    void *ctx = get_test_context ();
    TEST_ASSERT_NOT_NULL (ctx);
    const char *service_name = "ho-svc";
    const int timeout_ms = 3000;

    int port_seed = 25800;

    step_log ("setup registry");
    char registry_pub[MAX_SOCKET_STRING];
    char registry_router[MAX_SOCKET_STRING];
    void *registry = create_started_registry_with_port_seed (
      ctx, &port_seed, registry_pub, sizeof (registry_pub), registry_router,
      sizeof (registry_router));
    TEST_ASSERT_NOT_NULL (registry);

    void *client_discovery =
      zlink_discovery_new (ctx, ZLINK_SERVICE_TYPE_GATEWAY);
    TEST_ASSERT_NOT_NULL (client_discovery);
    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_discovery_connect_registry (client_discovery, registry_router));
    void *gateway =
      create_gateway_attached (ctx, client_discovery, service_name, NULL,
                         &gateway_handler_a);
    TEST_ASSERT_NOT_NULL (gateway);

    gateway_probe_t probe1;
    g_probe_a = &probe1;
    gateway_server_t server1;
    char ep1[256] = {0};
    step_log ("init server1");
    init_gateway_server_with_port_seed (&server1, ctx, registry_router,
                         "PROV-HO", &port_seed, ep1, sizeof (ep1),
                         service_name, &gateway_handler_a, &probe1);

    step_log ("wait gateway ready for server1");
    wait_gateway_ready (gateway, timeout_ms);
    step_log ("send msg1");
    send_gateway_with_timeout (gateway, "msg1", timeout_ms);
    step_log ("wait request on server1");
    TEST_ASSERT_TRUE (wait_for_calls (&probe1.requests, 1, timeout_ms));
    TEST_ASSERT_EQUAL_STRING ("msg1", probe1.payload);

    step_log ("destroy server1");
    destroy_gateway_server (&server1);
    msleep (300);

    gateway_probe_t probe2;
    g_probe_a = &probe2;
    gateway_server_t server2;
    char ep2[256] = {0};
    step_log ("init server2");
    init_gateway_server_with_port_seed (&server2, ctx, registry_router,
                         "PROV-HO", &port_seed, ep2, sizeof (ep2),
                         service_name, &gateway_handler_a, &probe2);

    step_log ("wait gateway ready for server2");
    wait_gateway_ready (gateway, timeout_ms);
    step_log ("send msg2");
    send_gateway_with_timeout (gateway, "msg2", timeout_ms);
    step_log ("wait request on server2");
    TEST_ASSERT_TRUE (wait_for_calls (&probe2.requests, 1, timeout_ms));
    TEST_ASSERT_EQUAL_STRING ("msg2", probe2.payload);
    TEST_ASSERT_EQUAL_INT (1, probe1.requests.load ());

    destroy_gateway_server (&server2);
    destroy_gateway_server (&server1);
    TEST_ASSERT_SUCCESS_ERRNO (zlink_gateway_destroy (&gateway));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_discovery_destroy (&client_discovery));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_registry_destroy (&registry));
    g_probe_a = NULL;
}

void test_provider_handover_gateway_reconnect ()
{
    void *ctx = get_test_context ();
    TEST_ASSERT_NOT_NULL (ctx);
    const char *service_name = "ho-svc2";
    const int timeout_ms = 3000;
    const char gw_rid[] = "GW-HO";

    int port_seed = 25810;

    step_log ("setup registry");
    char registry_pub[MAX_SOCKET_STRING];
    char registry_router[MAX_SOCKET_STRING];
    void *registry = create_started_registry_with_port_seed (
      ctx, &port_seed, registry_pub, sizeof (registry_pub), registry_router,
      sizeof (registry_router));
    TEST_ASSERT_NOT_NULL (registry);

    gateway_probe_t server_probe;
    g_probe_b = &server_probe;
    gateway_server_t server;
    char ep[256] = {0};
    init_gateway_server_with_port_seed (&server, ctx, registry_router,
                         "PROV-HO2", &port_seed, ep, sizeof (ep),
                         service_name, &gateway_handler_b, &server_probe);

    void *discovery1 = zlink_discovery_new (ctx, ZLINK_SERVICE_TYPE_GATEWAY);
    TEST_ASSERT_NOT_NULL (discovery1);
    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_discovery_connect_registry (discovery1, registry_router));
    void *gateway1 =
      create_gateway_attached (ctx, discovery1, service_name, gw_rid,
                         &gateway_handler_a);
    TEST_ASSERT_NOT_NULL (gateway1);
    step_log ("wait gateway1 ready");
    wait_gateway_ready (gateway1, timeout_ms);
    step_log ("send gw-1");
    send_gateway_with_timeout (gateway1, "gw-1", timeout_ms);
    step_log ("wait gw-1 request");
    TEST_ASSERT_TRUE (wait_for_calls (&server_probe.requests, 1, timeout_ms));
    TEST_ASSERT_EQUAL_STRING ("gw-1", server_probe.payload);

    TEST_ASSERT_SUCCESS_ERRNO (zlink_gateway_destroy (&gateway1));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_discovery_destroy (&discovery1));
    msleep (300);

    void *discovery2 = zlink_discovery_new (ctx, ZLINK_SERVICE_TYPE_GATEWAY);
    TEST_ASSERT_NOT_NULL (discovery2);
    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_discovery_connect_registry (discovery2, registry_router));
    void *gateway2 =
      create_gateway_attached (ctx, discovery2, service_name, gw_rid,
                         &gateway_handler_a);
    TEST_ASSERT_NOT_NULL (gateway2);
    step_log ("wait gateway2 ready");
    wait_gateway_ready (gateway2, timeout_ms);
    step_log ("send gw-2");
    send_gateway_with_timeout (gateway2, "gw-2", timeout_ms);
    step_log ("wait gw-2 request");
    TEST_ASSERT_TRUE (wait_for_calls (&server_probe.requests, 2, timeout_ms));
    TEST_ASSERT_EQUAL_STRING ("gw-2", server_probe.payload);

    TEST_ASSERT_SUCCESS_ERRNO (zlink_gateway_destroy (&gateway2));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_discovery_destroy (&discovery2));
    destroy_gateway_server (&server);
    TEST_ASSERT_SUCCESS_ERRNO (zlink_registry_destroy (&registry));
    g_probe_b = NULL;
}

int main (void)
{
    UNITY_BEGIN ();
    RUN_TEST (test_gateway_handover_provider_restart);
    RUN_TEST (test_provider_handover_gateway_reconnect);
    return UNITY_END ();
}
