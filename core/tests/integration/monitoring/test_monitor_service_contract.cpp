/* SPDX-License-Identifier: MPL-2.0 */

#include "testutil.hpp"
#include "testutil_monitoring.hpp"
#include "testutil_unity.hpp"

#include <chrono>
#include <condition_variable>
#include <mutex>
#include <string.h>

SETUP_TEARDOWN_TESTCONTEXT

namespace
{
enum monitor_mode_t
{
    monitor_recv_mode = 0,
    monitor_callback_mode = 1
};

enum service_mode_t
{
    service_recv_mode = 0,
    service_callback_mode = 1
};

struct gateway_monitor_probe_t
{
    gateway_monitor_probe_t () : send_ready (false), error_code (0) {}

    std::mutex mutex;
    std::condition_variable cv;
    bool send_ready;
    int error_code;
};

struct gateway_delivery_probe_t
{
    gateway_delivery_probe_t () : gateway (NULL), request_calls (0), reply_calls (0)
    {
        memset (&reply_target_rid, 0, sizeof (reply_target_rid));
        memset (request_payload, 0, sizeof (request_payload));
        memset (reply_payload, 0, sizeof (reply_payload));
    }

    void *gateway;
    std::mutex mutex;
    std::condition_variable cv;
    zlink_routing_id_t reply_target_rid;
    int request_calls;
    int reply_calls;
    char request_payload[64];
    char reply_payload[64];
};

struct spot_monitor_probe_t
{
    spot_monitor_probe_t () :
        sub_ready (false),
        pub_ready (false),
        error_code (0)
    {
    }

    std::mutex mutex;
    std::condition_variable cv;
    bool sub_ready;
    bool pub_ready;
    int error_code;
};

struct spot_delivery_probe_t
{
    spot_delivery_probe_t () : calls (0)
    {
        memset (topic, 0, sizeof (topic));
        memset (payload, 0, sizeof (payload));
    }

    std::mutex mutex;
    std::condition_variable cv;
    int calls;
    char topic[64];
    char payload[64];
};

gateway_monitor_probe_t *g_gateway_monitor_probe = NULL;
gateway_delivery_probe_t *g_gateway_server_probe = NULL;
gateway_delivery_probe_t *g_gateway_client_probe = NULL;
spot_monitor_probe_t *g_spot_monitor_probe = NULL;
spot_delivery_probe_t *g_spot_delivery_probe = NULL;

void close_message_parts (zlink_msg_t *parts_, size_t part_count_)
{
    for (size_t i = 0; i < part_count_; ++i)
        TEST_ASSERT_SUCCESS_ERRNO (zlink_msg_close (&parts_[i]));
}

void init_text_part (zlink_msg_t *part_, const char *text_)
{
    const size_t size = strlen (text_);
    TEST_ASSERT_SUCCESS_ERRNO (zlink_msg_init_size (part_, size));
    memcpy (zlink_msg_data (part_), text_, size);
}

void bind_gateway_on_seed (void *gateway_,
                           int *seed_,
                           char *endpoint_,
                           size_t endpoint_size_)
{
    TEST_ASSERT_NOT_NULL (gateway_);
    TEST_ASSERT_NOT_NULL (seed_);
    TEST_ASSERT_NOT_NULL (endpoint_);

    for (int attempt = 0; attempt < 64; ++attempt) {
        snprintf (endpoint_, endpoint_size_, "tcp://127.0.0.1:%d",
                  test_port (*seed_));
        if (zlink_gateway_bind (gateway_, endpoint_) == 0) {
            ++(*seed_);
            return;
        }
        if (errno != EADDRINUSE)
            TEST_FAIL_MESSAGE ("gateway bind failed");
        ++(*seed_);
    }

    TEST_FAIL_MESSAGE ("gateway bind seed exhausted");
}

void bind_spot_node_on_seed (void *node_,
                             int *seed_,
                             char *endpoint_,
                             size_t endpoint_size_)
{
    TEST_ASSERT_NOT_NULL (node_);
    TEST_ASSERT_NOT_NULL (seed_);
    TEST_ASSERT_NOT_NULL (endpoint_);

    for (int attempt = 0; attempt < 64; ++attempt) {
        snprintf (endpoint_, endpoint_size_, "tcp://127.0.0.1:%d",
                  test_port (*seed_));
        if (zlink_spot_node_bind (node_, endpoint_) == 0) {
            ++(*seed_);
            return;
        }
        if (errno != EADDRINUSE)
            TEST_FAIL_MESSAGE ("spot bind failed");
        ++(*seed_);
    }

    TEST_FAIL_MESSAGE ("spot bind seed exhausted");
}

void send_gateway_text_rid (void *gateway_,
                            const zlink_routing_id_t *rid_,
                            const char *text_)
{
    zlink_msg_t part;
    init_text_part (&part, text_);
    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_gateway_send_rid (gateway_, rid_, &part, 1, 0));
}

void publish_text (void *subject_, const char *topic_, const char *payload_)
{
    zlink_msg_t part;
    init_text_part (&part, payload_);
    TEST_ASSERT_SUCCESS_ERRNO (zlink_publish (subject_, topic_, &part, 1, 0));
}

void gateway_monitor_handler (const zlink_service_event_t *event_, void *)
{
    gateway_monitor_probe_t *probe = g_gateway_monitor_probe;
    if (!probe || !event_)
        return;

    std::lock_guard<std::mutex> lock (probe->mutex);
    if (event_->event_type == ZLINK_GATEWAY_SEND_READY_CHANGED
        && event_->value == 1) {
        probe->send_ready = true;
    } else if (event_->event_type == ZLINK_GATEWAY_MONITOR_EVENT_ERROR) {
        probe->error_code = event_->error_code != 0 ? event_->error_code : EIO;
    }
    probe->cv.notify_all ();
}

bool wait_for_gateway_ready_callback (gateway_monitor_probe_t *probe_,
                                      int timeout_ms_)
{
    if (!probe_)
        return false;

    std::unique_lock<std::mutex> lock (probe_->mutex);
    return probe_->cv.wait_for (
      lock, std::chrono::milliseconds (timeout_ms_), [probe_]() {
          return probe_->send_ready || probe_->error_code != 0;
      })
           && probe_->send_ready && probe_->error_code == 0;
}

bool wait_for_gateway_ready_recv (void *monitor_, int timeout_ms_)
{
    const auto deadline =
      std::chrono::steady_clock::now () + std::chrono::milliseconds (timeout_ms_);
    while (std::chrono::steady_clock::now () < deadline) {
        zlink_pollitem_t item = {monitor_, 0, ZLINK_POLLIN, 0};
        if (zlink_poll (&item, 1, 100) <= 0 || (item.revents & ZLINK_POLLIN) == 0)
            continue;

        for (;;) {
            zlink_service_event_t event;
            if (recv_service_event_from_socket (monitor_, &event, ZLINK_DONTWAIT)
                != 0) {
                break;
            }
            if (event.event_type == ZLINK_GATEWAY_MONITOR_EVENT_ERROR) {
                errno = event.error_code != 0 ? event.error_code : EIO;
                return false;
            }
            if (event.event_type == ZLINK_GATEWAY_SEND_READY_CHANGED
                && event.value == 1) {
                return true;
            }
        }
    }
    return false;
}

void gateway_server_handler (const zlink_routing_id_t *source_rid_,
                             zlink_msg_t *parts_,
                             size_t part_count_,
                             void *)
{
    gateway_delivery_probe_t *probe = g_gateway_server_probe;
    if (!probe || !source_rid_ || part_count_ != 1) {
        close_message_parts (parts_, part_count_);
        return;
    }

    {
        std::lock_guard<std::mutex> lock (probe->mutex);
        probe->reply_target_rid = *source_rid_;
        const size_t size = zlink_msg_size (&parts_[0]);
        const size_t copy_size = size < sizeof (probe->request_payload) - 1
                                   ? size
                                   : sizeof (probe->request_payload) - 1;
        memcpy (probe->request_payload, zlink_msg_data (&parts_[0]), copy_size);
        probe->request_payload[copy_size] = '\0';
        ++probe->request_calls;
    }

    close_message_parts (parts_, part_count_);
    send_gateway_text_rid (probe->gateway, source_rid_, "pong");
    probe->cv.notify_all ();
}

void gateway_client_handler (const zlink_routing_id_t *,
                             zlink_msg_t *parts_,
                             size_t part_count_,
                             void *)
{
    gateway_delivery_probe_t *probe = g_gateway_client_probe;
    if (!probe || part_count_ != 1) {
        close_message_parts (parts_, part_count_);
        return;
    }

    {
        std::lock_guard<std::mutex> lock (probe->mutex);
        const size_t size = zlink_msg_size (&parts_[0]);
        const size_t copy_size = size < sizeof (probe->reply_payload) - 1
                                   ? size
                                   : sizeof (probe->reply_payload) - 1;
        memcpy (probe->reply_payload, zlink_msg_data (&parts_[0]), copy_size);
        probe->reply_payload[copy_size] = '\0';
        ++probe->reply_calls;
    }

    close_message_parts (parts_, part_count_);
    probe->cv.notify_all ();
}

bool wait_for_gateway_delivery (gateway_delivery_probe_t *probe_,
                                bool request_side_,
                                int timeout_ms_)
{
    if (!probe_)
        return false;

    std::unique_lock<std::mutex> lock (probe_->mutex);
    return probe_->cv.wait_for (
      lock, std::chrono::milliseconds (timeout_ms_),
      [probe_, request_side_]() {
          return request_side_ ? probe_->request_calls > 0 : probe_->reply_calls > 0;
      });
}

void spot_monitor_handler (const zlink_service_event_t *event_, void *)
{
    spot_monitor_probe_t *probe = g_spot_monitor_probe;
    if (!probe || !event_)
        return;

    std::lock_guard<std::mutex> lock (probe->mutex);
    if (event_->event_type == ZLINK_SPOT_SUB_DELIVERY_READY_CHANGED
        && event_->value == 1) {
        probe->sub_ready = true;
    } else if (event_->event_type == ZLINK_SPOT_PUB_FIRST_DELIVERY_READY_CHANGED
               && event_->value == 1) {
        probe->pub_ready = true;
    } else if (event_->event_type == ZLINK_MONITOR_EVENT_ERROR) {
        probe->error_code = event_->error_code != 0 ? event_->error_code : EIO;
    }
    probe->cv.notify_all ();
}

bool wait_for_spot_ready_callback (spot_monitor_probe_t *probe_, int timeout_ms_)
{
    if (!probe_)
        return false;

    std::unique_lock<std::mutex> lock (probe_->mutex);
    return probe_->cv.wait_for (
      lock, std::chrono::milliseconds (timeout_ms_), [probe_]() {
          return (probe_->sub_ready && probe_->pub_ready) || probe_->error_code != 0;
      })
           && probe_->sub_ready && probe_->pub_ready && probe_->error_code == 0;
}

bool wait_for_spot_ready_recv (void *sub_monitor_,
                               void *pub_monitor_,
                               int timeout_ms_)
{
    bool sub_ready = false;
    bool pub_ready = false;
    const auto deadline =
      std::chrono::steady_clock::now () + std::chrono::milliseconds (timeout_ms_);

    while (std::chrono::steady_clock::now () < deadline) {
        zlink_pollitem_t items[2] = {
          {sub_monitor_, 0, ZLINK_POLLIN, 0},
          {pub_monitor_, 0, ZLINK_POLLIN, 0}};
        if (zlink_poll (items, 2, 100) <= 0)
            continue;

        for (int i = 0; i < 2; ++i) {
            if ((items[i].revents & ZLINK_POLLIN) == 0)
                continue;

            for (;;) {
                zlink_service_event_t event;
                if (recv_service_event_from_socket (
                      i == 0 ? sub_monitor_ : pub_monitor_, &event,
                      ZLINK_DONTWAIT)
                    != 0) {
                    break;
                }
                if (event.event_type == ZLINK_MONITOR_EVENT_ERROR) {
                    errno = event.error_code != 0 ? event.error_code : EIO;
                    return false;
                }
                if (event.event_type == ZLINK_SPOT_SUB_DELIVERY_READY_CHANGED
                    && event.value == 1) {
                    sub_ready = true;
                }
                if (event.event_type == ZLINK_SPOT_PUB_FIRST_DELIVERY_READY_CHANGED
                    && event.value == 1) {
                    pub_ready = true;
                }
            }
        }

        if (sub_ready && pub_ready)
            return true;
    }

    return false;
}

void spot_delivery_handler (const zlink_routing_id_t *,
                            const char *topic_,
                            size_t topic_len_,
                            zlink_msg_t *parts_,
                            size_t part_count_,
                            void *)
{
    spot_delivery_probe_t *probe = g_spot_delivery_probe;
    if (!probe || !topic_ || part_count_ != 1) {
        close_message_parts (parts_, part_count_);
        return;
    }

    {
        std::lock_guard<std::mutex> lock (probe->mutex);
        const size_t copy_topic =
          topic_len_ < sizeof (probe->topic) - 1 ? topic_len_ : sizeof (probe->topic) - 1;
        memcpy (probe->topic, topic_, copy_topic);
        probe->topic[copy_topic] = '\0';

        const size_t size = zlink_msg_size (&parts_[0]);
        const size_t copy_size = size < sizeof (probe->payload) - 1
                                   ? size
                                   : sizeof (probe->payload) - 1;
        memcpy (probe->payload, zlink_msg_data (&parts_[0]), copy_size);
        probe->payload[copy_size] = '\0';
        ++probe->calls;
    }

    close_message_parts (parts_, part_count_);
    probe->cv.notify_all ();
}

bool wait_for_spot_delivery (spot_delivery_probe_t *probe_, int timeout_ms_)
{
    if (!probe_)
        return false;

    std::unique_lock<std::mutex> lock (probe_->mutex);
    return probe_->cv.wait_for (
      lock, std::chrono::milliseconds (timeout_ms_),
      [probe_]() { return probe_->calls > 0; });
}

void configure_service_handle (void *handle_)
{
    const int zero = 0;
    const int timeout_ms = 3000;
    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_set_option (handle_, ZLINK_OPT_LINGER, &zero, sizeof (zero)));
    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_set_option (handle_, ZLINK_OPT_SNDTIMEO, &timeout_ms,
                        sizeof (timeout_ms)));
    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_set_option (handle_, ZLINK_OPT_RCVTIMEO, &timeout_ms,
                        sizeof (timeout_ms)));
}

void run_gateway_ready_matrix (monitor_mode_t monitor_mode_)
{
    void *ctx = get_test_context ();
    TEST_ASSERT_NOT_NULL (ctx);

    void *server = zlink_gateway_new (ctx, "gw-ready-contract");
    void *client = zlink_gateway_new (ctx, "gw-ready-contract");
    TEST_ASSERT_NOT_NULL (server);
    TEST_ASSERT_NOT_NULL (client);
    configure_service_handle (server);
    configure_service_handle (client);

    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_set_routing_id (server, "gw-server", strlen ("gw-server")));
    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_set_routing_id (client, "gw-client", strlen ("gw-client")));

    zlink_service_monitor_open_options_t monitor_opts;
    memset (&monitor_opts, 0, sizeof (monitor_opts));
    monitor_opts.events = ZLINK_GATEWAY_SEND_READY_CHANGED
                          | ZLINK_GATEWAY_MONITOR_EVENT_ERROR;
    void *monitor = zlink_service_monitor_open (client, &monitor_opts);
    TEST_ASSERT_NOT_NULL (monitor);
    configure_service_handle (monitor);

    gateway_monitor_probe_t monitor_probe;
    g_gateway_monitor_probe = &monitor_probe;
    if (monitor_mode_ == monitor_callback_mode) {
        TEST_ASSERT_SUCCESS_ERRNO (
          zlink_service_monitor_handler (monitor, &gateway_monitor_handler, NULL));
    }

    char endpoint[MAX_SOCKET_STRING];
    int bind_seed = 23100;
    bind_gateway_on_seed (server, &bind_seed, endpoint, sizeof (endpoint));

    zlink_routing_id_t server_rid;
    memset (&server_rid, 0, sizeof (server_rid));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_get_routing_id (server, &server_rid));
    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_gateway_connect (client, endpoint, &server_rid));

    const bool ready =
      monitor_mode_ == monitor_recv_mode
        ? wait_for_gateway_ready_recv (monitor, 3000)
        : wait_for_gateway_ready_callback (&monitor_probe, 3000);
    TEST_ASSERT_TRUE (ready);

    send_gateway_text_rid (client, &server_rid, "ping");

    zlink_routing_id_t source_rid;
    memset (&source_rid, 0, sizeof (source_rid));
    zlink_msg_t *parts = NULL;
    size_t part_count = 0;
    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_gateway_recv (server, &source_rid, &parts, &part_count, 0));
    TEST_ASSERT_EQUAL_UINT64 (1, part_count);
    TEST_ASSERT_EQUAL_MEMORY ("ping", zlink_msg_data (&parts[0]), 4);
    close_message_parts (parts, part_count);
    free (parts);

    send_gateway_text_rid (server, &source_rid, "pong");

    zlink_routing_id_t reply_source_rid;
    memset (&reply_source_rid, 0, sizeof (reply_source_rid));
    zlink_msg_t *reply_parts = NULL;
    size_t reply_part_count = 0;
    TEST_ASSERT_SUCCESS_ERRNO (zlink_gateway_recv (
      client, &reply_source_rid, &reply_parts, &reply_part_count, 0));
    TEST_ASSERT_EQUAL_UINT64 (1, reply_part_count);
    TEST_ASSERT_EQUAL_MEMORY ("pong", zlink_msg_data (&reply_parts[0]), 4);
    close_message_parts (reply_parts, reply_part_count);
    free (reply_parts);

    g_gateway_monitor_probe = NULL;
    TEST_ASSERT_SUCCESS_ERRNO (zlink_monitor_close (&monitor));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_gateway_destroy (&client));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_gateway_destroy (&server));
}

void run_spot_ready_matrix (monitor_mode_t monitor_mode_,
                            service_mode_t service_mode_)
{
    void *ctx = get_test_context ();
    TEST_ASSERT_NOT_NULL (ctx);

    const char *topic = "svc.contract";

    void *pub = zlink_spot_node_new (ctx, "spot-ready-contract");
    void *sub = zlink_spot_node_new (ctx, "spot-ready-contract");
    TEST_ASSERT_NOT_NULL (pub);
    TEST_ASSERT_NOT_NULL (sub);
    configure_service_handle (pub);
    configure_service_handle (sub);

    spot_delivery_probe_t delivery_probe;
    g_spot_delivery_probe = &delivery_probe;
    if (service_mode_ == service_callback_mode) {
        TEST_ASSERT_SUCCESS_ERRNO (
          zlink_subscribe_handler (sub, &spot_delivery_handler, NULL));
    }

    zlink_service_monitor_open_options_t sub_monitor_opts;
    memset (&sub_monitor_opts, 0, sizeof (sub_monitor_opts));
    sub_monitor_opts.events =
      ZLINK_SPOT_SUB_DELIVERY_READY_CHANGED | ZLINK_MONITOR_EVENT_ERROR;
    void *sub_monitor = zlink_service_monitor_open (sub, &sub_monitor_opts);
    TEST_ASSERT_NOT_NULL (sub_monitor);

    zlink_service_monitor_open_options_t pub_monitor_opts;
    memset (&pub_monitor_opts, 0, sizeof (pub_monitor_opts));
    pub_monitor_opts.events =
      ZLINK_SPOT_PUB_FIRST_DELIVERY_READY_CHANGED | ZLINK_MONITOR_EVENT_ERROR;
    void *pub_monitor = zlink_service_monitor_open (pub, &pub_monitor_opts);
    TEST_ASSERT_NOT_NULL (pub_monitor);
    configure_service_handle (sub_monitor);
    configure_service_handle (pub_monitor);

    spot_monitor_probe_t monitor_probe;
    g_spot_monitor_probe = &monitor_probe;
    if (monitor_mode_ == monitor_callback_mode) {
        TEST_ASSERT_SUCCESS_ERRNO (
          zlink_service_monitor_handler (sub_monitor, &spot_monitor_handler, NULL));
        TEST_ASSERT_SUCCESS_ERRNO (
          zlink_service_monitor_handler (pub_monitor, &spot_monitor_handler, NULL));
    }

    char endpoint[MAX_SOCKET_STRING];
    int bind_seed = 23120;
    bind_spot_node_on_seed (pub, &bind_seed, endpoint, sizeof (endpoint));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_spot_node_connect_peer (sub, endpoint));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_set_subscription (sub, topic));

    const bool ready =
      monitor_mode_ == monitor_recv_mode
        ? wait_for_spot_ready_recv (sub_monitor, pub_monitor, 5000)
        : wait_for_spot_ready_callback (&monitor_probe, 5000);
    TEST_ASSERT_TRUE (ready);

    publish_text (pub, topic, "payload");

    if (service_mode_ == service_recv_mode) {
        zlink_msg_t *parts = NULL;
        size_t part_count = 0;
        char topic_buf[64] = {0};
        size_t topic_len = sizeof (topic_buf);
        TEST_ASSERT_SUCCESS_ERRNO (
          zlink_subscribe (sub, &parts, &part_count, 0, topic_buf, &topic_len));
        TEST_ASSERT_EQUAL_UINT64 (1, part_count);
        TEST_ASSERT_EQUAL_STRING (topic, topic_buf);
        TEST_ASSERT_EQUAL_MEMORY ("payload", zlink_msg_data (&parts[0]), 7);
        close_message_parts (parts, part_count);
        free (parts);
    } else {
        TEST_ASSERT_TRUE (wait_for_spot_delivery (&delivery_probe, 3000));
        TEST_ASSERT_EQUAL_STRING (topic, delivery_probe.topic);
        TEST_ASSERT_EQUAL_STRING ("payload", delivery_probe.payload);
    }

    g_spot_monitor_probe = NULL;
    g_spot_delivery_probe = NULL;
    TEST_ASSERT_SUCCESS_ERRNO (zlink_monitor_close (&pub_monitor));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_monitor_close (&sub_monitor));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_spot_node_destroy (&sub));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_spot_node_destroy (&pub));
}
} // namespace

void test_gateway_ready_with_monitor_recv_and_service_recv ()
{
    run_gateway_ready_matrix (monitor_recv_mode);
}

void test_gateway_service_callback_attach_succeeds ()
{
    void *ctx = get_test_context ();
    TEST_ASSERT_NOT_NULL (ctx);

    void *gateway = zlink_gateway_new (ctx, "gw-ready-contract");
    TEST_ASSERT_NOT_NULL (gateway);

    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_recv_handler (gateway, &gateway_server_handler, NULL));

    zlink_msg_t *parts = NULL;
    size_t part_count = 0;
    TEST_ASSERT_EQUAL_INT (
      -1, zlink_gateway_recv (gateway, NULL, &parts, &part_count,
                              ZLINK_DONTWAIT));
    TEST_ASSERT_EQUAL_INT (EBUSY, zlink_errno ());

    TEST_ASSERT_SUCCESS_ERRNO (zlink_gateway_destroy (&gateway));
}

void test_gateway_ready_with_monitor_callback_and_service_recv ()
{
    run_gateway_ready_matrix (monitor_callback_mode);
}

void test_spot_ready_with_monitor_recv_and_service_recv ()
{
    run_spot_ready_matrix (monitor_recv_mode, service_recv_mode);
}

void test_spot_ready_with_monitor_recv_and_service_callback ()
{
    run_spot_ready_matrix (monitor_recv_mode, service_callback_mode);
}

void test_spot_ready_with_monitor_callback_and_service_recv ()
{
    run_spot_ready_matrix (monitor_callback_mode, service_recv_mode);
}

void test_spot_ready_with_monitor_callback_and_service_callback ()
{
    run_spot_ready_matrix (monitor_callback_mode, service_callback_mode);
}

int main (int, char **)
{
    setup_test_environment ();

    UNITY_BEGIN ();
    RUN_TEST (test_gateway_ready_with_monitor_recv_and_service_recv);
    RUN_TEST (test_gateway_service_callback_attach_succeeds);
    RUN_TEST (test_gateway_ready_with_monitor_callback_and_service_recv);
    RUN_TEST (test_spot_ready_with_monitor_recv_and_service_recv);
    RUN_TEST (test_spot_ready_with_monitor_recv_and_service_callback);
    RUN_TEST (test_spot_ready_with_monitor_callback_and_service_recv);
    RUN_TEST (test_spot_ready_with_monitor_callback_and_service_callback);
    return UNITY_END ();
}
