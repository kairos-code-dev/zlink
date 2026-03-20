/* SPDX-License-Identifier: MPL-2.0 */

#include "../testutil.hpp"
#include "../testutil_unity.hpp"

#include <cerrno>
#include <chrono>
#include <condition_variable>
#include <cstring>
#include <mutex>
#include <sstream>
#include <string>

#include <unity.h>

#if !defined(_WIN32)
#include <unistd.h>
#else
#include <process.h>
#endif

void setUp ()
{
}

void tearDown ()
{
}

namespace
{
void noop_socket_handler (const zlink_routing_id_t *,
                          zlink_msg_t *parts_,
                          size_t part_count_,
                          void *)
{
    zlink_multipart_close (parts_, part_count_);
}

void noop_spot_handler (const zlink_routing_id_t *,
                        const char *,
                        size_t,
                        zlink_msg_t *parts_,
                        size_t part_count_,
                        void *)
{
    zlink_multipart_close (parts_, part_count_);
}

void noop_send_ready_handler (void *, void *)
{
}

int current_process_id ()
{
#if !defined(_WIN32)
    return static_cast<int> (getpid ());
#else
    return static_cast<int> (_getpid ());
#endif
}

struct spot_ready_probe_t
{
    spot_ready_probe_t () :
        sub_filter_applied (false),
        sub_delivery_ready (false),
        pub_first_ready (false),
        error_code (0)
    {
    }

    std::mutex mutex;
    std::condition_variable cv;
    bool sub_filter_applied;
    bool sub_delivery_ready;
    bool pub_first_ready;
    int error_code;
};

void spot_sub_monitor_handler (const zlink_service_event_t *event_, void *userdata_)
{
    spot_ready_probe_t *probe =
      static_cast<spot_ready_probe_t *> (userdata_);
    if (!probe || !event_)
        return;

    {
        std::lock_guard<std::mutex> lock (probe->mutex);
        if (event_->event_type == ZLINK_SPOT_SUB_FILTER_APPLIED)
            probe->sub_filter_applied = true;
        else if (event_->event_type == ZLINK_SPOT_SUB_DELIVERY_READY_CHANGED)
            probe->sub_delivery_ready = event_->value > 0;
        else if (event_->event_type == ZLINK_MONITOR_EVENT_ERROR
                 && probe->error_code == 0)
            probe->error_code = event_->error_code != 0 ? event_->error_code : EIO;
    }

    probe->cv.notify_all ();
}

void spot_pub_monitor_handler (const zlink_service_event_t *event_, void *userdata_)
{
    spot_ready_probe_t *probe =
      static_cast<spot_ready_probe_t *> (userdata_);
    if (!probe || !event_)
        return;

    {
        std::lock_guard<std::mutex> lock (probe->mutex);
        if (event_->event_type == ZLINK_SPOT_PUB_FIRST_DELIVERY_READY_CHANGED)
            probe->pub_first_ready = event_->value > 0;
        else if (event_->event_type == ZLINK_MONITOR_EVENT_ERROR
                 && probe->error_code == 0)
            probe->error_code = event_->error_code != 0 ? event_->error_code : EIO;
    }

    probe->cv.notify_all ();
}

bool wait_for_spot_ready_flag (spot_ready_probe_t *probe_,
                               bool spot_ready_probe_t::*member_,
                               int timeout_ms_)
{
    std::unique_lock<std::mutex> lock (probe_->mutex);
    if (probe_->error_code != 0)
        return false;
    if (probe_->*member_)
        return true;

    return probe_->cv.wait_for (
      lock,
      std::chrono::milliseconds (timeout_ms_ > 0 ? timeout_ms_ : 1),
      [probe_, member_] () {
          return probe_->error_code != 0 || probe_->*member_;
      })
           && probe_->error_code == 0 && probe_->*member_;
}

std::string bind_spot_test_endpoint (void *node_)
{
    const int base_port = 36000 + (current_process_id () % 1000) * 8;
    for (int i = 0; i < 64; ++i) {
        std::ostringstream endpoint;
        endpoint << "tcp://127.0.0.1:" << (base_port + i);
        if (zlink_spot_node_bind (node_, endpoint.str ().c_str ()) == 0)
            return endpoint.str ();
    }
    return std::string ();
}

void test_gateway_is_recv_only ()
{
    void *ctx = zlink_ctx_new ();
    TEST_ASSERT_NOT_NULL (ctx);

    void *gateway = zlink_gateway_new (ctx, "unit-gateway");
    TEST_ASSERT_NOT_NULL (gateway);

    void *poller = zlink_poller_new ();
    TEST_ASSERT_NOT_NULL (poller);

    TEST_ASSERT_EQUAL_INT (
      -1, zlink_recv_handler (gateway, &noop_socket_handler, NULL));
    TEST_ASSERT_EQUAL_INT (ENOTSUP, zlink_errno ());

    TEST_ASSERT_EQUAL_INT (
      -1, zlink_send_ready_handler (gateway, &noop_send_ready_handler, NULL));
    TEST_ASSERT_EQUAL_INT (ENOTSUP, zlink_errno ());

    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_poller_add (poller, gateway, gateway, ZLINK_POLLOUT));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_poller_remove (poller, gateway));

    TEST_ASSERT_SUCCESS_ERRNO (zlink_poller_destroy (&poller));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_gateway_destroy (&gateway));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_ctx_term (ctx));
}

void test_spot_node_callback_policy ()
{
    void *ctx = zlink_ctx_new ();
    TEST_ASSERT_NOT_NULL (ctx);

    void *node = zlink_spot_node_new (ctx, "unit-spot");
    TEST_ASSERT_NOT_NULL (node);

    void *poller = zlink_poller_new ();
    TEST_ASSERT_NOT_NULL (poller);

    TEST_ASSERT_EQUAL_INT (
      -1, zlink_send_ready_handler (node, &noop_send_ready_handler, NULL));
    TEST_ASSERT_EQUAL_INT (EBUSY, zlink_errno ());

    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_poller_add (poller, node, node, ZLINK_POLLIN));

    TEST_ASSERT_EQUAL_INT (
      -1, zlink_subscribe_handler (node, &noop_spot_handler, NULL));
    TEST_ASSERT_EQUAL_INT (EBUSY, zlink_errno ());

    TEST_ASSERT_SUCCESS_ERRNO (zlink_poller_remove (poller, node));
    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_subscribe_handler (node, &noop_spot_handler, NULL));

    zlink_msg_t *parts = NULL;
    size_t part_count = 0;
    char topic[64];
    size_t topic_len = sizeof (topic);
    TEST_ASSERT_EQUAL_INT (
      -1, zlink_subscribe (node, &parts, &part_count, ZLINK_DONTWAIT, topic,
                           &topic_len));
    TEST_ASSERT_EQUAL_INT (EBUSY, zlink_errno ());

    TEST_ASSERT_EQUAL_INT (
      -1, zlink_poller_add (poller, node, node, ZLINK_POLLIN));
    TEST_ASSERT_EQUAL_INT (EBUSY, zlink_errno ());
    TEST_ASSERT_EQUAL_INT (
      -1, zlink_poller_add (poller, node, node, ZLINK_POLLOUT));
    TEST_ASSERT_EQUAL_INT (EBUSY, zlink_errno ());

    TEST_ASSERT_SUCCESS_ERRNO (zlink_poller_destroy (&poller));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_spot_node_destroy (&node));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_ctx_term (ctx));
}

void test_stream_send_ready_requires_callback_mode ()
{
    void *ctx = zlink_ctx_new ();
    TEST_ASSERT_NOT_NULL (ctx);

    void *stream = zlink_socket (ctx, ZLINK_SOCKET_STREAM);
    TEST_ASSERT_NOT_NULL (stream);

    void *poller = zlink_poller_new ();
    TEST_ASSERT_NOT_NULL (poller);

    TEST_ASSERT_EQUAL_INT (
      -1, zlink_send_ready_handler (stream, &noop_send_ready_handler, NULL));
    TEST_ASSERT_EQUAL_INT (EBUSY, zlink_errno ());

    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_poller_add (poller, stream, stream, ZLINK_POLLOUT));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_poller_remove (poller, stream));

    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_recv_handler (stream, &noop_socket_handler, NULL));

    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_send_ready_handler (stream, &noop_send_ready_handler, NULL));

    TEST_ASSERT_EQUAL_INT (
      -1, zlink_poller_add (poller, stream, stream, ZLINK_POLLOUT));
    TEST_ASSERT_EQUAL_INT (EBUSY, zlink_errno ());

    TEST_ASSERT_SUCCESS_ERRNO (zlink_poller_destroy (&poller));
    close_zero_linger (stream);
    TEST_ASSERT_SUCCESS_ERRNO (zlink_ctx_term (ctx));
}

void test_generic_monitor_poller_accepts_non_pollin_events ()
{
    void *ctx = zlink_ctx_new ();
    TEST_ASSERT_NOT_NULL (ctx);

    void *server = zlink_socket (ctx, ZLINK_SOCKET_PAIR);
    TEST_ASSERT_NOT_NULL (server);
    void *client = zlink_socket (ctx, ZLINK_SOCKET_PAIR);
    TEST_ASSERT_NOT_NULL (client);

    zlink_socket_monitor_open_options_t socket_monitor_opts;
    memset (&socket_monitor_opts, 0, sizeof (socket_monitor_opts));
    socket_monitor_opts.events = ZLINK_EVENT_ALL;
    void *monitor = zlink_socket_monitor_open (server, &socket_monitor_opts);
    TEST_ASSERT_NOT_NULL (monitor);

    void *poller = zlink_poller_new ();
    TEST_ASSERT_NOT_NULL (poller);

    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_poller_add (poller, monitor, monitor, ZLINK_POLLOUT));
    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_poller_modify (poller, monitor, ZLINK_POLLOUT));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_poller_remove (poller, monitor));

    TEST_ASSERT_SUCCESS_ERRNO (zlink_poller_destroy (&poller));
    close_zero_linger (client);
    close_zero_linger (server);
    TEST_ASSERT_SUCCESS_ERRNO (zlink_monitor_close (&monitor));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_ctx_term (ctx));
}

void test_spot_node_pollin_matches_subscribe_surface ()
{
    void *ctx = zlink_ctx_new ();
    TEST_ASSERT_NOT_NULL (ctx);

    void *server = zlink_spot_node_new (ctx, "unit-spot-server");
    void *client = zlink_spot_node_new (ctx, "unit-spot-client");
    TEST_ASSERT_NOT_NULL (server);
    TEST_ASSERT_NOT_NULL (client);

    zlink_service_monitor_open_options_t sub_monitor_opts;
    memset (&sub_monitor_opts, 0, sizeof (sub_monitor_opts));
    sub_monitor_opts.events = ZLINK_SPOT_SUB_FILTER_APPLIED
                              | ZLINK_SPOT_SUB_DELIVERY_READY_CHANGED
                              | ZLINK_MONITOR_EVENT_ERROR;
    zlink_service_monitor_open_options_t pub_monitor_opts;
    memset (&pub_monitor_opts, 0, sizeof (pub_monitor_opts));
    pub_monitor_opts.events = ZLINK_SPOT_PUB_FIRST_DELIVERY_READY_CHANGED
                              | ZLINK_MONITOR_EVENT_ERROR;

    void *sub_monitor = zlink_service_monitor_open (client, &sub_monitor_opts);
    void *pub_monitor = zlink_service_monitor_open (server, &pub_monitor_opts);
    TEST_ASSERT_NOT_NULL (sub_monitor);
    TEST_ASSERT_NOT_NULL (pub_monitor);

    spot_ready_probe_t sub_probe;
    spot_ready_probe_t pub_probe;
    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_service_monitor_handler (
        sub_monitor, &spot_sub_monitor_handler, &sub_probe));
    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_service_monitor_handler (
        pub_monitor, &spot_pub_monitor_handler, &pub_probe));

    const std::string endpoint = bind_spot_test_endpoint (server);
    TEST_ASSERT_FALSE (endpoint.empty ());
    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_spot_node_connect_peer (client, endpoint.c_str ()));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_set_subscription (client, "bench"));

    TEST_ASSERT_TRUE (wait_for_spot_ready_flag (&sub_probe,
                                                &spot_ready_probe_t::sub_filter_applied,
                                                3000));
    TEST_ASSERT_TRUE (wait_for_spot_ready_flag (&sub_probe,
                                                &spot_ready_probe_t::sub_delivery_ready,
                                                5000));
    TEST_ASSERT_TRUE (wait_for_spot_ready_flag (&pub_probe,
                                                &spot_ready_probe_t::pub_first_ready,
                                                5000));

    void *poller = zlink_poller_new ();
    TEST_ASSERT_NOT_NULL (poller);
    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_poller_add (poller, client, client, ZLINK_POLLIN));

    zlink_msg_t part;
    TEST_ASSERT_SUCCESS_ERRNO (zlink_msg_init_size (&part, 4));
    memcpy (zlink_msg_data (&part), "pong", 4);
    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_publish (server, "bench", &part, 1, 0));

    zlink_poller_event_t event;
    TEST_ASSERT_EQUAL_INT (1, zlink_poller_wait (poller, &event, 3000));
    TEST_ASSERT_EQUAL_PTR (client, event.user_data);
    TEST_ASSERT_TRUE ((event.events & ZLINK_POLLIN) != 0);

    zlink_msg_t *parts = NULL;
    size_t part_count = 0;
    char topic[32];
    size_t topic_len = sizeof (topic);
    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_subscribe (
        client, NULL, &parts, &part_count, topic, &topic_len, 0));
    TEST_ASSERT_EQUAL_UINT64 (1u, part_count);
    TEST_ASSERT_EQUAL_STRING_LEN ("bench", topic, 5);
    TEST_ASSERT_EQUAL_MEMORY ("pong", zlink_msg_data (&parts[0]), 4);

    zlink_multipart_close (parts, part_count);
    free (parts);
    TEST_ASSERT_SUCCESS_ERRNO (zlink_poller_destroy (&poller));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_monitor_close (&sub_monitor));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_monitor_close (&pub_monitor));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_spot_node_destroy (&client));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_spot_node_destroy (&server));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_ctx_term (ctx));
}
}

int main (void)
{
    UNITY_BEGIN ();

    setup_test_environment ();

    RUN_TEST (test_gateway_is_recv_only);
    RUN_TEST (test_spot_node_callback_policy);
    RUN_TEST (test_spot_node_pollin_matches_subscribe_surface);
    RUN_TEST (test_stream_send_ready_requires_callback_mode);
    RUN_TEST (test_generic_monitor_poller_accepts_non_pollin_events);

    return UNITY_END ();
}
