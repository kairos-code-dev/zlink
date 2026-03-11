/* SPDX-License-Identifier: MPL-2.0 */

#include "../testutil.hpp"
#include "../testutil_unity.hpp"

#include <atomic>
#include <string.h>
#include <thread>
#include <vector>

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
    gateway_server_t () : discovery (NULL), gateway (NULL) {}

    void *discovery;
    void *gateway;
};

gateway_probe_t *g_probe_a = NULL;
gateway_probe_t *g_probe_b = NULL;

void step_log (const char *msg_)
{
    if (getenv ("ZLINK_TEST_DEBUG")) {
        fprintf (stderr, "[gateway] %s\n", msg_ ? msg_ : "");
        fflush (stderr);
    }
}

void close_parts (zlink_msg_t *parts_, size_t part_count_)
{
    for (size_t i = 0; i < part_count_; ++i)
        zlink_msg_close (&parts_[i]);
}

void discard_gateway_message (zlink_gateway_msg_kind_t kind_,
                              const char *,
                              size_t,
                              const zlink_routing_id_t *,
                              zlink_msg_t *parts_,
                              size_t part_count_)
{
    if (kind_ == ZLINK_GATEWAY_MSG_CONTROL)
        return;
    close_parts (parts_, part_count_);
}

void record_gateway_message (gateway_probe_t *probe_,
                             zlink_gateway_msg_kind_t kind_,
                             const char *service_name_,
                             size_t service_name_len_,
                             zlink_msg_t *parts_,
                             size_t part_count_)
{
    if (!probe_) {
        discard_gateway_message (kind_, service_name_, service_name_len_, NULL,
                                 parts_, part_count_);
        return;
    }

    if (kind_ == ZLINK_GATEWAY_MSG_CONTROL) {
        probe_->control.fetch_add (1);
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
    }

    close_parts (parts_, part_count_);
}

void gateway_handler_a (zlink_gateway_msg_kind_t kind_,
                        const char *service_name_,
                        size_t service_name_len_,
                        const zlink_routing_id_t *,
                        zlink_msg_t *parts_,
                        size_t part_count_)
{
    record_gateway_message (g_probe_a, kind_, service_name_, service_name_len_,
                            parts_, part_count_);
}

void gateway_handler_b (zlink_gateway_msg_kind_t kind_,
                        const char *service_name_,
                        size_t service_name_len_,
                        const zlink_routing_id_t *,
                        zlink_msg_t *parts_,
                        size_t part_count_)
{
    record_gateway_message (g_probe_b, kind_, service_name_, service_name_len_,
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

void update_gateway_weight_with_timeout (void *gateway_,
                                         const char *service_name_,
                                         uint32_t weight_,
                                         int timeout_ms_)
{
    const int step_ms = 10;
    const int attempts = timeout_ms_ / step_ms;
    for (int i = 0; i < attempts; ++i) {
        if (zlink_gateway_update_weight (gateway_, service_name_, weight_) == 0)
            return;
        if (errno != EAGAIN)
            break;
        msleep (step_ms);
    }
    TEST_FAIL_MESSAGE ("gateway update timeout");
}

void send_gateway_with_timeout (void *gateway_,
                                const char *service_name_,
                                const char *payload_,
                                int timeout_ms_)
{
    const int step_ms = 2;
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

void send_gateway_rid_with_timeout (void *gateway_,
                                    const char *service_name_,
                                    const zlink_routing_id_t *rid_,
                                    const char *payload_,
                                    int timeout_ms_)
{
    const int step_ms = 2;
    const int attempts = timeout_ms_ / step_ms;
    for (int i = 0; i < attempts; ++i) {
        if (zlink_gateway_send_rid_bytes (gateway_, service_name_, rid_,
                                          payload_, strlen (payload_),
                                          ZLINK_DONTWAIT)
            == 0)
            return;
        if (errno != EAGAIN && errno != EHOSTUNREACH)
            break;
        msleep (step_ms);
    }
    TEST_FAIL_MESSAGE ("gateway send_rid timeout");
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
    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_registry_set_broadcast_interval (registry, 50));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_registry_start (registry));
    *registry_out_ = registry;
}

void create_server_gateway (gateway_server_t *server_,
                            void *ctx_,
                            const char *registry_ep_,
                            const char *routing_id_)
{
    server_->discovery =
      zlink_discovery_new_typed (ctx_, ZLINK_SERVICE_TYPE_GATEWAY);
    TEST_ASSERT_NOT_NULL (server_->discovery);
    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_discovery_connect_registry (server_->discovery, registry_ep_));
    server_->gateway = zlink_gateway_new (ctx_, server_->discovery, routing_id_,
                                          &discard_gateway_message);
    TEST_ASSERT_NOT_NULL (server_->gateway);
}

void bind_register_server (gateway_server_t *server_,
                           const char *endpoint_,
                           const char *service_name_,
                           uint32_t weight_,
                           zlink_gateway_handler_fn handler_)
{
    TEST_ASSERT_SUCCESS_ERRNO (zlink_gateway_bind (server_->gateway, endpoint_));
    register_gateway_with_timeout (server_->gateway, service_name_, endpoint_,
                                   weight_, 3000);
    if (handler_ != &discard_gateway_message)
        TEST_ASSERT_SUCCESS_ERRNO (
          zlink_gateway_set_handler (server_->gateway, handler_));
}

void destroy_server_gateway (gateway_server_t *server_)
{
    if (server_->gateway)
        TEST_ASSERT_SUCCESS_ERRNO (zlink_gateway_destroy (&server_->gateway));
    if (server_->discovery)
        TEST_ASSERT_SUCCESS_ERRNO (zlink_discovery_destroy (&server_->discovery));
}

void *create_client_gateway (void *ctx_,
                             const char *registry_ep_,
                             const char *service_name_,
                             const char *routing_id_)
{
    void *discovery =
      zlink_discovery_new_typed (ctx_, ZLINK_SERVICE_TYPE_GATEWAY);
    TEST_ASSERT_NOT_NULL (discovery);
    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_discovery_connect_registry (discovery, registry_ep_));
    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_discovery_subscribe (discovery, service_name_));

    void *gateway =
      zlink_gateway_new (ctx_, discovery, routing_id_, &discard_gateway_message);
    TEST_ASSERT_NOT_NULL (gateway);
    return gateway;
}

void destroy_client_gateway (void **gateway_p_, void **discovery_p_)
{
    if (gateway_p_ && *gateway_p_)
        TEST_ASSERT_SUCCESS_ERRNO (zlink_gateway_destroy (gateway_p_));
    if (discovery_p_ && *discovery_p_)
        TEST_ASSERT_SUCCESS_ERRNO (zlink_discovery_destroy (discovery_p_));
}

struct send_worker_args_t
{
    void *gateway;
    const char *service_name;
    int count;
    std::atomic<int> *ok;
    std::atomic<int> *fail;
};

void send_worker (void *arg_)
{
    send_worker_args_t *args = static_cast<send_worker_args_t *> (arg_);
    for (int i = 0; i < args->count; ++i) {
        const char *payload = "sync";
        int rc = -1;
        for (int attempt = 0; attempt < 50; ++attempt) {
            rc = zlink_gateway_send_bytes (args->gateway, args->service_name,
                                           payload, 4, ZLINK_DONTWAIT);
            if (rc == 0)
                break;
            if (errno != EAGAIN && errno != EHOSTUNREACH)
                break;
            msleep (1);
        }
        if (rc == 0)
            ++(*args->ok);
        else
            ++(*args->fail);
    }
}
}

static void test_gateway_provider_setsockopt ()
{
    step_log ("test_gateway_provider_setsockopt");
    void *ctx = get_test_context ();
    TEST_ASSERT_NOT_NULL (ctx);

    void *client_discovery =
      zlink_discovery_new_typed (ctx, ZLINK_SERVICE_TYPE_GATEWAY);
    TEST_ASSERT_NOT_NULL (client_discovery);
    void *client =
      zlink_gateway_new (ctx, client_discovery, NULL, &discard_gateway_message);
    TEST_ASSERT_NOT_NULL (client);

    gateway_server_t server;
    server.discovery =
      zlink_discovery_new_typed (ctx, ZLINK_SERVICE_TYPE_GATEWAY);
    TEST_ASSERT_NOT_NULL (server.discovery);
    server.gateway = zlink_gateway_new (ctx, server.discovery, NULL,
                                        &discard_gateway_message);
    TEST_ASSERT_NOT_NULL (server.gateway);

    const int hwm = 1000000;
    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_gateway_set_option (client, ZLINK_GATEWAY_OPT_SNDHWM, &hwm,
                                sizeof (hwm)));
    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_gateway_set_option (client, ZLINK_GATEWAY_OPT_RCVHWM, &hwm,
                                sizeof (hwm)));
    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_gateway_set_option (server.gateway, ZLINK_GATEWAY_OPT_SNDHWM, &hwm,
                                sizeof (hwm)));
    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_gateway_set_option (server.gateway, ZLINK_GATEWAY_OPT_RCVHWM, &hwm,
                                sizeof (hwm)));

    destroy_server_gateway (&server);
    TEST_ASSERT_SUCCESS_ERRNO (zlink_gateway_destroy (&client));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_discovery_destroy (&client_discovery));
}

static void test_gateway_can_be_polled_via_service_instance ()
{
    step_log ("test_gateway_can_be_polled_via_service_instance");
    void *ctx = get_test_context ();
    TEST_ASSERT_NOT_NULL (ctx);

    void *registry = NULL;
    setup_registry (ctx, &registry, "inproc://reg-pub-gateway-poll",
                    "inproc://reg-router-gateway-poll");
    msleep (100);

    void *client_discovery =
      zlink_discovery_new_typed (ctx, ZLINK_SERVICE_TYPE_GATEWAY);
    TEST_ASSERT_NOT_NULL (client_discovery);
    TEST_ASSERT_SUCCESS_ERRNO (zlink_discovery_connect_registry (
      client_discovery, "inproc://reg-router-gateway-poll"));
    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_discovery_subscribe (client_discovery, "poll-svc"));
    void *client =
      zlink_gateway_new (ctx, client_discovery, NULL, &discard_gateway_message);
    TEST_ASSERT_NOT_NULL (client);

    gateway_probe_t probe;
    g_probe_a = &probe;
    gateway_server_t server;
    create_server_gateway (&server, ctx, "inproc://reg-router-gateway-poll",
                           "poll-server");
    char endpoint[MAX_SOCKET_STRING];
    snprintf (endpoint, sizeof (endpoint), "tcp://127.0.0.1:%d",
              test_port (22400));
    bind_register_server (&server, endpoint, "poll-svc", 1, &gateway_handler_a);

    wait_gateway_ready (client, "poll-svc", 2000);

    void *poller = zlink_poller_new ();
    TEST_ASSERT_NOT_NULL (poller);
    int tag = 41;
    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_poller_add_gateway (poller, client, &tag, ZLINK_POLLOUT));
    zlink_poller_event_t event;
    memset (&event, 0, sizeof (event));
    TEST_ASSERT_EQUAL_INT (1, zlink_poller_wait (poller, &event, 2000));
    TEST_ASSERT_TRUE ((event.events & ZLINK_POLLOUT) != 0);
    TEST_ASSERT_EQUAL_PTR (&tag, event.user_data);

    send_gateway_with_timeout (client, "poll-svc", "hello", 2000);
    TEST_ASSERT_TRUE (wait_for_calls (&probe.requests, 1, 2000));
    TEST_ASSERT_EQUAL_STRING ("hello", probe.payload);

    TEST_ASSERT_SUCCESS_ERRNO (zlink_poller_destroy (&poller));
    destroy_server_gateway (&server);
    TEST_ASSERT_SUCCESS_ERRNO (zlink_gateway_destroy (&client));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_discovery_destroy (&client_discovery));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_registry_destroy (&registry));
    g_probe_a = NULL;
}

static void test_gateway_refreshes_existing_service_on_first_connection_count ()
{
    step_log ("test_gateway_refreshes_existing_service_on_first_connection_count");
    void *ctx = get_test_context ();
    TEST_ASSERT_NOT_NULL (ctx);

    void *registry = NULL;
    setup_registry (ctx, &registry, "inproc://reg-pub-gateway-late",
                    "inproc://reg-router-gateway-late");
    msleep (100);

    gateway_server_t server;
    create_server_gateway (&server, ctx, "inproc://reg-router-gateway-late",
                           "late-server");
    char endpoint[MAX_SOCKET_STRING];
    snprintf (endpoint, sizeof (endpoint), "tcp://127.0.0.1:%d",
              test_port (22402));
    bind_register_server (&server, endpoint, "late-gateway-svc", 1,
                          &discard_gateway_message);

    void *discovery =
      zlink_discovery_new_typed (ctx, ZLINK_SERVICE_TYPE_GATEWAY);
    TEST_ASSERT_NOT_NULL (discovery);
    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_discovery_connect_registry (discovery,
                                        "inproc://reg-router-gateway-late"));
    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_discovery_subscribe (discovery, "late-gateway-svc"));
    void *client =
      zlink_gateway_new (ctx, discovery, NULL, &discard_gateway_message);
    TEST_ASSERT_NOT_NULL (client);

    wait_gateway_ready (client, "late-gateway-svc", 2000);
    TEST_ASSERT_TRUE (
      zlink_gateway_connection_count (client, "late-gateway-svc") > 0);

    destroy_server_gateway (&server);
    TEST_ASSERT_SUCCESS_ERRNO (zlink_gateway_destroy (&client));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_discovery_destroy (&discovery));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_registry_destroy (&registry));
}

static void test_gateway_router_peers_do_not_enter_pollable_mode ()
{
    step_log ("test_gateway_router_peers_do_not_enter_pollable_mode");
    void *ctx = get_test_context ();
    TEST_ASSERT_NOT_NULL (ctx);

    void *registry = NULL;
    setup_registry (ctx, &registry, "inproc://reg-pub-gateway-peers",
                    "inproc://reg-router-gateway-peers");
    msleep (100);

    void *discovery =
      zlink_discovery_new_typed (ctx, ZLINK_SERVICE_TYPE_GATEWAY);
    TEST_ASSERT_NOT_NULL (discovery);
    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_discovery_connect_registry (discovery,
                                        "inproc://reg-router-gateway-peers"));
    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_discovery_subscribe (discovery, "peer-stats-svc"));
    void *client =
      zlink_gateway_new (ctx, discovery, NULL, &discard_gateway_message);
    TEST_ASSERT_NOT_NULL (client);

    gateway_probe_t probe;
    g_probe_a = &probe;
    gateway_server_t server;
    create_server_gateway (&server, ctx, "inproc://reg-router-gateway-peers",
                           "peer-server");
    char endpoint[MAX_SOCKET_STRING];
    snprintf (endpoint, sizeof (endpoint), "tcp://127.0.0.1:%d",
              test_port (22403));
    bind_register_server (&server, endpoint, "peer-stats-svc", 1,
                          &gateway_handler_a);

    wait_gateway_ready (client, "peer-stats-svc", 2000);

    size_t count = 0;
    TEST_ASSERT_SUCCESS_ERRNO (zlink_gateway_router_peers (client, NULL, &count));
    TEST_ASSERT_TRUE (count > 0);

    send_gateway_with_timeout (client, "peer-stats-svc", "ping", 2000);
    TEST_ASSERT_TRUE (wait_for_calls (&probe.requests, 1, 2000));

    destroy_server_gateway (&server);
    TEST_ASSERT_SUCCESS_ERRNO (zlink_gateway_destroy (&client));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_discovery_destroy (&discovery));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_registry_destroy (&registry));
    g_probe_a = NULL;
}

static void test_gateway_single_service_tcp ()
{
    step_log ("test_gateway_single_service_tcp");
    void *ctx = get_test_context ();
    TEST_ASSERT_NOT_NULL (ctx);

    void *registry = NULL;
    setup_registry (ctx, &registry, "inproc://reg-pub-gateway1",
                    "inproc://reg-router-gateway1");
    msleep (100);

    void *discovery =
      zlink_discovery_new_typed (ctx, ZLINK_SERVICE_TYPE_GATEWAY);
    TEST_ASSERT_NOT_NULL (discovery);
    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_discovery_connect_registry (discovery, "inproc://reg-router-gateway1"));
    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_discovery_subscribe (discovery, "svc"));
    void *client =
      zlink_gateway_new (ctx, discovery, NULL, &discard_gateway_message);
    TEST_ASSERT_NOT_NULL (client);

    gateway_probe_t probe;
    g_probe_a = &probe;
    gateway_server_t server;
    create_server_gateway (&server, ctx, "inproc://reg-router-gateway1",
                           "PROV1");
    char endpoint[256];
    snprintf (endpoint, sizeof (endpoint), "tcp://127.0.0.1:%d", test_port (22500));
    bind_register_server (&server, endpoint, "svc", 1, &gateway_handler_a);

    wait_gateway_ready (client, "svc", 2000);
    send_gateway_with_timeout (client, "svc", "hello", 2000);
    TEST_ASSERT_TRUE (wait_for_calls (&probe.requests, 1, 2000));
    TEST_ASSERT_EQUAL_STRING ("svc", probe.service);
    TEST_ASSERT_EQUAL_STRING ("hello", probe.payload);

    destroy_server_gateway (&server);
    TEST_ASSERT_SUCCESS_ERRNO (zlink_gateway_destroy (&client));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_discovery_destroy (&discovery));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_registry_destroy (&registry));
    g_probe_a = NULL;
}

static void test_gateway_send_rid_tcp ()
{
    step_log ("test_gateway_send_rid_tcp");
    void *ctx = get_test_context ();
    TEST_ASSERT_NOT_NULL (ctx);

    void *registry = NULL;
    setup_registry (ctx, &registry, "inproc://reg-pub-gateway-rid",
                    "inproc://reg-router-gateway-rid");
    msleep (100);

    void *discovery =
      zlink_discovery_new_typed (ctx, ZLINK_SERVICE_TYPE_GATEWAY);
    TEST_ASSERT_NOT_NULL (discovery);
    TEST_ASSERT_SUCCESS_ERRNO (zlink_discovery_connect_registry (
      discovery, "inproc://reg-router-gateway-rid"));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_discovery_subscribe (discovery, "svc"));
    void *client =
      zlink_gateway_new (ctx, discovery, NULL, &discard_gateway_message);
    TEST_ASSERT_NOT_NULL (client);

    gateway_probe_t probe1;
    gateway_probe_t probe2;
    g_probe_a = &probe1;
    g_probe_b = &probe2;
    gateway_server_t server1;
    gateway_server_t server2;
    create_server_gateway (&server1, ctx, "inproc://reg-router-gateway-rid",
                           "PROV-A");
    create_server_gateway (&server2, ctx, "inproc://reg-router-gateway-rid",
                           "PROV-B");

    char ep1[256];
    char ep2[256];
    snprintf (ep1, sizeof (ep1), "tcp://127.0.0.1:%d", test_port (22501));
    snprintf (ep2, sizeof (ep2), "tcp://127.0.0.1:%d", test_port (22502));
    bind_register_server (&server1, ep1, "svc", 1, &gateway_handler_a);
    bind_register_server (&server2, ep2, "svc", 1, &gateway_handler_b);

    wait_gateway_ready (client, "svc", 2000);
    zlink_routing_id_t rid;
    memset (&rid, 0, sizeof (rid));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_gateway_routing_id (server2.gateway, &rid));
    send_gateway_rid_with_timeout (client, "svc", &rid, "rid-msg", 2000);
    TEST_ASSERT_TRUE (wait_for_calls (&probe2.requests, 1, 2000));
    TEST_ASSERT_EQUAL_INT (0, probe1.requests.load ());
    TEST_ASSERT_EQUAL_STRING ("rid-msg", probe2.payload);

    destroy_server_gateway (&server2);
    destroy_server_gateway (&server1);
    TEST_ASSERT_SUCCESS_ERRNO (zlink_gateway_destroy (&client));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_discovery_destroy (&discovery));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_registry_destroy (&registry));
    g_probe_a = NULL;
    g_probe_b = NULL;
}

static void test_gateway_multi_service_tcp ()
{
    step_log ("test_gateway_multi_service_tcp");
    void *ctx = get_test_context ();
    TEST_ASSERT_NOT_NULL (ctx);

    void *registry = NULL;
    setup_registry (ctx, &registry, "inproc://reg-pub-gateway2",
                    "inproc://reg-router-gateway2");
    msleep (100);

    void *discovery =
      zlink_discovery_new_typed (ctx, ZLINK_SERVICE_TYPE_GATEWAY);
    TEST_ASSERT_NOT_NULL (discovery);
    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_discovery_connect_registry (discovery, "inproc://reg-router-gateway2"));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_discovery_subscribe (discovery, "svc-A"));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_discovery_subscribe (discovery, "svc-B"));
    void *client =
      zlink_gateway_new (ctx, discovery, NULL, &discard_gateway_message);
    TEST_ASSERT_NOT_NULL (client);

    gateway_probe_t probe_a;
    gateway_probe_t probe_b;
    g_probe_a = &probe_a;
    g_probe_b = &probe_b;
    gateway_server_t server_a;
    gateway_server_t server_b;
    create_server_gateway (&server_a, ctx, "inproc://reg-router-gateway2", "A");
    create_server_gateway (&server_b, ctx, "inproc://reg-router-gateway2", "B");
    char ep_a[256];
    char ep_b[256];
    snprintf (ep_a, sizeof (ep_a), "tcp://127.0.0.1:%d", test_port (22510));
    snprintf (ep_b, sizeof (ep_b), "tcp://127.0.0.1:%d", test_port (22511));
    bind_register_server (&server_a, ep_a, "svc-A", 1, &gateway_handler_a);
    bind_register_server (&server_b, ep_b, "svc-B", 1, &gateway_handler_b);

    wait_gateway_ready (client, "svc-A", 2000);
    wait_gateway_ready (client, "svc-B", 2000);
    send_gateway_with_timeout (client, "svc-A", "msg-a", 2000);
    send_gateway_with_timeout (client, "svc-B", "msg-b", 2000);
    TEST_ASSERT_TRUE (wait_for_calls (&probe_a.requests, 1, 2000));
    TEST_ASSERT_TRUE (wait_for_calls (&probe_b.requests, 1, 2000));
    TEST_ASSERT_EQUAL_STRING ("msg-a", probe_a.payload);
    TEST_ASSERT_EQUAL_STRING ("msg-b", probe_b.payload);

    destroy_server_gateway (&server_b);
    destroy_server_gateway (&server_a);
    TEST_ASSERT_SUCCESS_ERRNO (zlink_gateway_destroy (&client));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_discovery_destroy (&discovery));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_registry_destroy (&registry));
    g_probe_a = NULL;
    g_probe_b = NULL;
}

static void test_gateway_refresh_on_update ()
{
    step_log ("test_gateway_refresh_on_update");
    void *ctx = get_test_context ();
    TEST_ASSERT_NOT_NULL (ctx);

    void *registry = NULL;
    setup_registry (ctx, &registry, "inproc://reg-pub-gateway-update",
                    "inproc://reg-router-gateway-update");
    msleep (100);

    void *discovery =
      zlink_discovery_new_typed (ctx, ZLINK_SERVICE_TYPE_GATEWAY);
    TEST_ASSERT_NOT_NULL (discovery);
    TEST_ASSERT_SUCCESS_ERRNO (zlink_discovery_connect_registry (
      discovery, "inproc://reg-router-gateway-update"));
    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_discovery_subscribe (discovery, "svc-update"));
    void *client =
      zlink_gateway_new (ctx, discovery, NULL, &discard_gateway_message);
    TEST_ASSERT_NOT_NULL (client);

    gateway_probe_t probe1;
    gateway_probe_t probe2;
    g_probe_a = &probe1;
    g_probe_b = &probe2;
    gateway_server_t server1;
    gateway_server_t server2;
    create_server_gateway (&server1, ctx, "inproc://reg-router-gateway-update",
                           "UP1");
    char ep1[256];
    snprintf (ep1, sizeof (ep1), "tcp://127.0.0.1:%d", test_port (22520));
    bind_register_server (&server1, ep1, "svc-update", 1, &gateway_handler_a);

    wait_gateway_ready (client, "svc-update", 2000);
    send_gateway_with_timeout (client, "svc-update", "one", 2000);
    TEST_ASSERT_TRUE (wait_for_calls (&probe1.requests, 1, 2000));

    TEST_ASSERT_SUCCESS_ERRNO (zlink_gateway_unregister (server1.gateway,
                                                         "svc-update"));
    msleep (300);

    create_server_gateway (&server2, ctx, "inproc://reg-router-gateway-update",
                           "UP2");
    char ep2[256];
    snprintf (ep2, sizeof (ep2), "tcp://127.0.0.1:%d", test_port (22521));
    bind_register_server (&server2, ep2, "svc-update", 1, &gateway_handler_b);

    wait_gateway_ready (client, "svc-update", 2000);
    send_gateway_with_timeout (client, "svc-update", "two", 2000);
    TEST_ASSERT_TRUE (wait_for_calls (&probe2.requests, 1, 2000));
    TEST_ASSERT_EQUAL_INT (1, probe1.requests.load ());

    destroy_server_gateway (&server2);
    destroy_server_gateway (&server1);
    TEST_ASSERT_SUCCESS_ERRNO (zlink_gateway_destroy (&client));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_discovery_destroy (&discovery));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_registry_destroy (&registry));
    g_probe_a = NULL;
    g_probe_b = NULL;
}

static void test_gateway_protocol_ws ()
{
    step_log ("test_gateway_protocol_ws");
    if (!zlink_has ("ws")) {
        TEST_IGNORE_MESSAGE ("WebSocket not available");
        return;
    }

    void *ctx = get_test_context ();
    TEST_ASSERT_NOT_NULL (ctx);

    void *registry = NULL;
    setup_registry (ctx, &registry, "inproc://reg-pub-gateway-ws",
                    "inproc://reg-router-gateway-ws");
    msleep (100);

    void *discovery =
      zlink_discovery_new_typed (ctx, ZLINK_SERVICE_TYPE_GATEWAY);
    TEST_ASSERT_NOT_NULL (discovery);
    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_discovery_connect_registry (discovery, "inproc://reg-router-gateway-ws"));
    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_discovery_subscribe (discovery, "svc-ws"));
    void *client =
      zlink_gateway_new (ctx, discovery, NULL, &discard_gateway_message);
    TEST_ASSERT_NOT_NULL (client);

    gateway_probe_t probe;
    g_probe_a = &probe;
    gateway_server_t server;
    create_server_gateway (&server, ctx, "inproc://reg-router-gateway-ws",
                           "PROVWS");
    char endpoint[256];
    snprintf (endpoint, sizeof (endpoint), "ws://127.0.0.1:%d", test_port (22530));
    bind_register_server (&server, endpoint, "svc-ws", 1, &gateway_handler_a);

    wait_gateway_ready (client, "svc-ws", 2000);
    send_gateway_with_timeout (client, "svc-ws", "ws-test", 2000);
    TEST_ASSERT_TRUE (wait_for_calls (&probe.requests, 1, 2000));

    destroy_server_gateway (&server);
    TEST_ASSERT_SUCCESS_ERRNO (zlink_gateway_destroy (&client));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_discovery_destroy (&discovery));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_registry_destroy (&registry));
    g_probe_a = NULL;
}

static void test_gateway_protocol_tls ()
{
    step_log ("test_gateway_protocol_tls");
    if (!zlink_has ("tls")) {
        TEST_IGNORE_MESSAGE ("TLS not available");
        return;
    }

    const tls_test_files_t files = make_tls_test_files ();
    void *ctx = get_test_context ();
    TEST_ASSERT_NOT_NULL (ctx);

    void *registry = NULL;
    setup_registry (ctx, &registry, "inproc://reg-pub-gateway-tls",
                    "inproc://reg-router-gateway-tls");
    msleep (100);

    void *discovery =
      zlink_discovery_new_typed (ctx, ZLINK_SERVICE_TYPE_GATEWAY);
    TEST_ASSERT_NOT_NULL (discovery);
    TEST_ASSERT_SUCCESS_ERRNO (zlink_discovery_connect_registry (
      discovery, "inproc://reg-router-gateway-tls"));
    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_discovery_subscribe (discovery, "svc-tls"));
    void *client =
      zlink_gateway_new (ctx, discovery, NULL, &discard_gateway_message);
    TEST_ASSERT_NOT_NULL (client);
    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_gateway_set_tls_client (client, files.ca_cert.c_str (), "localhost", 0));

    gateway_probe_t probe;
    g_probe_a = &probe;
    gateway_server_t server;
    create_server_gateway (&server, ctx, "inproc://reg-router-gateway-tls",
                           "PROVTLS");
    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_gateway_set_tls_server (server.gateway, files.server_cert.c_str (),
                                    files.server_key.c_str ()));
    char endpoint[256];
    snprintf (endpoint, sizeof (endpoint), "tls://127.0.0.1:%d",
              test_port (22531));
    bind_register_server (&server, endpoint, "svc-tls", 1, &gateway_handler_a);

    wait_gateway_ready (client, "svc-tls", 2000);
    send_gateway_with_timeout (client, "svc-tls", "tls-test", 2000);
    TEST_ASSERT_TRUE (wait_for_calls (&probe.requests, 1, 2000));

    destroy_server_gateway (&server);
    TEST_ASSERT_SUCCESS_ERRNO (zlink_gateway_destroy (&client));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_discovery_destroy (&discovery));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_registry_destroy (&registry));
    cleanup_tls_test_files (files);
    g_probe_a = NULL;
}

static void test_gateway_protocol_wss ()
{
    step_log ("test_gateway_protocol_wss");
    if (!zlink_has ("wss")) {
        TEST_IGNORE_MESSAGE ("WSS not available");
        return;
    }

    const tls_test_files_t files = make_tls_test_files ();
    void *ctx = get_test_context ();
    TEST_ASSERT_NOT_NULL (ctx);

    void *registry = NULL;
    setup_registry (ctx, &registry, "inproc://reg-pub-gateway-wss",
                    "inproc://reg-router-gateway-wss");
    msleep (100);

    void *discovery =
      zlink_discovery_new_typed (ctx, ZLINK_SERVICE_TYPE_GATEWAY);
    TEST_ASSERT_NOT_NULL (discovery);
    TEST_ASSERT_SUCCESS_ERRNO (zlink_discovery_connect_registry (
      discovery, "inproc://reg-router-gateway-wss"));
    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_discovery_subscribe (discovery, "svc-wss"));
    void *client =
      zlink_gateway_new (ctx, discovery, NULL, &discard_gateway_message);
    TEST_ASSERT_NOT_NULL (client);
    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_gateway_set_tls_client (client, files.ca_cert.c_str (), "localhost", 0));

    gateway_probe_t probe;
    g_probe_a = &probe;
    gateway_server_t server;
    create_server_gateway (&server, ctx, "inproc://reg-router-gateway-wss",
                           "PROVWSS");
    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_gateway_set_tls_server (server.gateway, files.server_cert.c_str (),
                                    files.server_key.c_str ()));
    char endpoint[256];
    snprintf (endpoint, sizeof (endpoint), "wss://127.0.0.1:%d",
              test_port (22532));
    bind_register_server (&server, endpoint, "svc-wss", 1, &gateway_handler_a);

    wait_gateway_ready (client, "svc-wss", 2000);
    send_gateway_with_timeout (client, "svc-wss", "wss-test", 2000);
    TEST_ASSERT_TRUE (wait_for_calls (&probe.requests, 1, 2000));

    destroy_server_gateway (&server);
    TEST_ASSERT_SUCCESS_ERRNO (zlink_gateway_destroy (&client));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_discovery_destroy (&discovery));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_registry_destroy (&registry));
    cleanup_tls_test_files (files);
    g_probe_a = NULL;
}

static void test_gateway_load_balancing ()
{
    step_log ("test_gateway_load_balancing");
    void *ctx = get_test_context ();
    TEST_ASSERT_NOT_NULL (ctx);

    void *registry = NULL;
    setup_registry (ctx, &registry, "inproc://reg-pub-lb",
                    "inproc://reg-router-lb");
    msleep (100);

    void *discovery =
      zlink_discovery_new_typed (ctx, ZLINK_SERVICE_TYPE_GATEWAY);
    TEST_ASSERT_NOT_NULL (discovery);
    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_discovery_connect_registry (discovery, "inproc://reg-router-lb"));
    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_discovery_subscribe (discovery, "lb-svc"));
    void *client =
      zlink_gateway_new (ctx, discovery, NULL, &discard_gateway_message);
    TEST_ASSERT_NOT_NULL (client);

    gateway_probe_t probe1;
    gateway_probe_t probe2;
    g_probe_a = &probe1;
    g_probe_b = &probe2;
    gateway_server_t server1;
    gateway_server_t server2;
    create_server_gateway (&server1, ctx, "inproc://reg-router-lb", "PROV1");
    create_server_gateway (&server2, ctx, "inproc://reg-router-lb", "PROV2");
    char ep1[256];
    char ep2[256];
    snprintf (ep1, sizeof (ep1), "tcp://127.0.0.1:%d", test_port (22540));
    snprintf (ep2, sizeof (ep2), "tcp://127.0.0.1:%d", test_port (22541));
    bind_register_server (&server1, ep1, "lb-svc", 10, &gateway_handler_a);
    bind_register_server (&server2, ep2, "lb-svc", 10, &gateway_handler_b);

    wait_gateway_ready (client, "lb-svc", 2000);
    for (int i = 0; i < 12; ++i)
        send_gateway_with_timeout (client, "lb-svc", "rr", 2000);

    TEST_ASSERT_TRUE (wait_for_calls (&probe1.requests, 1, 2000));
    TEST_ASSERT_TRUE (wait_for_calls (&probe2.requests, 1, 2000));
    TEST_ASSERT_EQUAL_INT (12, probe1.requests.load () + probe2.requests.load ());

    destroy_server_gateway (&server2);
    destroy_server_gateway (&server1);
    TEST_ASSERT_SUCCESS_ERRNO (zlink_gateway_destroy (&client));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_discovery_destroy (&discovery));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_registry_destroy (&registry));
    g_probe_a = NULL;
    g_probe_b = NULL;
}

static void test_gateway_weighted_load_balancing ()
{
    step_log ("test_gateway_weighted_load_balancing");
    void *ctx = get_test_context ();
    TEST_ASSERT_NOT_NULL (ctx);

    void *registry = NULL;
    setup_registry (ctx, &registry, "inproc://reg-pub-lb-weighted",
                    "inproc://reg-router-lb-weighted");
    msleep (100);

    void *discovery =
      zlink_discovery_new_typed (ctx, ZLINK_SERVICE_TYPE_GATEWAY);
    TEST_ASSERT_NOT_NULL (discovery);
    TEST_ASSERT_SUCCESS_ERRNO (zlink_discovery_connect_registry (
      discovery, "inproc://reg-router-lb-weighted"));
    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_discovery_subscribe (discovery, "lb-weighted"));
    void *client =
      zlink_gateway_new (ctx, discovery, NULL, &discard_gateway_message);
    TEST_ASSERT_NOT_NULL (client);
    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_gateway_set_lb_strategy (client, "lb-weighted",
                                     ZLINK_GATEWAY_LB_WEIGHTED));

    gateway_probe_t probe1;
    gateway_probe_t probe2;
    g_probe_a = &probe1;
    g_probe_b = &probe2;
    gateway_server_t server1;
    gateway_server_t server2;
    create_server_gateway (&server1, ctx, "inproc://reg-router-lb-weighted",
                           "WPROV1");
    create_server_gateway (&server2, ctx, "inproc://reg-router-lb-weighted",
                           "WPROV2");
    char ep1[256];
    char ep2[256];
    snprintf (ep1, sizeof (ep1), "tcp://127.0.0.1:%d", test_port (22542));
    snprintf (ep2, sizeof (ep2), "tcp://127.0.0.1:%d", test_port (22543));
    bind_register_server (&server1, ep1, "lb-weighted", 8, &gateway_handler_a);
    bind_register_server (&server2, ep2, "lb-weighted", 1, &gateway_handler_b);

    wait_gateway_ready (client, "lb-weighted", 2000);
    for (int i = 0; i < 27; ++i)
        send_gateway_with_timeout (client, "lb-weighted", "wt", 2000);

    TEST_ASSERT_TRUE (wait_for_calls (&probe1.requests, 1, 2000));
    TEST_ASSERT_TRUE (wait_for_calls (&probe2.requests, 1, 2000));
    TEST_ASSERT_EQUAL_INT (27, probe1.requests.load () + probe2.requests.load ());
    TEST_ASSERT_TRUE (probe1.requests.load () > probe2.requests.load ());

    destroy_server_gateway (&server2);
    destroy_server_gateway (&server1);
    TEST_ASSERT_SUCCESS_ERRNO (zlink_gateway_destroy (&client));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_discovery_destroy (&discovery));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_registry_destroy (&registry));
    g_probe_a = NULL;
    g_probe_b = NULL;
}

static void test_gateway_concurrent_send_and_updates ()
{
    step_log ("test_gateway_concurrent_send_and_updates");
    void *ctx = get_test_context ();
    TEST_ASSERT_NOT_NULL (ctx);

    void *registry = NULL;
    setup_registry (ctx, &registry, "inproc://reg-pub-gateway-sync",
                    "inproc://reg-router-gateway-sync");
    msleep (100);

    void *discovery =
      zlink_discovery_new_typed (ctx, ZLINK_SERVICE_TYPE_GATEWAY);
    TEST_ASSERT_NOT_NULL (discovery);
    TEST_ASSERT_SUCCESS_ERRNO (zlink_discovery_connect_registry (
      discovery, "inproc://reg-router-gateway-sync"));
    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_discovery_subscribe (discovery, "svc-sync"));
    void *client =
      zlink_gateway_new (ctx, discovery, NULL, &discard_gateway_message);
    TEST_ASSERT_NOT_NULL (client);

    gateway_probe_t probe;
    g_probe_a = &probe;
    gateway_server_t server;
    create_server_gateway (&server, ctx, "inproc://reg-router-gateway-sync",
                           "SYNC");
    char endpoint[256];
    snprintf (endpoint, sizeof (endpoint), "tcp://127.0.0.1:%d", test_port (22544));
    bind_register_server (&server, endpoint, "svc-sync", 1, &gateway_handler_a);

    wait_gateway_ready (client, "svc-sync", 2000);

    const int send_threads = 4;
    const int send_per_thread = 50;
    std::atomic<int> send_ok (0);
    std::atomic<int> send_fail (0);
    std::vector<void *> threads;
    std::vector<send_worker_args_t> args (send_threads);

    for (int i = 0; i < send_threads; ++i) {
        args[i].gateway = client;
        args[i].service_name = "svc-sync";
        args[i].count = send_per_thread;
        args[i].ok = &send_ok;
        args[i].fail = &send_fail;
        threads.push_back (zlink_thread_start (&send_worker, &args[i]));
    }

    std::thread updater ([&] () {
        for (int i = 0; i < 50; ++i) {
            update_gateway_weight_with_timeout (server.gateway, "svc-sync",
                                                (i % 2) + 1, 100);
            msleep (1);
        }
    });

    for (size_t i = 0; i < threads.size (); ++i)
        zlink_thread_join (threads[i]);
    updater.join ();

    TEST_ASSERT_EQUAL_INT (0, send_fail.load ());
    TEST_ASSERT_TRUE (wait_for_calls (&probe.requests, send_ok.load (), 5000));

    destroy_server_gateway (&server);
    TEST_ASSERT_SUCCESS_ERRNO (zlink_gateway_destroy (&client));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_discovery_destroy (&discovery));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_registry_destroy (&registry));
    g_probe_a = NULL;
}

int main (void)
{
    UNITY_BEGIN ();
    RUN_TEST (test_gateway_provider_setsockopt);
    RUN_TEST (test_gateway_can_be_polled_via_service_instance);
    RUN_TEST (test_gateway_refreshes_existing_service_on_first_connection_count);
    RUN_TEST (test_gateway_router_peers_do_not_enter_pollable_mode);
    RUN_TEST (test_gateway_single_service_tcp);
    RUN_TEST (test_gateway_send_rid_tcp);
    RUN_TEST (test_gateway_multi_service_tcp);
    RUN_TEST (test_gateway_refresh_on_update);
    RUN_TEST (test_gateway_protocol_ws);
    RUN_TEST (test_gateway_protocol_tls);
    RUN_TEST (test_gateway_protocol_wss);
    RUN_TEST (test_gateway_load_balancing);
    RUN_TEST (test_gateway_weighted_load_balancing);
    RUN_TEST (test_gateway_concurrent_send_and_updates);
    return UNITY_END ();
}
