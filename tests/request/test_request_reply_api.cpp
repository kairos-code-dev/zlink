/* SPDX-License-Identifier: MPL-2.0 */

#include "../testutil.hpp"
#include "../testutil_unity.hpp"

#include <atomic>
#include <stdio.h>
#include <string.h>
#include <vector>

static void *g_server = NULL;
static std::atomic<int> g_server_called (0);
static std::atomic<int> g_server_called_count (0);
static std::atomic<int> g_client_called (0);
static std::atomic<int> g_client_called_count (0);
static std::atomic<int> g_client_error (0);
static std::atomic<int> g_reply_rc (0);
static std::atomic<int> g_reply_errno (0);
static char g_reply_buf[64];
static size_t g_reply_size = 0;
static std::atomic<int> g_server_part_ok (0);
static std::atomic<int> g_client_part_ok (0);
static std::atomic<int> g_group_index (0);
static std::atomic<uint64_t> g_group_order[2];
static std::atomic<int> g_pipeline_ok (0);
static std::atomic<int> g_pipeline_calls (0);
static const int pipeline_count = 5;
static std::atomic<uint64_t> g_pipeline_req_ids[pipeline_count];

struct pipeline_entry_t
{
    zmq_routing_id_t routing_id;
    uint64_t request_id;
    int index;
};

static std::vector<pipeline_entry_t> g_pipeline_entries;

static void reset_state ()
{
    g_server_called.store (0);
    g_server_called_count.store (0);
    g_client_called.store (0);
    g_client_called_count.store (0);
    g_client_error.store (0);
    g_reply_rc.store (0);
    g_reply_errno.store (0);
    g_reply_size = 0;
    memset (g_reply_buf, 0, sizeof (g_reply_buf));
    g_server_part_ok.store (0);
    g_client_part_ok.store (0);
    g_group_index.store (0);
    g_group_order[0].store (0);
    g_group_order[1].store (0);
    g_pipeline_ok.store (0);
    g_pipeline_calls.store (0);
    for (int i = 0; i < pipeline_count; ++i)
        g_pipeline_req_ids[i].store (0);
    g_pipeline_entries.clear ();
}

static void wait_for (std::atomic<int> &flag, int timeout_ms)
{
    int elapsed = 0;
    while (!flag.load () && elapsed < timeout_ms) {
        msleep (10);
        elapsed += 10;
    }
}

static void server_echo_handler (zmq_msg_t *parts,
                                 size_t part_count,
                                 const zmq_routing_id_t *routing_id,
                                 uint64_t request_id)
{
    g_server_called.store (1);

    if (parts && part_count > 0)
        zmq_msgv_close (parts, part_count);

    zmq_msg_t reply;
    zmq_msg_init_size (&reply, 5);
    memcpy (zmq_msg_data (&reply), "World", 5);
    const int rc = zmq_reply (g_server, routing_id, request_id, &reply, 1);
    g_reply_rc.store (rc);
    g_reply_errno.store (rc == 0 ? 0 : errno);
}

static void server_no_reply_handler (zmq_msg_t *parts,
                                     size_t part_count,
                                     const zmq_routing_id_t *,
                                     uint64_t)
{
    g_server_called.store (1);
    if (parts && part_count > 0)
        zmq_msgv_close (parts, part_count);
}

static void server_group_handler (zmq_msg_t *parts,
                                  size_t part_count,
                                  const zmq_routing_id_t *routing_id,
                                  uint64_t request_id)
{
    g_server_called_count.fetch_add (1);
    if (parts && part_count > 0)
        zmq_msgv_close (parts, part_count);

    zmq_msg_t reply;
    zmq_msg_init_size (&reply, 2);
    memcpy (zmq_msg_data (&reply), "OK", 2);
    zmq_reply (g_server, routing_id, request_id, &reply, 1);
}

static void server_reply_simple_handler (zmq_msg_t *parts,
                                         size_t part_count,
                                         const zmq_routing_id_t *,
                                         uint64_t)
{
    g_server_called_count.fetch_add (1);
    if (parts && part_count > 0)
        zmq_msgv_close (parts, part_count);

    zmq_msg_t reply;
    zmq_msg_init_size (&reply, 6);
    memcpy (zmq_msg_data (&reply), "SIMPLE", 6);
    const int rc = zmq_reply_simple (g_server, &reply, 1);
    g_reply_rc.store (rc);
    g_reply_errno.store (rc == 0 ? 0 : errno);
}

static void server_multipart_handler (zmq_msg_t *parts,
                                      size_t part_count,
                                      const zmq_routing_id_t *routing_id,
                                      uint64_t request_id)
{
    g_server_called_count.fetch_add (1);
    bool ok = false;
    if (parts && part_count == 2) {
        const char *head = static_cast<const char *> (zmq_msg_data (&parts[0]));
        const size_t head_size = zmq_msg_size (&parts[0]);
        const char *body = static_cast<const char *> (zmq_msg_data (&parts[1]));
        const size_t body_size = zmq_msg_size (&parts[1]);
        ok = head_size == 4 && body_size == 4
             && memcmp (head, "head", 4) == 0
             && memcmp (body, "body", 4) == 0;
    }
    g_server_part_ok.store (ok ? 1 : 0);
    if (parts && part_count > 0)
        zmq_msgv_close (parts, part_count);

    zmq_msg_t reply_parts[2];
    zmq_msg_init_size (&reply_parts[0], 5);
    memcpy (zmq_msg_data (&reply_parts[0]), "resp1", 5);
    zmq_msg_init_size (&reply_parts[1], 5);
    memcpy (zmq_msg_data (&reply_parts[1]), "resp2", 5);
    const int rc = zmq_reply (g_server, routing_id, request_id, reply_parts, 2);
    if (rc != 0) {
        zmq_msg_close (&reply_parts[0]);
        zmq_msg_close (&reply_parts[1]);
    }
}

static void server_pipeline_handler (zmq_msg_t *parts,
                                     size_t part_count,
                                     const zmq_routing_id_t *routing_id,
                                     uint64_t request_id)
{
    g_server_called_count.fetch_add (1);
    int index = -1;
    if (parts && part_count > 0) {
        const char *data = static_cast<const char *> (zmq_msg_data (&parts[0]));
        const size_t size = zmq_msg_size (&parts[0]);
        if (size >= 5 && data[0] == 'r' && data[1] == 'e' && data[2] == 'q'
            && data[3] == '-' && data[4] >= '0' && data[4] <= '9')
            index = data[4] - '0';
    }
    if (parts && part_count > 0)
        zmq_msgv_close (parts, part_count);

    pipeline_entry_t entry;
    if (routing_id)
        entry.routing_id = *routing_id;
    else
        entry.routing_id.size = 0;
    entry.request_id = request_id;
    entry.index = index;
    g_pipeline_entries.push_back (entry);

    if (static_cast<int> (g_pipeline_entries.size ()) == pipeline_count) {
        for (int i = pipeline_count - 1; i >= 0; --i) {
            const pipeline_entry_t &item = g_pipeline_entries[i];
            char buf[16];
            int len = snprintf (buf, sizeof (buf), "rep-%d", item.index);
            if (len < 0)
                len = 0;

            zmq_msg_t reply;
            zmq_msg_init_size (&reply, static_cast<size_t> (len));
            if (len > 0)
                memcpy (zmq_msg_data (&reply), buf, static_cast<size_t> (len));
            zmq_reply (g_server, &item.routing_id, item.request_id, &reply, 1);
        }
        g_pipeline_entries.clear ();
    }
}

static void client_callback (uint64_t,
                             zmq_msg_t *reply_parts,
                             size_t reply_count,
                             int error)
{
    g_client_error.store (error);
    if (error == 0 && reply_parts && reply_count > 0) {
        g_reply_size = zmq_msg_size (&reply_parts[0]);
        if (g_reply_size > sizeof (g_reply_buf))
            g_reply_size = sizeof (g_reply_buf);
        memcpy (g_reply_buf, zmq_msg_data (&reply_parts[0]), g_reply_size);
    }
    if (reply_parts && reply_count > 0)
        zmq_msgv_close (reply_parts, reply_count);
    g_client_called.store (1);
}

static void client_group_callback (uint64_t request_id,
                                   zmq_msg_t *reply_parts,
                                   size_t reply_count,
                                   int error)
{
    if (error == 0) {
        const int index = g_group_index.fetch_add (1);
        if (index < 2)
            g_group_order[index].store (request_id);
    }
    if (reply_parts && reply_count > 0)
        zmq_msgv_close (reply_parts, reply_count);
}

static void client_multipart_callback (uint64_t,
                                       zmq_msg_t *reply_parts,
                                       size_t reply_count,
                                       int error)
{
    bool ok = false;
    if (error == 0 && reply_parts && reply_count == 2) {
        const char *first =
          static_cast<const char *> (zmq_msg_data (&reply_parts[0]));
        const size_t first_size = zmq_msg_size (&reply_parts[0]);
        const char *second =
          static_cast<const char *> (zmq_msg_data (&reply_parts[1]));
        const size_t second_size = zmq_msg_size (&reply_parts[1]);
        ok = first_size == 5 && second_size == 5
             && memcmp (first, "resp1", 5) == 0
             && memcmp (second, "resp2", 5) == 0;
    }
    g_client_part_ok.store (ok ? 1 : 0);
    if (reply_parts && reply_count > 0)
        zmq_msgv_close (reply_parts, reply_count);
    g_client_called_count.fetch_add (1);
}

static void pipeline_callback (uint64_t request_id,
                               zmq_msg_t *reply_parts,
                               size_t reply_count,
                               int error)
{
    bool ok = false;
    if (error == 0 && reply_parts && reply_count > 0) {
        int index = -1;
        for (int i = 0; i < pipeline_count; ++i) {
            if (g_pipeline_req_ids[i].load () == request_id) {
                index = i;
                break;
            }
        }
        if (index >= 0) {
            char expected[16];
            int len = snprintf (expected, sizeof (expected), "rep-%d", index);
            if (len > 0
                && zmq_msg_size (&reply_parts[0])
                     == static_cast<size_t> (len)
                && memcmp (zmq_msg_data (&reply_parts[0]), expected,
                           static_cast<size_t> (len))
                     == 0) {
                ok = true;
            }
        }
    }
    if (ok)
        g_pipeline_ok.fetch_add (1);
    if (reply_parts && reply_count > 0)
        zmq_msgv_close (reply_parts, reply_count);
    g_pipeline_calls.fetch_add (1);
}
static void test_callback_request_reply ()
{
    reset_state ();

    void *ctx = zmq_ctx_new ();
    TEST_ASSERT_NOT_NULL (ctx);

    g_server = zmq_socket_threadsafe (ctx, ZMQ_ROUTER);
    TEST_ASSERT_NOT_NULL (g_server);

    void *client = zmq_socket_threadsafe (ctx, ZMQ_DEALER);
    TEST_ASSERT_NOT_NULL (client);

    TEST_ASSERT_SUCCESS_ERRNO (zmq_bind (g_server, ENDPOINT_0));
    TEST_ASSERT_SUCCESS_ERRNO (zmq_connect (client, ENDPOINT_0));
    msleep (SETTLE_TIME);

    TEST_ASSERT_SUCCESS_ERRNO (zmq_on_request (g_server, server_echo_handler));

    int correlate = 0;
    size_t correlate_size = sizeof (correlate);
    TEST_ASSERT_SUCCESS_ERRNO (zmq_getsockopt (g_server, ZMQ_REQUEST_CORRELATE,
                                               &correlate, &correlate_size));
    TEST_ASSERT_EQUAL_INT (1, correlate);
    correlate = 0;
    correlate_size = sizeof (correlate);
    TEST_ASSERT_SUCCESS_ERRNO (zmq_getsockopt (client, ZMQ_REQUEST_CORRELATE,
                                               &correlate, &correlate_size));
    TEST_ASSERT_EQUAL_INT (1, correlate);

    zmq_msg_t request;
    zmq_msg_init_size (&request, 5);
    memcpy (zmq_msg_data (&request), "Hello", 5);

    uint64_t req_id = zmq_request (client, NULL, &request, 1, client_callback,
                                   ZMQ_REQUEST_TIMEOUT_DEFAULT);
    TEST_ASSERT_TRUE (req_id > 0);

    wait_for (g_server_called, 2000);
    TEST_ASSERT_TRUE (g_server_called.load ());
    TEST_ASSERT_EQUAL_INT (0, g_reply_rc.load ());

    wait_for (g_client_called, 2000);
    TEST_ASSERT_TRUE (g_client_called.load ());
    TEST_ASSERT_EQUAL_INT (0, g_client_error.load ());
    TEST_ASSERT_EQUAL_UINT (5, g_reply_size);
    TEST_ASSERT_EQUAL_MEMORY ("World", g_reply_buf, 5);

    TEST_ASSERT_SUCCESS_ERRNO (zmq_close (client));
    TEST_ASSERT_SUCCESS_ERRNO (zmq_close (g_server));
    TEST_ASSERT_SUCCESS_ERRNO (zmq_ctx_term (ctx));
    g_server = NULL;
}

static void test_polling_request_reply ()
{
    reset_state ();

    void *ctx = zmq_ctx_new ();
    TEST_ASSERT_NOT_NULL (ctx);

    g_server = zmq_socket_threadsafe (ctx, ZMQ_ROUTER);
    TEST_ASSERT_NOT_NULL (g_server);

    void *client = zmq_socket_threadsafe (ctx, ZMQ_DEALER);
    TEST_ASSERT_NOT_NULL (client);

    TEST_ASSERT_SUCCESS_ERRNO (zmq_bind (g_server, ENDPOINT_1));
    TEST_ASSERT_SUCCESS_ERRNO (zmq_connect (client, ENDPOINT_1));
    msleep (SETTLE_TIME);

    TEST_ASSERT_SUCCESS_ERRNO (zmq_on_request (g_server, server_echo_handler));

    zmq_msg_t request;
    zmq_msg_init_size (&request, 4);
    memcpy (zmq_msg_data (&request), "Ping", 4);

    uint64_t req_id = zmq_request_send (client, NULL, &request, 1);
    TEST_ASSERT_TRUE (req_id > 0);

    zmq_completion_t completion;
    TEST_ASSERT_SUCCESS_ERRNO (zmq_request_recv (client, &completion, 2000));
    TEST_ASSERT_EQUAL_UINT (req_id, completion.request_id);
    TEST_ASSERT_EQUAL_INT (0, completion.error);
    TEST_ASSERT_EQUAL_UINT (1, completion.part_count);
    TEST_ASSERT_EQUAL_MEMORY ("World",
                              zmq_msg_data (&completion.parts[0]),
                              zmq_msg_size (&completion.parts[0]));
    zmq_msgv_close (completion.parts, completion.part_count);

    TEST_ASSERT_SUCCESS_ERRNO (zmq_close (client));
    TEST_ASSERT_SUCCESS_ERRNO (zmq_close (g_server));
    TEST_ASSERT_SUCCESS_ERRNO (zmq_ctx_term (ctx));
    g_server = NULL;
}

static void test_request_timeout ()
{
    reset_state ();

    void *ctx = zmq_ctx_new ();
    TEST_ASSERT_NOT_NULL (ctx);

    g_server = zmq_socket_threadsafe (ctx, ZMQ_ROUTER);
    TEST_ASSERT_NOT_NULL (g_server);

    void *client = zmq_socket_threadsafe (ctx, ZMQ_DEALER);
    TEST_ASSERT_NOT_NULL (client);

    TEST_ASSERT_SUCCESS_ERRNO (zmq_bind (g_server, ENDPOINT_2));
    TEST_ASSERT_SUCCESS_ERRNO (zmq_connect (client, ENDPOINT_2));
    msleep (SETTLE_TIME);

    TEST_ASSERT_SUCCESS_ERRNO (
      zmq_on_request (g_server, server_no_reply_handler));

    zmq_msg_t request;
    zmq_msg_init_size (&request, 4);
    memcpy (zmq_msg_data (&request), "Wait", 4);

    uint64_t req_id =
      zmq_request (client, NULL, &request, 1, client_callback, 50);
    TEST_ASSERT_TRUE (req_id > 0);

    wait_for (g_client_called, 2000);
    TEST_ASSERT_TRUE (g_client_called.load ());
    TEST_ASSERT_EQUAL_INT (ETIMEDOUT, g_client_error.load ());

    TEST_ASSERT_SUCCESS_ERRNO (zmq_close (client));
    TEST_ASSERT_SUCCESS_ERRNO (zmq_close (g_server));
    TEST_ASSERT_SUCCESS_ERRNO (zmq_ctx_term (ctx));
    g_server = NULL;
}

static void test_router_router_targeted ()
{
    reset_state ();

    void *ctx = zmq_ctx_new ();
    TEST_ASSERT_NOT_NULL (ctx);

    g_server = zmq_socket_threadsafe (ctx, ZMQ_ROUTER);
    TEST_ASSERT_NOT_NULL (g_server);

    void *client = zmq_socket_threadsafe (ctx, ZMQ_ROUTER);
    TEST_ASSERT_NOT_NULL (client);

    const char *server_id = "server";
    const char *client_id = "client";
    TEST_ASSERT_SUCCESS_ERRNO (
      zmq_setsockopt (g_server, ZMQ_ROUTING_ID, server_id,
                      strlen (server_id)));
    TEST_ASSERT_SUCCESS_ERRNO (
      zmq_setsockopt (client, ZMQ_ROUTING_ID, client_id,
                      strlen (client_id)));

    TEST_ASSERT_SUCCESS_ERRNO (zmq_bind (g_server, ENDPOINT_3));
    TEST_ASSERT_SUCCESS_ERRNO (zmq_connect (client, ENDPOINT_3));
    msleep (SETTLE_TIME);

    TEST_ASSERT_SUCCESS_ERRNO (zmq_on_request (g_server, server_echo_handler));

    zmq_routing_id_t target;
    target.size = static_cast<uint8_t> (strlen (server_id));
    memcpy (target.data, server_id, target.size);

    zmq_msg_t request;
    zmq_msg_init_size (&request, 5);
    memcpy (zmq_msg_data (&request), "Hello", 5);

    uint64_t req_id = zmq_request (client, &target, &request, 1,
                                   client_callback,
                                   ZMQ_REQUEST_TIMEOUT_DEFAULT);
    TEST_ASSERT_TRUE (req_id > 0);

    wait_for (g_client_called, 2000);
    TEST_ASSERT_TRUE (g_client_called.load ());
    TEST_ASSERT_EQUAL_INT (0, g_client_error.load ());
    TEST_ASSERT_EQUAL_MEMORY ("World", g_reply_buf, 5);

    TEST_ASSERT_SUCCESS_ERRNO (zmq_close (client));
    TEST_ASSERT_SUCCESS_ERRNO (zmq_close (g_server));
    TEST_ASSERT_SUCCESS_ERRNO (zmq_ctx_term (ctx));
    g_server = NULL;
}

static void test_multipart_request_reply ()
{
    reset_state ();

    void *ctx = zmq_ctx_new ();
    TEST_ASSERT_NOT_NULL (ctx);

    g_server = zmq_socket_threadsafe (ctx, ZMQ_ROUTER);
    TEST_ASSERT_NOT_NULL (g_server);

    void *client = zmq_socket_threadsafe (ctx, ZMQ_DEALER);
    TEST_ASSERT_NOT_NULL (client);

    TEST_ASSERT_SUCCESS_ERRNO (zmq_bind (g_server, ENDPOINT_0));
    TEST_ASSERT_SUCCESS_ERRNO (zmq_connect (client, ENDPOINT_0));
    msleep (SETTLE_TIME);

    TEST_ASSERT_SUCCESS_ERRNO (
      zmq_on_request (g_server, server_multipart_handler));

    zmq_msg_t parts[2];
    zmq_msg_init_size (&parts[0], 4);
    memcpy (zmq_msg_data (&parts[0]), "head", 4);
    zmq_msg_init_size (&parts[1], 4);
    memcpy (zmq_msg_data (&parts[1]), "body", 4);

    uint64_t req_id = zmq_request (client, NULL, parts, 2,
                                   client_multipart_callback, 2000);
    TEST_ASSERT_TRUE (req_id > 0);

    wait_for (g_client_called_count, 2000);
    TEST_ASSERT_EQUAL_INT (1, g_server_part_ok.load ());
    TEST_ASSERT_EQUAL_INT (1, g_client_part_ok.load ());

    TEST_ASSERT_SUCCESS_ERRNO (zmq_close (client));
    TEST_ASSERT_SUCCESS_ERRNO (zmq_close (g_server));
    TEST_ASSERT_SUCCESS_ERRNO (zmq_ctx_term (ctx));
    g_server = NULL;
}

static void test_group_request_order ()
{
    reset_state ();

    void *ctx = zmq_ctx_new ();
    TEST_ASSERT_NOT_NULL (ctx);

    g_server = zmq_socket_threadsafe (ctx, ZMQ_ROUTER);
    TEST_ASSERT_NOT_NULL (g_server);

    void *client = zmq_socket_threadsafe (ctx, ZMQ_DEALER);
    TEST_ASSERT_NOT_NULL (client);

    TEST_ASSERT_SUCCESS_ERRNO (zmq_bind (g_server, ENDPOINT_1));
    TEST_ASSERT_SUCCESS_ERRNO (zmq_connect (client, ENDPOINT_1));
    msleep (SETTLE_TIME);

    TEST_ASSERT_SUCCESS_ERRNO (zmq_on_request (g_server, server_group_handler));

    zmq_msg_t msg1;
    zmq_msg_init_size (&msg1, 1);
    memcpy (zmq_msg_data (&msg1), "A", 1);
    uint64_t req1 =
      zmq_group_request (client, NULL, 42, &msg1, 1, client_group_callback,
                         2000);
    TEST_ASSERT_TRUE (req1 > 0);

    zmq_msg_t msg2;
    zmq_msg_init_size (&msg2, 1);
    memcpy (zmq_msg_data (&msg2), "B", 1);
    uint64_t req2 =
      zmq_group_request (client, NULL, 42, &msg2, 1, client_group_callback,
                         2000);
    TEST_ASSERT_TRUE (req2 > 0);

    int elapsed = 0;
    while (g_group_index.load () < 2 && elapsed < 2000) {
        msleep (10);
        elapsed += 10;
    }
    TEST_ASSERT_EQUAL_INT (2, g_group_index.load ());
    TEST_ASSERT_EQUAL_UINT (req1, g_group_order[0].load ());
    TEST_ASSERT_EQUAL_UINT (req2, g_group_order[1].load ());

    TEST_ASSERT_SUCCESS_ERRNO (zmq_close (client));
    TEST_ASSERT_SUCCESS_ERRNO (zmq_close (g_server));
    TEST_ASSERT_SUCCESS_ERRNO (zmq_ctx_term (ctx));
    g_server = NULL;
}

static void test_pipeline_requests ()
{
    reset_state ();

    void *ctx = zmq_ctx_new ();
    TEST_ASSERT_NOT_NULL (ctx);

    g_server = zmq_socket_threadsafe (ctx, ZMQ_ROUTER);
    TEST_ASSERT_NOT_NULL (g_server);

    void *client = zmq_socket_threadsafe (ctx, ZMQ_DEALER);
    TEST_ASSERT_NOT_NULL (client);

    TEST_ASSERT_SUCCESS_ERRNO (zmq_bind (g_server, ENDPOINT_2));
    TEST_ASSERT_SUCCESS_ERRNO (zmq_connect (client, ENDPOINT_2));
    msleep (SETTLE_TIME);

    TEST_ASSERT_SUCCESS_ERRNO (
      zmq_on_request (g_server, server_pipeline_handler));

    for (int i = 0; i < pipeline_count; ++i) {
        char buf[16];
        int len = snprintf (buf, sizeof (buf), "req-%d", i);
        if (len < 0)
            len = 0;
        zmq_msg_t request;
        zmq_msg_init_size (&request, static_cast<size_t> (len));
        if (len > 0)
            memcpy (zmq_msg_data (&request), buf, static_cast<size_t> (len));
        uint64_t req_id = zmq_request (client, NULL, &request, 1,
                                       pipeline_callback, 2000);
        TEST_ASSERT_TRUE (req_id > 0);
        g_pipeline_req_ids[i].store (req_id);
    }

    int elapsed = 0;
    while (g_pipeline_calls.load () < pipeline_count && elapsed < 3000) {
        msleep (10);
        elapsed += 10;
    }

    TEST_ASSERT_EQUAL_INT (pipeline_count, g_pipeline_calls.load ());
    TEST_ASSERT_EQUAL_INT (pipeline_count, g_pipeline_ok.load ());

    TEST_ASSERT_SUCCESS_ERRNO (zmq_close (client));
    TEST_ASSERT_SUCCESS_ERRNO (zmq_close (g_server));
    TEST_ASSERT_SUCCESS_ERRNO (zmq_ctx_term (ctx));
    g_server = NULL;
}

static void test_cancel_all_requests ()
{
    reset_state ();

    void *ctx = zmq_ctx_new ();
    TEST_ASSERT_NOT_NULL (ctx);

    g_server = zmq_socket_threadsafe (ctx, ZMQ_ROUTER);
    TEST_ASSERT_NOT_NULL (g_server);

    void *client = zmq_socket_threadsafe (ctx, ZMQ_DEALER);
    TEST_ASSERT_NOT_NULL (client);

    TEST_ASSERT_SUCCESS_ERRNO (zmq_bind (g_server, ENDPOINT_3));
    TEST_ASSERT_SUCCESS_ERRNO (zmq_connect (client, ENDPOINT_3));
    msleep (SETTLE_TIME);

    TEST_ASSERT_SUCCESS_ERRNO (
      zmq_on_request (g_server, server_no_reply_handler));

    zmq_msg_t request;
    zmq_msg_init_size (&request, 4);
    memcpy (zmq_msg_data (&request), "Wait", 4);
    uint64_t req_id =
      zmq_request (client, NULL, &request, 1, client_callback, 5000);
    TEST_ASSERT_TRUE (req_id > 0);

    msleep (10);
    TEST_ASSERT_EQUAL_INT (1, zmq_pending_requests (client));

    TEST_ASSERT_EQUAL_INT (1, zmq_cancel_all_requests (client));

    wait_for (g_client_called, 2000);
    TEST_ASSERT_TRUE (g_client_called.load ());
    TEST_ASSERT_EQUAL_INT (ECANCELED_ZMQ, g_client_error.load ());
    TEST_ASSERT_EQUAL_INT (0, zmq_pending_requests (client));

    TEST_ASSERT_SUCCESS_ERRNO (zmq_close (client));
    TEST_ASSERT_SUCCESS_ERRNO (zmq_close (g_server));
    TEST_ASSERT_SUCCESS_ERRNO (zmq_ctx_term (ctx));
    g_server = NULL;
}

static void test_reply_simple ()
{
    reset_state ();

    void *ctx = zmq_ctx_new ();
    TEST_ASSERT_NOT_NULL (ctx);

    g_server = zmq_socket_threadsafe (ctx, ZMQ_ROUTER);
    TEST_ASSERT_NOT_NULL (g_server);

    void *client = zmq_socket_threadsafe (ctx, ZMQ_DEALER);
    TEST_ASSERT_NOT_NULL (client);

    TEST_ASSERT_SUCCESS_ERRNO (zmq_bind (g_server, ENDPOINT_0));
    TEST_ASSERT_SUCCESS_ERRNO (zmq_connect (client, ENDPOINT_0));
    msleep (SETTLE_TIME);

    TEST_ASSERT_SUCCESS_ERRNO (
      zmq_on_request (g_server, server_reply_simple_handler));

    zmq_msg_t request;
    zmq_msg_init_size (&request, 4);
    memcpy (zmq_msg_data (&request), "Ping", 4);

    uint64_t req_id = zmq_request (client, NULL, &request, 1, client_callback,
                                   ZMQ_REQUEST_TIMEOUT_DEFAULT);
    TEST_ASSERT_TRUE (req_id > 0);

    wait_for (g_client_called, 2000);
    TEST_ASSERT_TRUE (g_client_called.load ());
    TEST_ASSERT_EQUAL_INT (0, g_client_error.load ());
    TEST_ASSERT_EQUAL_MEMORY ("SIMPLE", g_reply_buf, 6);
    TEST_ASSERT_EQUAL_INT (0, g_reply_rc.load ());

    TEST_ASSERT_SUCCESS_ERRNO (zmq_close (client));
    TEST_ASSERT_SUCCESS_ERRNO (zmq_close (g_server));
    TEST_ASSERT_SUCCESS_ERRNO (zmq_ctx_term (ctx));
    g_server = NULL;
}

int main (void)
{
    setup_test_environment ();

    UNITY_BEGIN ();
    RUN_TEST (test_callback_request_reply);
    RUN_TEST (test_polling_request_reply);
    RUN_TEST (test_request_timeout);
    RUN_TEST (test_router_router_targeted);
    RUN_TEST (test_multipart_request_reply);
    RUN_TEST (test_group_request_order);
    RUN_TEST (test_pipeline_requests);
    RUN_TEST (test_cancel_all_requests);
    RUN_TEST (test_reply_simple);
    return UNITY_END ();
}
