/* SPDX-License-Identifier: MPL-2.0 */

#include "../testutil.hpp"
#include "../testutil_unity.hpp"

#include <atomic>
#include <stdio.h>
#include <string.h>

namespace
{
struct gateway_probe_t
{
    gateway_probe_t () :
        request_calls (0),
        reply_calls (0),
        control_calls (0),
        other_calls (0),
        close_failures (0),
        send_failures (0),
        last_kind (0),
        server (NULL)
    {
        memset (&request_rid, 0, sizeof (request_rid));
        memset (request_service, 0, sizeof (request_service));
        memset (request_payload, 0, sizeof (request_payload));
        memset (reply_service, 0, sizeof (reply_service));
        memset (reply_payload, 0, sizeof (reply_payload));
        memset (control_event, 0, sizeof (control_event));
        memset (control_error, 0, sizeof (control_error));
    }

    std::atomic<int> request_calls;
    std::atomic<int> reply_calls;
    std::atomic<int> control_calls;
    std::atomic<int> other_calls;
    std::atomic<int> close_failures;
    std::atomic<int> send_failures;
    std::atomic<int> last_kind;
    void *server;
    zlink_routing_id_t request_rid;
    char request_service[64];
    char request_payload[64];
    char reply_service[64];
    char reply_payload[64];
    char control_event[64];
    char control_error[32];
};

gateway_probe_t *g_probe = NULL;

void close_parts (gateway_probe_t *probe_, zlink_msg_t *parts_, size_t count_)
{
    for (size_t i = 0; i < count_; ++i) {
        if (zlink_msg_close (&parts_[i]) != 0 && probe_)
            probe_->close_failures.fetch_add (1);
    }
}

bool wait_for_calls (std::atomic<int> *calls_, int expected_, int timeout_ms_)
{
    const int step_ms = 10;
    const int attempts = timeout_ms_ / step_ms;
    for (int i = 0; i < attempts; ++i) {
        if (calls_->load () >= expected_)
            return true;
        msleep (step_ms);
    }
    return calls_->load () >= expected_;
}

void gateway_server_handler (zlink_gateway_msg_kind_t kind_,
                             const char *service_name_,
                             size_t service_name_len_,
                             const zlink_routing_id_t *source_rid_,
                             zlink_msg_t *parts_,
                             size_t part_count_)
{
    gateway_probe_t *probe = g_probe;
    if (!probe) {
        close_parts (probe, parts_, part_count_);
        return;
    }

    if (kind_ == ZLINK_GATEWAY_MSG_REQUEST) {
        if (getenv ("ZLINK_TEST_DEBUG")) {
            fprintf (stderr, "[gw-handler] server request service=%.*s parts=%zu\n",
                     static_cast<int> (service_name_len_), service_name_,
                     part_count_);
            fflush (stderr);
        }
        if (service_name_len_ > 0) {
            const size_t copy_size =
              service_name_len_ < sizeof (probe->request_service) - 1
                ? service_name_len_
                : sizeof (probe->request_service) - 1;
            memcpy (probe->request_service, service_name_, copy_size);
            probe->request_service[copy_size] = '\0';
        }
        if (source_rid_)
            probe->request_rid = *source_rid_;
        if (part_count_ > 0) {
            const size_t size = zlink_msg_size (&parts_[0]);
            const size_t copy_size =
              size < sizeof (probe->request_payload) - 1
                ? size
                : sizeof (probe->request_payload) - 1;
            memcpy (probe->request_payload, zlink_msg_data (&parts_[0]),
                    copy_size);
            probe->request_payload[copy_size] = '\0';
        }
        close_parts (probe, parts_, part_count_);
        probe->request_calls.fetch_add (1);
        if (zlink_gateway_send_rid_bytes (probe->server, probe->request_service,
                                          &probe->request_rid, "pong", 4, 0)
            != 0) {
            probe->send_failures.fetch_add (1);
            if (getenv ("ZLINK_TEST_DEBUG")) {
                fprintf (stderr, "[gw-handler] reply send failed errno=%d\n",
                         errno);
                fflush (stderr);
            }
        }
        return;
    }

    if (kind_ == ZLINK_GATEWAY_MSG_CONTROL) {
        if (part_count_ > 0) {
            const size_t size = zlink_msg_size (&parts_[0]);
            const size_t copy_size =
              size < sizeof (probe->control_event) - 1
                ? size
                : sizeof (probe->control_event) - 1;
            memcpy (probe->control_event, zlink_msg_data (&parts_[0]),
                    copy_size);
            probe->control_event[copy_size] = '\0';
        }
        if (part_count_ > 1) {
            const size_t size = zlink_msg_size (&parts_[1]);
            const size_t copy_size =
              size < sizeof (probe->control_error) - 1
                ? size
                : sizeof (probe->control_error) - 1;
            memcpy (probe->control_error, zlink_msg_data (&parts_[1]),
                    copy_size);
            probe->control_error[copy_size] = '\0';
        }
        close_parts (probe, parts_, part_count_);
        probe->control_calls.fetch_add (1);
        probe->last_kind.store (static_cast<int> (kind_));
        return;
    }

    probe->other_calls.fetch_add (1);
    probe->last_kind.store (static_cast<int> (kind_));
    close_parts (probe, parts_, part_count_);
}

void gateway_client_handler (zlink_gateway_msg_kind_t kind_,
                             const char *service_name_,
                             size_t service_name_len_,
                             const zlink_routing_id_t *source_rid_,
                             zlink_msg_t *parts_,
                             size_t part_count_)
{
    LIBZLINK_UNUSED (source_rid_);

    gateway_probe_t *probe = g_probe;
    if (!probe) {
        close_parts (probe, parts_, part_count_);
        return;
    }

    if (kind_ == ZLINK_GATEWAY_MSG_REPLY) {
        if (getenv ("ZLINK_TEST_DEBUG")) {
            fprintf (stderr, "[gw-handler] client reply service=%.*s parts=%zu\n",
                     static_cast<int> (service_name_len_), service_name_,
                     part_count_);
            fflush (stderr);
        }
        if (service_name_len_ > 0) {
            const size_t copy_size =
              service_name_len_ < sizeof (probe->reply_service) - 1
                ? service_name_len_
                : sizeof (probe->reply_service) - 1;
            memcpy (probe->reply_service, service_name_, copy_size);
            probe->reply_service[copy_size] = '\0';
        }
        if (part_count_ > 0) {
            const size_t size = zlink_msg_size (&parts_[0]);
            const size_t copy_size =
              size < sizeof (probe->reply_payload) - 1
                ? size
                : sizeof (probe->reply_payload) - 1;
            memcpy (probe->reply_payload, zlink_msg_data (&parts_[0]),
                    copy_size);
            probe->reply_payload[copy_size] = '\0';
        }
        probe->reply_calls.fetch_add (1);
        probe->last_kind.store (static_cast<int> (kind_));
    } else {
        if (getenv ("ZLINK_TEST_DEBUG")) {
            fprintf (stderr, "[gw-handler] client other kind=%d service=%.*s\n",
                     static_cast<int> (kind_),
                     static_cast<int> (service_name_len_), service_name_);
            fflush (stderr);
        }
        probe->other_calls.fetch_add (1);
        probe->last_kind.store (static_cast<int> (kind_));
    }

    close_parts (probe, parts_, part_count_);
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
}

SETUP_TEARDOWN_TESTCONTEXT

void test_gateway_handler_dispatches_request_and_reply ()
{
    void *ctx = get_test_context ();
    TEST_ASSERT_NOT_NULL (ctx);

    void *registry = NULL;
    setup_registry (ctx, &registry, "inproc://gw-handler-pub",
                    "inproc://gw-handler-router");

    void *client_discovery =
      zlink_discovery_new_typed (ctx, ZLINK_SERVICE_TYPE_GATEWAY);
    void *server_discovery =
      zlink_discovery_new_typed (ctx, ZLINK_SERVICE_TYPE_GATEWAY);
    TEST_ASSERT_NOT_NULL (client_discovery);
    TEST_ASSERT_NOT_NULL (server_discovery);

    TEST_ASSERT_SUCCESS_ERRNO (zlink_discovery_connect_registry (
      client_discovery, "inproc://gw-handler-router"));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_discovery_connect_registry (
      server_discovery, "inproc://gw-handler-router"));
    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_discovery_subscribe (client_discovery, "svc-handler"));

    void *client = zlink_gateway_new (ctx, client_discovery, "gw-client-h",
                                      &gateway_client_handler);
    void *server = zlink_gateway_new (ctx, server_discovery, "gw-server-h",
                                      &gateway_server_handler);
    TEST_ASSERT_NOT_NULL (client);
    TEST_ASSERT_NOT_NULL (server);

    gateway_probe_t probe;
    probe.server = server;
    g_probe = &probe;

    char endpoint[MAX_SOCKET_STRING];
    snprintf (endpoint, sizeof (endpoint), "tcp://127.0.0.1:%d",
              test_port (22620));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_gateway_bind (server, endpoint));
    register_gateway_with_timeout (server, "svc-handler", endpoint, 1, 3000);
    TEST_ASSERT_TRUE (wait_for_calls (&probe.control_calls, 1, 3000));

    for (int i = 0; i < 400; ++i) {
        if (zlink_gateway_connection_count (client, "svc-handler") > 0)
            break;
        msleep (10);
    }
    TEST_ASSERT_TRUE (zlink_gateway_connection_count (client, "svc-handler") > 0);

    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_gateway_send_bytes (client, "svc-handler", "ping", 4, 0));

    TEST_ASSERT_TRUE (wait_for_calls (&probe.request_calls, 1, 3000));
    TEST_ASSERT_TRUE (wait_for_calls (&probe.reply_calls, 1, 3000));
    TEST_ASSERT_EQUAL_STRING ("svc-handler", probe.request_service);
    TEST_ASSERT_EQUAL_STRING ("ping", probe.request_payload);
    TEST_ASSERT_EQUAL_STRING ("svc-handler", probe.reply_service);
    TEST_ASSERT_EQUAL_STRING ("pong", probe.reply_payload);
    TEST_ASSERT_EQUAL_STRING ("register_ok", probe.control_event);
    TEST_ASSERT_EQUAL_STRING ("0", probe.control_error);
    TEST_ASSERT_EQUAL_INT (0, probe.close_failures.load ());
    TEST_ASSERT_EQUAL_INT (0, probe.send_failures.load ());
    TEST_ASSERT_EQUAL_INT (0, probe.other_calls.load ());

    TEST_ASSERT_SUCCESS_ERRNO (zlink_gateway_destroy (&server));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_gateway_destroy (&client));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_discovery_destroy (&server_discovery));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_discovery_destroy (&client_discovery));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_registry_destroy (&registry));
    g_probe = NULL;
}

int main (int, char **)
{
    setup_test_environment ();

    UNITY_BEGIN ();
    RUN_TEST (test_gateway_handler_dispatches_request_and_reply);
    return UNITY_END ();
}
