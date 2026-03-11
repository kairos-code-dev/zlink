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
    gateway_probe_t () : requests (0)
    {
        memset (payload, 0, sizeof (payload));
    }

    std::atomic<int> requests;
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
std::atomic<int> *g_ready_calls = NULL;
void *g_ready_subject = NULL;

void gateway_ready_handler (void *subject_)
{
    g_ready_subject = subject_;
    if (g_ready_calls)
        g_ready_calls->fetch_add (1);
}

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

void discard_gateway_message (const zlink_routing_id_t *,
                              zlink_msg_t *parts_,
                              size_t part_count_)
{
    close_parts (parts_, part_count_);
}

void *create_gateway_attached (void *ctx_,
                               void *discovery_,
                               const char *service_name_,
                               const char *routing_id_,
                               zlink_socket_msg_handler_fn handler_)
{
    void *gateway =
      zlink_gateway_new (ctx_, service_name_, routing_id_, handler_);
    if (!gateway)
        return NULL;
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

void record_gateway_message (gateway_probe_t *probe_,
                             const zlink_routing_id_t *,
                             zlink_msg_t *parts_,
                             size_t part_count_)
{
    if (!probe_) {
        discard_gateway_message (NULL, parts_, part_count_);
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

    close_parts (parts_, part_count_);
}

void gateway_handler_a (const zlink_routing_id_t *source_rid_,
                        zlink_msg_t *parts_,
                        size_t part_count_)
{
    record_gateway_message (g_probe_a, source_rid_, parts_, part_count_);
}

void gateway_handler_b (const zlink_routing_id_t *source_rid_,
                        zlink_msg_t *parts_,
                        size_t part_count_)
{
    record_gateway_message (g_probe_b, source_rid_, parts_, part_count_);
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

bool wait_for_total_calls (std::atomic<int> *counter_a_,
                           std::atomic<int> *counter_b_,
                           int expected_,
                           int timeout_ms_)
{
    const int step_ms = 10;
    const int attempts = timeout_ms_ / step_ms;
    for (int i = 0; i < attempts; ++i) {
        if (counter_a_->load () + counter_b_->load () >= expected_)
            return true;
        msleep (step_ms);
    }
    if (getenv ("ZLINK_TEST_DEBUG")) {
        char buffer[128];
        snprintf (buffer, sizeof (buffer),
                  "wait_for_total_calls timeout: a=%d b=%d expected=%d",
                  counter_a_->load (), counter_b_->load (), expected_);
        step_log (buffer);
    }
    return counter_a_->load () + counter_b_->load () >= expected_;
}

void wait_gateway_ready (void *gateway_,
                         int timeout_ms_)
{
    const int step_ms = 10;
    const int attempts = timeout_ms_ / step_ms;
    for (int i = 0; i < attempts; ++i) {
        if (zlink_gateway_connection_count (gateway_) > 0)
            return;
        msleep (step_ms);
    }
    TEST_FAIL_MESSAGE ("gateway connection timeout");
}

void wait_gateway_connections (void *gateway_,
                               int expected_,
                               int timeout_ms_)
{
    const int step_ms = 10;
    const int attempts = timeout_ms_ / step_ms;
    for (int i = 0; i < attempts; ++i) {
        if (zlink_gateway_connection_count (gateway_) >= expected_)
            return;
        msleep (step_ms);
    }
    TEST_FAIL_MESSAGE ("gateway expected connection count timeout");
}

void wait_gateway_peer_weights (void *gateway_,
                                size_t expected_count_,
                                uint32_t expected_weight_sum_,
                                int timeout_ms_)
{
    const int step_ms = 10;
    const int attempts = timeout_ms_ / step_ms;
    for (int i = 0; i < attempts; ++i) {
        size_t count = 0;
        if (zlink_gateway_router_peers (gateway_, NULL, &count) == 0
            && count == expected_count_) {
            std::vector<zlink_gateway_peer_info_t> peers (count);
            size_t filled = count;
            if (count == 0
                || zlink_gateway_router_peers (gateway_, &peers[0], &filled)
                     == 0) {
                uint32_t sum = 0;
                for (size_t pi = 0; pi < filled; ++pi)
                    sum += peers[pi].weight == 0 ? 1 : peers[pi].weight;
                if (filled == expected_count_ && sum == expected_weight_sum_)
                    return;
            }
        }
        msleep (step_ms);
    }
    TEST_FAIL_MESSAGE ("gateway expected peer weight timeout");
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

void update_gateway_weight_with_timeout (void *gateway_,
                                         uint32_t weight_,
                                         int timeout_ms_)
{
    zlink_routing_id_t rid;
    memset (&rid, 0, sizeof (rid));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_gateway_routing_id (gateway_, &rid));

    const int step_ms = 10;
    const int attempts = timeout_ms_ / step_ms;
    for (int i = 0; i < attempts; ++i) {
        if (zlink_gateway_update_peer_weight (gateway_, &rid, weight_) == 0)
            return;
        if (errno != EAGAIN && errno != ENOENT)
            break;
        msleep (step_ms);
    }
    TEST_FAIL_MESSAGE ("gateway update timeout");
}

bool try_update_gateway_weight_with_timeout (void *gateway_,
                                             uint32_t weight_,
                                             int timeout_ms_)
{
    zlink_routing_id_t rid;
    memset (&rid, 0, sizeof (rid));
    if (zlink_gateway_routing_id (gateway_, &rid) != 0)
        return false;

    const int step_ms = 10;
    const int attempts = timeout_ms_ / step_ms;
    for (int i = 0; i < attempts; ++i) {
        if (zlink_gateway_update_peer_weight (gateway_, &rid, weight_) == 0)
            return true;
        if (errno != EAGAIN && errno != ENOENT)
            break;
        msleep (step_ms);
    }
    return false;
}

void send_gateway_with_timeout (void *gateway_,
                                const char *payload_,
                                int timeout_ms_)
{
    const int step_ms = 2;
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

void send_gateway_rid_with_timeout (void *gateway_,
                                    const zlink_routing_id_t *rid_,
                                    const char *payload_,
                                    int timeout_ms_)
{
    const int step_ms = 2;
    const int attempts = timeout_ms_ / step_ms;
    for (int i = 0; i < attempts; ++i) {
        zlink_msg_t part;
        TEST_ASSERT_SUCCESS_ERRNO (
          zlink_msg_init_size (&part, strlen (payload_)));
        memcpy (zlink_msg_data (&part), payload_, strlen (payload_));
        if (zlink_gateway_send_rid (gateway_, rid_, &part, 1, ZLINK_DONTWAIT)
            == 0)
            return;
        TEST_ASSERT_SUCCESS_ERRNO (zlink_msg_close (&part));
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

void create_server_gateway (gateway_server_t *server_,
                            void *ctx_,
                            const char *registry_ep_,
                            const char *service_name_,
                            const char *routing_id_,
                            zlink_socket_msg_handler_fn handler_)
{
    server_->discovery =
      zlink_discovery_new (ctx_, ZLINK_SERVICE_TYPE_GATEWAY);
    TEST_ASSERT_NOT_NULL (server_->discovery);
    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_discovery_connect_registry (server_->discovery, registry_ep_));
    server_->gateway =
      create_gateway_attached (ctx_, server_->discovery, service_name_, routing_id_,
                         handler_);
    TEST_ASSERT_NOT_NULL (server_->gateway);
}

void bind_register_server (gateway_server_t *server_,
                           const char *endpoint_,
                           uint32_t weight_,
                           zlink_socket_msg_handler_fn handler_)
{
    (void) handler_;
    step_log ("bind_register_server: bind begin");
    bind_gateway_with_timeout (server_->gateway, endpoint_, 3000);
    step_log ("bind_register_server: bind end");
    if (weight_ != 1) {
        step_log ("bind_register_server: weight update begin");
        update_gateway_weight_with_timeout (server_->gateway, weight_, 3000);
        step_log ("bind_register_server: weight update end");
    }
}

void destroy_server_gateway (gateway_server_t *server_)
{
    if (server_->gateway) {
        step_log ("destroy_server_gateway: gateway destroy begin");
        TEST_ASSERT_SUCCESS_ERRNO (zlink_gateway_destroy (&server_->gateway));
        step_log ("destroy_server_gateway: gateway destroy end");
    }
    if (server_->discovery) {
        step_log ("destroy_server_gateway: discovery destroy begin");
        TEST_ASSERT_SUCCESS_ERRNO (zlink_discovery_destroy (&server_->discovery));
        step_log ("destroy_server_gateway: discovery destroy end");
    }
}

bool should_run_named_test (const char *name_)
{
    const char *selected = getenv ("ZLINK_TEST_CASE");
    return !selected || !*selected || strcmp (selected, name_) == 0;
}

bool should_run_gateway_concurrent_stress ()
{
    const char *enabled = getenv ("ZLINK_RUN_GATEWAY_CONCURRENT_STRESS");
    return enabled && enabled[0] != '\0' && strcmp (enabled, "0") != 0;
}

void *create_client_gateway (void *ctx_,
                             const char *registry_ep_,
                             const char *service_name_,
                             const char *routing_id_)
{
    void *discovery =
      zlink_discovery_new (ctx_, ZLINK_SERVICE_TYPE_GATEWAY);
    TEST_ASSERT_NOT_NULL (discovery);
    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_discovery_connect_registry (discovery, registry_ep_));
    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_discovery_subscribe (discovery, service_name_));

    void *gateway =
      create_gateway_attached (ctx_, discovery, service_name_, routing_id_,
                         &discard_gateway_message);
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
            zlink_msg_t part;
            if (zlink_msg_init_size (&part, 4) != 0)
                break;
            memcpy (zlink_msg_data (&part), payload, 4);
            rc = zlink_gateway_send (args->gateway, &part, 1, ZLINK_DONTWAIT);
            if (rc != 0)
                (void) zlink_msg_close (&part);
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
      zlink_discovery_new (ctx, ZLINK_SERVICE_TYPE_GATEWAY);
    TEST_ASSERT_NOT_NULL (client_discovery);
    void *client =
      create_gateway_attached (ctx, client_discovery, "opts-client", NULL,
                         &discard_gateway_message);
    TEST_ASSERT_NOT_NULL (client);

    gateway_server_t server;
    server.discovery =
      zlink_discovery_new (ctx, ZLINK_SERVICE_TYPE_GATEWAY);
    TEST_ASSERT_NOT_NULL (server.discovery);
    server.gateway = create_gateway_attached (ctx, server.discovery, "opts-server",
                                        NULL, &discard_gateway_message);
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

    std::atomic<int> ready_calls (0);
    g_ready_calls = &ready_calls;
    g_ready_subject = NULL;

    void *gateway = zlink_gateway_new (ctx, "ready-svc", "gw-ready",
                                       &discard_gateway_message);
    TEST_ASSERT_NOT_NULL (gateway);

    const int hwm = 2;
    TEST_ASSERT_SUCCESS_ERRNO (zlink_gateway_set_option (
      gateway, ZLINK_GATEWAY_OPT_SNDHWM, &hwm, sizeof (hwm)));
    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_gateway_set_send_ready_handler (gateway, &gateway_ready_handler));
    step_log ("gateway ready handler installed");

    char endpoint[MAX_SOCKET_STRING];
    snprintf (endpoint, sizeof (endpoint), "tcp://127.0.0.1:%d",
              test_port (22400));
    bind_gateway_with_timeout (gateway, endpoint, 2000);
    step_log ("gateway bound");
    TEST_ASSERT_EQUAL_INT (0, ready_calls.load ());

    TEST_ASSERT_SUCCESS_ERRNO (zlink_gateway_destroy (&gateway));
    g_ready_calls = NULL;
    g_ready_subject = NULL;
}

static void test_gateway_refreshes_existing_service_on_first_connection_count ()
{
    step_log ("test_gateway_refreshes_existing_service_on_first_connection_count");
    void *ctx = get_test_context ();
    TEST_ASSERT_NOT_NULL (ctx);

    void *registry = NULL;
    char registry_pub[MAX_SOCKET_STRING];
    char registry_router[MAX_SOCKET_STRING];
    make_registry_endpoint (registry_pub, sizeof (registry_pub), 25902);
    make_registry_endpoint (registry_router, sizeof (registry_router), 25903);
    setup_registry (ctx, &registry, registry_pub, registry_router);

    gateway_server_t server;
    create_server_gateway (&server, ctx, registry_router,
                           "late-gateway-svc", "late-server",
                           &discard_gateway_message);
    char endpoint[MAX_SOCKET_STRING];
    snprintf (endpoint, sizeof (endpoint), "tcp://127.0.0.1:%d",
              test_port (22402));
    bind_register_server (&server, endpoint, 1,
                          &discard_gateway_message);

    void *discovery =
      zlink_discovery_new (ctx, ZLINK_SERVICE_TYPE_GATEWAY);
    TEST_ASSERT_NOT_NULL (discovery);
    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_discovery_connect_registry (discovery, registry_router));
    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_discovery_subscribe (discovery, "late-gateway-svc"));
    void *client =
      create_gateway_attached (ctx, discovery, "late-gateway-svc", NULL,
                         &discard_gateway_message);
    TEST_ASSERT_NOT_NULL (client);

    wait_gateway_ready (client, 2000);
    TEST_ASSERT_TRUE (zlink_gateway_connection_count (client) > 0);

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
    char registry_pub[MAX_SOCKET_STRING];
    char registry_router[MAX_SOCKET_STRING];
    make_registry_endpoint (registry_pub, sizeof (registry_pub), 25904);
    make_registry_endpoint (registry_router, sizeof (registry_router), 25905);
    setup_registry (ctx, &registry, registry_pub, registry_router);

    void *discovery =
      zlink_discovery_new (ctx, ZLINK_SERVICE_TYPE_GATEWAY);
    TEST_ASSERT_NOT_NULL (discovery);
    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_discovery_connect_registry (discovery, registry_router));
    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_discovery_subscribe (discovery, "peer-stats-svc"));
    void *client =
      create_gateway_attached (ctx, discovery, "peer-stats-svc", NULL,
                         &discard_gateway_message);
    TEST_ASSERT_NOT_NULL (client);

    gateway_probe_t probe;
    g_probe_a = &probe;
    gateway_server_t server;
    create_server_gateway (&server, ctx, registry_router,
                           "peer-stats-svc", "peer-server",
                           &gateway_handler_a);
    char endpoint[MAX_SOCKET_STRING];
    snprintf (endpoint, sizeof (endpoint), "tcp://127.0.0.1:%d",
              test_port (22403));
    bind_register_server (&server, endpoint, 1,
                          &gateway_handler_a);

    wait_gateway_ready (client, 2000);

    size_t count = 0;
    TEST_ASSERT_SUCCESS_ERRNO (zlink_gateway_router_peers (client, NULL, &count));
    TEST_ASSERT_TRUE (count > 0);

    send_gateway_with_timeout (client, "ping", 2000);
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
    char registry_pub[MAX_SOCKET_STRING];
    char registry_router[MAX_SOCKET_STRING];
    make_registry_endpoint (registry_pub, sizeof (registry_pub), 25906);
    make_registry_endpoint (registry_router, sizeof (registry_router), 25907);
    setup_registry (ctx, &registry, registry_pub, registry_router);

    void *discovery =
      zlink_discovery_new (ctx, ZLINK_SERVICE_TYPE_GATEWAY);
    TEST_ASSERT_NOT_NULL (discovery);
    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_discovery_connect_registry (discovery, registry_router));
    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_discovery_subscribe (discovery, "svc"));
    void *client =
      create_gateway_attached (ctx, discovery, "svc", NULL, &discard_gateway_message);
    TEST_ASSERT_NOT_NULL (client);

    gateway_probe_t probe;
    g_probe_a = &probe;
    gateway_server_t server;
    create_server_gateway (&server, ctx, registry_router, "svc",
                           "PROV1", &gateway_handler_a);
    char endpoint[256];
    snprintf (endpoint, sizeof (endpoint), "tcp://127.0.0.1:%d", test_port (22500));
    bind_register_server (&server, endpoint, 1, &gateway_handler_a);

    wait_gateway_ready (client, 2000);
    send_gateway_with_timeout (client, "hello", 2000);
    TEST_ASSERT_TRUE (wait_for_calls (&probe.requests, 1, 2000));
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
    char registry_pub[MAX_SOCKET_STRING];
    char registry_router[MAX_SOCKET_STRING];
    make_registry_endpoint (registry_pub, sizeof (registry_pub), 25908);
    make_registry_endpoint (registry_router, sizeof (registry_router), 25909);
    setup_registry (ctx, &registry, registry_pub, registry_router);

    void *discovery =
      zlink_discovery_new (ctx, ZLINK_SERVICE_TYPE_GATEWAY);
    TEST_ASSERT_NOT_NULL (discovery);
    TEST_ASSERT_SUCCESS_ERRNO (zlink_discovery_connect_registry (
      discovery, registry_router));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_discovery_subscribe (discovery, "svc"));
    void *client =
      create_gateway_attached (ctx, discovery, "svc", NULL, &discard_gateway_message);
    TEST_ASSERT_NOT_NULL (client);

    gateway_probe_t probe1;
    gateway_probe_t probe2;
    g_probe_a = &probe1;
    g_probe_b = &probe2;
    gateway_server_t server1;
    gateway_server_t server2;
    create_server_gateway (&server1, ctx, registry_router,
                           "svc", "PROV-A", &gateway_handler_a);
    create_server_gateway (&server2, ctx, registry_router,
                           "svc", "PROV-B", &gateway_handler_b);

    char ep1[256];
    char ep2[256];
    snprintf (ep1, sizeof (ep1), "tcp://127.0.0.1:%d", test_port (22501));
    snprintf (ep2, sizeof (ep2), "tcp://127.0.0.1:%d", test_port (22502));
    bind_register_server (&server1, ep1, 1, &gateway_handler_a);
    bind_register_server (&server2, ep2, 1, &gateway_handler_b);

    wait_gateway_connections (client, 2, 3000);
    zlink_routing_id_t rid;
    memset (&rid, 0, sizeof (rid));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_gateway_routing_id (server2.gateway, &rid));
    send_gateway_rid_with_timeout (client, &rid, "rid-msg", 2000);
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
    char registry_pub[MAX_SOCKET_STRING];
    char registry_router[MAX_SOCKET_STRING];
    make_registry_endpoint (registry_pub, sizeof (registry_pub), 25910);
    make_registry_endpoint (registry_router, sizeof (registry_router), 25911);
    setup_registry (ctx, &registry, registry_pub, registry_router);

    void *discovery_a =
      zlink_discovery_new (ctx, ZLINK_SERVICE_TYPE_GATEWAY);
    void *discovery_b =
      zlink_discovery_new (ctx, ZLINK_SERVICE_TYPE_GATEWAY);
    TEST_ASSERT_NOT_NULL (discovery_a);
    TEST_ASSERT_NOT_NULL (discovery_b);
    TEST_ASSERT_SUCCESS_ERRNO (zlink_discovery_connect_registry (
      discovery_a, registry_router));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_discovery_connect_registry (
      discovery_b, registry_router));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_discovery_subscribe (discovery_a, "svc-A"));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_discovery_subscribe (discovery_b, "svc-B"));
    void *client_a =
      create_gateway_attached (ctx, discovery_a, "svc-A", NULL,
                         &discard_gateway_message);
    void *client_b =
      create_gateway_attached (ctx, discovery_b, "svc-B", NULL,
                         &discard_gateway_message);
    TEST_ASSERT_NOT_NULL (client_a);
    TEST_ASSERT_NOT_NULL (client_b);

    gateway_probe_t probe_a;
    gateway_probe_t probe_b;
    g_probe_a = &probe_a;
    g_probe_b = &probe_b;
    gateway_server_t server_a;
    gateway_server_t server_b;
    create_server_gateway (&server_a, ctx, registry_router,
                           "svc-A", "A", &gateway_handler_a);
    create_server_gateway (&server_b, ctx, registry_router,
                           "svc-B", "B", &gateway_handler_b);
    char ep_a[256];
    char ep_b[256];
    snprintf (ep_a, sizeof (ep_a), "tcp://127.0.0.1:%d", test_port (22510));
    snprintf (ep_b, sizeof (ep_b), "tcp://127.0.0.1:%d", test_port (22511));
    bind_register_server (&server_a, ep_a, 1, &gateway_handler_a);
    bind_register_server (&server_b, ep_b, 1, &gateway_handler_b);

    wait_gateway_ready (client_a, 2000);
    wait_gateway_ready (client_b, 2000);
    send_gateway_with_timeout (client_a, "msg-a", 2000);
    send_gateway_with_timeout (client_b, "msg-b", 2000);
    TEST_ASSERT_TRUE (wait_for_calls (&probe_a.requests, 1, 2000));
    TEST_ASSERT_TRUE (wait_for_calls (&probe_b.requests, 1, 2000));
    TEST_ASSERT_EQUAL_STRING ("msg-a", probe_a.payload);
    TEST_ASSERT_EQUAL_STRING ("msg-b", probe_b.payload);

    destroy_server_gateway (&server_b);
    destroy_server_gateway (&server_a);
    TEST_ASSERT_SUCCESS_ERRNO (zlink_gateway_destroy (&client_b));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_gateway_destroy (&client_a));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_discovery_destroy (&discovery_b));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_discovery_destroy (&discovery_a));
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
    char registry_pub[MAX_SOCKET_STRING];
    char registry_router[MAX_SOCKET_STRING];
    make_registry_endpoint (registry_pub, sizeof (registry_pub), 25912);
    make_registry_endpoint (registry_router, sizeof (registry_router), 25913);
    setup_registry (ctx, &registry, registry_pub, registry_router);

    void *discovery =
      zlink_discovery_new (ctx, ZLINK_SERVICE_TYPE_GATEWAY);
    TEST_ASSERT_NOT_NULL (discovery);
    TEST_ASSERT_SUCCESS_ERRNO (zlink_discovery_connect_registry (
      discovery, registry_router));
    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_discovery_subscribe (discovery, "svc-update"));
    void *client =
      create_gateway_attached (ctx, discovery, "svc-update", NULL,
                         &discard_gateway_message);
    TEST_ASSERT_NOT_NULL (client);

    gateway_probe_t probe1;
    gateway_probe_t probe2;
    g_probe_a = &probe1;
    g_probe_b = &probe2;
    gateway_server_t server1;
    gateway_server_t server2;
    create_server_gateway (&server1, ctx, registry_router,
                           "svc-update", "UP1", &gateway_handler_a);
    char ep1[256];
    snprintf (ep1, sizeof (ep1), "tcp://127.0.0.1:%d", test_port (22520));
    bind_register_server (&server1, ep1, 1, &gateway_handler_a);

    wait_gateway_ready (client, 2000);
    send_gateway_with_timeout (client, "one", 2000);
    TEST_ASSERT_TRUE (wait_for_calls (&probe1.requests, 1, 2000));

    destroy_server_gateway (&server1);
    msleep (300);

    create_server_gateway (&server2, ctx, registry_router,
                           "svc-update", "UP2", &gateway_handler_b);
    char ep2[256];
    snprintf (ep2, sizeof (ep2), "tcp://127.0.0.1:%d", test_port (22521));
    bind_register_server (&server2, ep2, 1, &gateway_handler_b);

    wait_gateway_ready (client, 2000);
    send_gateway_with_timeout (client, "two", 2000);
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
    char registry_pub[MAX_SOCKET_STRING];
    char registry_router[MAX_SOCKET_STRING];
    make_registry_endpoint (registry_pub, sizeof (registry_pub), 25914);
    make_registry_endpoint (registry_router, sizeof (registry_router), 25915);
    setup_registry (ctx, &registry, registry_pub, registry_router);

    void *discovery =
      zlink_discovery_new (ctx, ZLINK_SERVICE_TYPE_GATEWAY);
    TEST_ASSERT_NOT_NULL (discovery);
    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_discovery_connect_registry (discovery, registry_router));
    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_discovery_subscribe (discovery, "svc-ws"));
    void *client =
      create_gateway_attached (ctx, discovery, "svc-ws", NULL,
                         &discard_gateway_message);
    TEST_ASSERT_NOT_NULL (client);

    gateway_probe_t probe;
    g_probe_a = &probe;
    gateway_server_t server;
    create_server_gateway (&server, ctx, registry_router,
                           "svc-ws", "PROVWS", &gateway_handler_a);
    char endpoint[256];
    snprintf (endpoint, sizeof (endpoint), "ws://127.0.0.1:%d", test_port (22530));
    bind_register_server (&server, endpoint, 1, &gateway_handler_a);

    wait_gateway_ready (client, 2000);
    send_gateway_with_timeout (client, "ws-test", 2000);
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
    char registry_pub[MAX_SOCKET_STRING];
    char registry_router[MAX_SOCKET_STRING];
    make_registry_endpoint (registry_pub, sizeof (registry_pub), 25916);
    make_registry_endpoint (registry_router, sizeof (registry_router), 25917);
    setup_registry (ctx, &registry, registry_pub, registry_router);

    void *discovery =
      zlink_discovery_new (ctx, ZLINK_SERVICE_TYPE_GATEWAY);
    TEST_ASSERT_NOT_NULL (discovery);
    TEST_ASSERT_SUCCESS_ERRNO (zlink_discovery_connect_registry (
      discovery, registry_router));
    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_discovery_subscribe (discovery, "svc-tls"));
    void *client =
      create_gateway_attached (ctx, discovery, "svc-tls", NULL,
                         &discard_gateway_message);
    TEST_ASSERT_NOT_NULL (client);
    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_gateway_set_tls_client (client, files.ca_cert.c_str (), "localhost", 0));

    gateway_probe_t probe;
    g_probe_a = &probe;
    gateway_server_t server;
    create_server_gateway (&server, ctx, registry_router,
                           "svc-tls", "PROVTLS", &gateway_handler_a);
    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_gateway_set_tls_server (server.gateway, files.server_cert.c_str (),
                                    files.server_key.c_str ()));
    char endpoint[256];
    snprintf (endpoint, sizeof (endpoint), "tls://127.0.0.1:%d",
              test_port (22531));
    bind_register_server (&server, endpoint, 1, &gateway_handler_a);

    wait_gateway_ready (client, 2000);
    send_gateway_with_timeout (client, "tls-test", 2000);
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
    char registry_pub[MAX_SOCKET_STRING];
    char registry_router[MAX_SOCKET_STRING];
    make_registry_endpoint (registry_pub, sizeof (registry_pub), 25918);
    make_registry_endpoint (registry_router, sizeof (registry_router), 25919);
    setup_registry (ctx, &registry, registry_pub, registry_router);

    void *discovery =
      zlink_discovery_new (ctx, ZLINK_SERVICE_TYPE_GATEWAY);
    TEST_ASSERT_NOT_NULL (discovery);
    TEST_ASSERT_SUCCESS_ERRNO (zlink_discovery_connect_registry (
      discovery, registry_router));
    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_discovery_subscribe (discovery, "svc-wss"));
    void *client =
      create_gateway_attached (ctx, discovery, "svc-wss", NULL,
                         &discard_gateway_message);
    TEST_ASSERT_NOT_NULL (client);
    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_gateway_set_tls_client (client, files.ca_cert.c_str (), "localhost", 0));

    gateway_probe_t probe;
    g_probe_a = &probe;
    gateway_server_t server;
    create_server_gateway (&server, ctx, registry_router,
                           "svc-wss", "PROVWSS", &gateway_handler_a);
    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_gateway_set_tls_server (server.gateway, files.server_cert.c_str (),
                                    files.server_key.c_str ()));
    char endpoint[256];
    snprintf (endpoint, sizeof (endpoint), "wss://127.0.0.1:%d",
              test_port (22532));
    bind_register_server (&server, endpoint, 1, &gateway_handler_a);

    wait_gateway_ready (client, 2000);
    send_gateway_with_timeout (client, "wss-test", 2000);
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
    char registry_pub[MAX_SOCKET_STRING];
    char registry_router[MAX_SOCKET_STRING];
    make_registry_endpoint (registry_pub, sizeof (registry_pub), 25920);
    make_registry_endpoint (registry_router, sizeof (registry_router), 25921);
    setup_registry (ctx, &registry, registry_pub, registry_router);

    void *discovery =
      zlink_discovery_new (ctx, ZLINK_SERVICE_TYPE_GATEWAY);
    TEST_ASSERT_NOT_NULL (discovery);
    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_discovery_connect_registry (discovery, registry_router));
    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_discovery_subscribe (discovery, "lb-svc"));
    void *client =
      create_gateway_attached (ctx, discovery, "lb-svc", NULL,
                         &discard_gateway_message);
    TEST_ASSERT_NOT_NULL (client);

    gateway_probe_t probe1;
    gateway_probe_t probe2;
    g_probe_a = &probe1;
    g_probe_b = &probe2;
    gateway_server_t server1;
    gateway_server_t server2;
    create_server_gateway (&server1, ctx, registry_router, "lb-svc",
                           "PROV1", &gateway_handler_a);
    create_server_gateway (&server2, ctx, registry_router, "lb-svc",
                           "PROV2", &gateway_handler_b);
    char ep1[256];
    char ep2[256];
    snprintf (ep1, sizeof (ep1), "tcp://127.0.0.1:%d", test_port (22540));
    snprintf (ep2, sizeof (ep2), "tcp://127.0.0.1:%d", test_port (22541));
    bind_register_server (&server1, ep1, 10, &gateway_handler_a);
    bind_register_server (&server2, ep2, 10, &gateway_handler_b);

    wait_gateway_connections (client, 2, 3000);
    for (int i = 0; i < 12; ++i)
        send_gateway_with_timeout (client, "rr", 2000);

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
    char registry_pub[MAX_SOCKET_STRING];
    char registry_router[MAX_SOCKET_STRING];
    make_registry_endpoint (registry_pub, sizeof (registry_pub), 25922);
    make_registry_endpoint (registry_router, sizeof (registry_router), 25923);
    setup_registry (ctx, &registry, registry_pub, registry_router);

    void *discovery =
      zlink_discovery_new (ctx, ZLINK_SERVICE_TYPE_GATEWAY);
    TEST_ASSERT_NOT_NULL (discovery);
    TEST_ASSERT_SUCCESS_ERRNO (zlink_discovery_connect_registry (
      discovery, registry_router));
    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_discovery_subscribe (discovery, "lb-weighted"));
    void *client =
      create_gateway_attached (ctx, discovery, "lb-weighted", NULL,
                         &discard_gateway_message);
    TEST_ASSERT_NOT_NULL (client);
    TEST_ASSERT_SUCCESS_ERRNO (zlink_gateway_set_lb_strategy (
      client, ZLINK_GATEWAY_LB_STRATEGY_WEIGHTED));

    gateway_probe_t probe1;
    gateway_probe_t probe2;
    g_probe_a = &probe1;
    g_probe_b = &probe2;
    gateway_server_t server1;
    gateway_server_t server2;
    create_server_gateway (&server1, ctx, registry_router,
                           "lb-weighted", "WPROV1", &gateway_handler_a);
    create_server_gateway (&server2, ctx, registry_router,
                           "lb-weighted", "WPROV2", &gateway_handler_b);
    char ep1[256];
    char ep2[256];
    snprintf (ep1, sizeof (ep1), "tcp://127.0.0.1:%d", test_port (22542));
    snprintf (ep2, sizeof (ep2), "tcp://127.0.0.1:%d", test_port (22543));
    step_log ("test_gateway_weighted_load_balancing: bind server1");
    bind_register_server (&server1, ep1, 8, &gateway_handler_a);
    step_log ("test_gateway_weighted_load_balancing: bind server2");
    bind_register_server (&server2, ep2, 1, &gateway_handler_b);
    if (getenv ("ZLINK_TEST_DEBUG")) {
        zlink_routing_id_t rid1;
        zlink_routing_id_t rid2;
        memset (&rid1, 0, sizeof (rid1));
        memset (&rid2, 0, sizeof (rid2));
        TEST_ASSERT_SUCCESS_ERRNO (zlink_gateway_routing_id (server1.gateway, &rid1));
        TEST_ASSERT_SUCCESS_ERRNO (zlink_gateway_routing_id (server2.gateway, &rid2));
        char buffer[160];
        snprintf (buffer, sizeof (buffer), "weighted server rids: a=%.*s b=%.*s",
                  static_cast<int> (rid1.size),
                  reinterpret_cast<const char *> (rid1.data),
                  static_cast<int> (rid2.size),
                  reinterpret_cast<const char *> (rid2.data));
        step_log (buffer);
    }

    step_log ("test_gateway_weighted_load_balancing: wait ready");
    wait_gateway_connections (client, 2, 3000);
    wait_gateway_peer_weights (client, 2, 9, 3000);
    step_log ("test_gateway_weighted_load_balancing: send batch begin");
    for (int i = 0; i < 27; ++i)
        send_gateway_with_timeout (client, "wt", 2000);
    step_log ("test_gateway_weighted_load_balancing: send batch end");

    step_log ("test_gateway_weighted_load_balancing: wait probe1");
    TEST_ASSERT_TRUE (wait_for_calls (&probe1.requests, 1, 2000));
    step_log ("test_gateway_weighted_load_balancing: wait probe2");
    TEST_ASSERT_TRUE (wait_for_calls (&probe2.requests, 1, 2000));
    TEST_ASSERT_TRUE (wait_for_total_calls (&probe1.requests, &probe2.requests,
                                            27, 5000));
    step_log ("test_gateway_weighted_load_balancing: assertions");
    if (getenv ("ZLINK_TEST_DEBUG")) {
        char buffer[128];
        snprintf (buffer, sizeof (buffer),
                  "test_gateway_weighted_load_balancing: counts a=%d b=%d",
                  probe1.requests.load (), probe2.requests.load ());
        step_log (buffer);
    }
    TEST_ASSERT_EQUAL_INT (27, probe1.requests.load () + probe2.requests.load ());
    TEST_ASSERT_TRUE (probe1.requests.load () > probe2.requests.load ());

    step_log ("test_gateway_weighted_load_balancing: destroy server2");
    destroy_server_gateway (&server2);
    step_log ("test_gateway_weighted_load_balancing: destroy server1");
    destroy_server_gateway (&server1);
    step_log ("test_gateway_weighted_load_balancing: destroy client");
    TEST_ASSERT_SUCCESS_ERRNO (zlink_gateway_destroy (&client));
    step_log ("test_gateway_weighted_load_balancing: destroy discovery");
    TEST_ASSERT_SUCCESS_ERRNO (zlink_discovery_destroy (&discovery));
    step_log ("test_gateway_weighted_load_balancing: destroy registry");
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
    char registry_pub[MAX_SOCKET_STRING];
    char registry_router[MAX_SOCKET_STRING];
    make_registry_endpoint (registry_pub, sizeof (registry_pub), 25924);
    make_registry_endpoint (registry_router, sizeof (registry_router), 25925);
    setup_registry (ctx, &registry, registry_pub, registry_router);

    void *discovery =
      zlink_discovery_new (ctx, ZLINK_SERVICE_TYPE_GATEWAY);
    TEST_ASSERT_NOT_NULL (discovery);
    TEST_ASSERT_SUCCESS_ERRNO (zlink_discovery_connect_registry (
      discovery, registry_router));
    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_discovery_subscribe (discovery, "svc-sync"));
    void *client =
      create_gateway_attached (ctx, discovery, "svc-sync", NULL,
                         &discard_gateway_message);
    TEST_ASSERT_NOT_NULL (client);

    gateway_probe_t probe;
    g_probe_a = &probe;
    gateway_server_t server;
    create_server_gateway (&server, ctx, registry_router,
                           "svc-sync", "SYNC", &gateway_handler_a);
    char endpoint[256];
    snprintf (endpoint, sizeof (endpoint), "tcp://127.0.0.1:%d", test_port (22544));
    bind_register_server (&server, endpoint, 1, &gateway_handler_a);

    wait_gateway_ready (client, 2000);

    const int send_threads = 4;
    const int send_per_thread = 50;
    std::atomic<int> send_ok (0);
    std::atomic<int> send_fail (0);
    std::atomic<int> update_fail (0);
    std::vector<std::thread> threads;
    std::vector<send_worker_args_t> args (send_threads);

    for (int i = 0; i < send_threads; ++i) {
        args[i].gateway = client;
        args[i].count = send_per_thread;
        args[i].ok = &send_ok;
        args[i].fail = &send_fail;
        threads.push_back (std::thread (&send_worker, &args[i]));
    }
    step_log ("test_gateway_concurrent_send_and_updates: workers started");

    std::thread updater ([&] () {
        for (int i = 0; i < 50; ++i) {
            if (!try_update_gateway_weight_with_timeout (
                  server.gateway, (i % 2) + 1, 100))
                ++update_fail;
            msleep (1);
        }
    });
    step_log ("test_gateway_concurrent_send_and_updates: updater started");

    for (size_t i = 0; i < threads.size (); ++i)
        threads[i].join ();
    step_log ("test_gateway_concurrent_send_and_updates: workers joined");
    updater.join ();
    step_log ("test_gateway_concurrent_send_and_updates: updater joined");

    TEST_ASSERT_EQUAL_INT (0, send_fail.load ());
    TEST_ASSERT_EQUAL_INT (0, update_fail.load ());
    step_log ("test_gateway_concurrent_send_and_updates: waiting for probe");
    TEST_ASSERT_TRUE (wait_for_calls (&probe.requests, send_ok.load (), 5000));

    step_log ("test_gateway_concurrent_send_and_updates: destroy server");
    destroy_server_gateway (&server);
    step_log ("test_gateway_concurrent_send_and_updates: destroy client");
    TEST_ASSERT_SUCCESS_ERRNO (zlink_gateway_destroy (&client));
    step_log ("test_gateway_concurrent_send_and_updates: destroy discovery");
    TEST_ASSERT_SUCCESS_ERRNO (zlink_discovery_destroy (&discovery));
    step_log ("test_gateway_concurrent_send_and_updates: destroy registry");
    TEST_ASSERT_SUCCESS_ERRNO (zlink_registry_destroy (&registry));
    g_probe_a = NULL;
}

int main (void)
{
    UNITY_BEGIN ();
#define RUN_GATEWAY_TEST(name)                                                 \
    do {                                                                       \
        if (should_run_named_test (#name))                                     \
            RUN_TEST (name);                                                   \
    } while (0)
    RUN_GATEWAY_TEST (test_gateway_provider_setsockopt);
    RUN_GATEWAY_TEST (test_gateway_can_be_polled_via_service_instance);
    RUN_GATEWAY_TEST (test_gateway_refreshes_existing_service_on_first_connection_count);
    RUN_GATEWAY_TEST (test_gateway_router_peers_do_not_enter_pollable_mode);
    RUN_GATEWAY_TEST (test_gateway_single_service_tcp);
    RUN_GATEWAY_TEST (test_gateway_send_rid_tcp);
    RUN_GATEWAY_TEST (test_gateway_multi_service_tcp);
    RUN_GATEWAY_TEST (test_gateway_refresh_on_update);
    RUN_GATEWAY_TEST (test_gateway_protocol_ws);
    RUN_GATEWAY_TEST (test_gateway_protocol_tls);
    RUN_GATEWAY_TEST (test_gateway_protocol_wss);
    RUN_GATEWAY_TEST (test_gateway_load_balancing);
    RUN_GATEWAY_TEST (test_gateway_weighted_load_balancing);
#undef RUN_GATEWAY_TEST
    if (should_run_gateway_concurrent_stress ()
        && should_run_named_test ("test_gateway_concurrent_send_and_updates"))
        RUN_TEST (test_gateway_concurrent_send_and_updates);
    return UNITY_END ();
}
