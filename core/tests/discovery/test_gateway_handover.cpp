/* SPDX-License-Identifier: MPL-2.0 */

#include "../testutil.hpp"
#include "../testutil_unity.hpp"

#include <atomic>
#include <string.h>

SETUP_TEARDOWN_TESTCONTEXT

namespace
{
struct gateway_probe_t
{
    gateway_probe_t () : requests (0), control (0)
    {
        memset (service, 0, sizeof (service));
        memset (payload, 0, sizeof (payload));
    }

    std::atomic<int> requests;
    std::atomic<int> control;
    char service[64];
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

void step_log (const char *msg_)
{
    if (getenv ("ZLINK_TEST_DEBUG")) {
        fprintf (stderr, "[handover] %s\n", msg_ ? msg_ : "");
        fflush (stderr);
    }
}

void discard_gateway_message (zlink_gateway_msg_kind_t kind_,
                              const char *,
                              size_t,
                              const zlink_routing_id_t *,
                              zlink_msg_t *parts_,
                              size_t part_count_)
{
    (void) kind_;
    for (size_t i = 0; i < part_count_; ++i)
        zlink_msg_close (&parts_[i]);
}

void record_gateway_event (gateway_probe_t *probe_,
                           zlink_gateway_msg_kind_t kind_,
                           const char *service_name_,
                           size_t service_name_len_,
                           zlink_msg_t *parts_,
                           size_t part_count_)
{
    if (!probe_) {
        for (size_t i = 0; i < part_count_; ++i)
            zlink_msg_close (&parts_[i]);
        return;
    }

    if (kind_ == ZLINK_GATEWAY_MSG_REQUEST) {
        const size_t service_copy =
          service_name_len_ < sizeof (probe_->service) - 1
            ? service_name_len_
            : sizeof (probe_->service) - 1;
        memcpy (probe_->service, service_name_, service_copy);
        probe_->service[service_copy] = '\0';

        if (part_count_ > 0) {
            const size_t size = zlink_msg_size (&parts_[0]);
            const size_t payload_copy =
              size < sizeof (probe_->payload) - 1
                ? size
                : sizeof (probe_->payload) - 1;
            memcpy (probe_->payload, zlink_msg_data (&parts_[0]), payload_copy);
            probe_->payload[payload_copy] = '\0';
        }
        probe_->requests.fetch_add (1);
        for (size_t i = 0; i < part_count_; ++i)
            zlink_msg_close (&parts_[i]);
        return;
    } else if (kind_ == ZLINK_GATEWAY_MSG_CONTROL) {
        probe_->control.fetch_add (1);
        return;
    }

    for (size_t i = 0; i < part_count_; ++i)
        zlink_msg_close (&parts_[i]);
}

void gateway_handler_a (zlink_gateway_msg_kind_t kind_,
                        const char *service_name_,
                        size_t service_name_len_,
                        const zlink_routing_id_t *,
                        zlink_msg_t *parts_,
                        size_t part_count_)
{
    record_gateway_event (g_probe_a, kind_, service_name_, service_name_len_,
                          parts_, part_count_);
}

void gateway_handler_b (zlink_gateway_msg_kind_t kind_,
                        const char *service_name_,
                        size_t service_name_len_,
                        const zlink_routing_id_t *,
                        zlink_msg_t *parts_,
                        size_t part_count_)
{
    record_gateway_event (g_probe_b, kind_, service_name_, service_name_len_,
                          parts_, part_count_);
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

void wait_gateway_ready (void *gateway_,
                         const char *service_name_,
                         int timeout_ms_)
{
    const int step_ms = 10;
    const int attempts = timeout_ms_ / step_ms;
    for (int i = 0; i < attempts; ++i) {
        if (zlink_gateway_connection_count (gateway_, service_name_) > 0)
            return;
        msleep (step_ms);
    }
    TEST_FAIL_MESSAGE ("gateway connection timeout");
}

void send_gateway_with_timeout (void *gateway_,
                                const char *service_name_,
                                const char *payload_,
                                int timeout_ms_)
{
    const int step_ms = 5;
    const int attempts = timeout_ms_ / step_ms;
    for (int i = 0; i < attempts; ++i) {
        if (zlink_gateway_send_bytes (gateway_, service_name_, payload_,
                                      strlen (payload_), ZLINK_DONTWAIT)
            == 0)
            return;
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
      zlink_registry_set_endpoints (registry, pub_ep_, router_ep_));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_registry_start (registry));
    *registry_out_ = registry;
}

void register_gateway_with_timeout (void *gateway_,
                                    const char *service_name_,
                                    const char *endpoint_,
                                    uint32_t weight_,
                                    int timeout_ms_)
{
    const int step_ms = 10;
    const int attempts = timeout_ms_ / step_ms;
    for (int i = 0; i < attempts; ++i) {
        if (zlink_gateway_register (gateway_, service_name_, endpoint_, weight_)
            == 0)
            return;
        if (errno != EAGAIN)
            break;
        msleep (step_ms);
    }
    TEST_FAIL_MESSAGE ("gateway register timeout");
}

void init_gateway_server (gateway_server_t *server_,
                          void *ctx_,
                          const char *registry_ep_,
                          const char *routing_id_,
                          const char *bind_ep_,
                          const char *service_name_,
                          zlink_gateway_handler_fn handler_,
                          gateway_probe_t *probe_)
{
    step_log ("server discovery new");
    server_->discovery =
      zlink_discovery_new_typed (ctx_, ZLINK_SERVICE_TYPE_GATEWAY);
    TEST_ASSERT_NOT_NULL (server_->discovery);
    step_log ("server discovery connect");
    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_discovery_connect_registry (server_->discovery, registry_ep_));
    step_log ("server gateway new");
    server_->gateway = zlink_gateway_new (ctx_, server_->discovery, routing_id_,
                                          &discard_gateway_message);
    TEST_ASSERT_NOT_NULL (server_->gateway);
    server_->probe = probe_;

    step_log ("server gateway bind");
    TEST_ASSERT_SUCCESS_ERRNO (zlink_gateway_bind (server_->gateway, bind_ep_));
    step_log ("server gateway register");
    register_gateway_with_timeout (server_->gateway, service_name_, bind_ep_, 1,
                                   3000);
    if (handler_ != &discard_gateway_message) {
        step_log ("server gateway set handler");
        TEST_ASSERT_SUCCESS_ERRNO (
          zlink_gateway_set_handler (server_->gateway, handler_));
    }
    step_log ("server gateway register done");
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

    step_log ("setup registry");
    void *registry = NULL;
    setup_registry (ctx, &registry, "inproc://reg-pub-ho1",
                    "inproc://reg-router-ho1");
    msleep (100);

    void *client_discovery =
      zlink_discovery_new_typed (ctx, ZLINK_SERVICE_TYPE_GATEWAY);
    TEST_ASSERT_NOT_NULL (client_discovery);
    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_discovery_connect_registry (client_discovery, "inproc://reg-router-ho1"));
    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_discovery_subscribe (client_discovery, service_name));

    void *gateway =
      zlink_gateway_new (ctx, client_discovery, NULL, &gateway_handler_a);
    TEST_ASSERT_NOT_NULL (gateway);

    gateway_probe_t probe1;
    g_probe_a = &probe1;
    gateway_server_t server1;
    char ep1[256] = {0};
    snprintf (ep1, sizeof (ep1), "tcp://127.0.0.1:%d", test_port (22650));
    step_log ("init server1");
    init_gateway_server (&server1, ctx, "inproc://reg-router-ho1", "PROV-HO",
                         ep1, service_name, &gateway_handler_a, &probe1);

    step_log ("wait gateway ready for server1");
    wait_gateway_ready (gateway, service_name, timeout_ms);
    step_log ("send msg1");
    send_gateway_with_timeout (gateway, service_name, "msg1", timeout_ms);
    step_log ("wait request on server1");
    TEST_ASSERT_TRUE (wait_for_calls (&probe1.requests, 1, timeout_ms));
    TEST_ASSERT_EQUAL_STRING ("msg1", probe1.payload);

    step_log ("unregister server1");
    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_gateway_unregister (server1.gateway, service_name));
    msleep (300);

    gateway_probe_t probe2;
    g_probe_a = &probe2;
    gateway_server_t server2;
    char ep2[256] = {0};
    snprintf (ep2, sizeof (ep2), "tcp://127.0.0.1:%d", test_port (22651));
    step_log ("init server2");
    init_gateway_server (&server2, ctx, "inproc://reg-router-ho1", "PROV-HO",
                         ep2, service_name, &gateway_handler_a, &probe2);

    step_log ("wait gateway ready for server2");
    wait_gateway_ready (gateway, service_name, timeout_ms);
    step_log ("send msg2");
    send_gateway_with_timeout (gateway, service_name, "msg2", timeout_ms);
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

    step_log ("setup registry");
    void *registry = NULL;
    setup_registry (ctx, &registry, "inproc://reg-pub-ho2",
                    "inproc://reg-router-ho2");
    msleep (100);

    gateway_probe_t server_probe;
    g_probe_b = &server_probe;
    gateway_server_t server;
    char ep[256] = {0};
    snprintf (ep, sizeof (ep), "tcp://127.0.0.1:%d", test_port (22652));
    init_gateway_server (&server, ctx, "inproc://reg-router-ho2", "PROV-HO2",
                         ep, service_name, &gateway_handler_b, &server_probe);

    void *discovery1 = zlink_discovery_new_typed (ctx, ZLINK_SERVICE_TYPE_GATEWAY);
    TEST_ASSERT_NOT_NULL (discovery1);
    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_discovery_connect_registry (discovery1, "inproc://reg-router-ho2"));
    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_discovery_subscribe (discovery1, service_name));

    void *gateway1 = zlink_gateway_new (ctx, discovery1, gw_rid, &gateway_handler_a);
    TEST_ASSERT_NOT_NULL (gateway1);
    step_log ("wait gateway1 ready");
    wait_gateway_ready (gateway1, service_name, timeout_ms);
    step_log ("send gw-1");
    send_gateway_with_timeout (gateway1, service_name, "gw-1", timeout_ms);
    step_log ("wait gw-1 request");
    TEST_ASSERT_TRUE (wait_for_calls (&server_probe.requests, 1, timeout_ms));
    TEST_ASSERT_EQUAL_STRING ("gw-1", server_probe.payload);

    TEST_ASSERT_SUCCESS_ERRNO (zlink_gateway_destroy (&gateway1));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_discovery_destroy (&discovery1));
    msleep (300);

    void *discovery2 = zlink_discovery_new_typed (ctx, ZLINK_SERVICE_TYPE_GATEWAY);
    TEST_ASSERT_NOT_NULL (discovery2);
    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_discovery_connect_registry (discovery2, "inproc://reg-router-ho2"));
    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_discovery_subscribe (discovery2, service_name));

    void *gateway2 = zlink_gateway_new (ctx, discovery2, gw_rid, &gateway_handler_a);
    TEST_ASSERT_NOT_NULL (gateway2);
    step_log ("wait gateway2 ready");
    wait_gateway_ready (gateway2, service_name, timeout_ms);
    step_log ("send gw-2");
    send_gateway_with_timeout (gateway2, service_name, "gw-2", timeout_ms);
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
