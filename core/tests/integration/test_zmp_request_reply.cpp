/* SPDX-License-Identifier: MPL-2.0 */

#include "testutil.hpp"
#include "testutil_unity.hpp"

#include <chrono>
#include <condition_variable>
#include <cstdlib>
#include <mutex>
#include <string>
#include <vector>
#include <string.h>
#include <thread>

#if !defined _WIN32
#include <signal.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#endif

SETUP_TEARDOWN_TESTCONTEXT

namespace
{
struct reply_probe_t
{
    std::mutex mutex;
    std::condition_variable cv;
    bool done;
    zlink_request_result_t result;
    size_t part_count;
    std::string payload;
    size_t callback_count;
    void *progress_handle;

    reply_probe_t () :
        done (false),
        result (ZLINK_REQUEST_PROTOCOL_ERROR),
        part_count (0),
        callback_count (0),
        progress_handle (NULL)
    {
    }
};

struct request_handler_probe_t
{
    std::mutex mutex;
    std::condition_variable cv;
    bool invoked;
    uint64_t request_seq;
    std::string peer_rid;
    std::string request_payload;
    zlink_routing_id_t peer_rid_value;

    request_handler_probe_t () : invoked (false), request_seq (0)
    {
        memset (&peer_rid_value, 0, sizeof (peer_rid_value));
    }
};

struct request_event_t
{
    uint64_t request_seq;
    std::string peer_rid;
    zlink_routing_id_t peer_rid_value;
    std::string request_payload;
};

struct multi_request_probe_t
{
    std::mutex mutex;
    std::condition_variable cv;
    std::vector<request_event_t> events;
};

void capture_reply (zlink_request_result_t result_,
                    zlink_msg_t *parts_,
                    size_t part_count_,
                    void *userdata_);

void init_string_part (zlink_msg_t *part_, const char *text_)
{
    const size_t size = strlen (text_);
    TEST_ASSERT_SUCCESS_ERRNO (zlink_msg_init_size (part_, size));
    memcpy (zlink_msg_data (part_), text_, size);
}

void configure_submit_retry (void *socket_)
{
    int retry_mode = ZLINK_SUBMIT_RETRY_LOCAL_FAILURE;
    int retry_timeout = 200;
    int retry_attempts = 2;
    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_set_option (socket_, ZLINK_OPT_SUBMIT_RETRY_MODE, &retry_mode, sizeof (retry_mode)));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_set_option (socket_, ZLINK_OPT_SUBMIT_RETRY_TIMEOUT,
                                                 &retry_timeout, sizeof (retry_timeout)));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_set_option (socket_, ZLINK_OPT_SUBMIT_RETRY_ATTEMPTS,
                                                 &retry_attempts, sizeof (retry_attempts)));
}

std::string part_to_string_and_close (zlink_msg_t *part_)
{
    TEST_ASSERT_NOT_NULL (part_);
    std::string value (static_cast<const char *> (zlink_msg_data (part_)), zlink_msg_size (part_));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_msg_close (part_));
    return value;
}

zlink_recv_result_t recv_dealer_part_with_retry (void *dealer_,
                                                 uint8_t *message_type_out_,
                                                 uint64_t *request_seq_out_,
                                                 zlink_msg_t *part_out_,
                                                 zlink_part_flag_t *has_more_out_)
{
    const auto deadline = std::chrono::steady_clock::now () + std::chrono::milliseconds (3000);
    while (std::chrono::steady_clock::now () < deadline) {
        const zlink_recv_result_t rc =
          zlink_dealer_recv_part (dealer_, message_type_out_, request_seq_out_, part_out_,
                                  has_more_out_, static_cast<zlink_recv_flags_t> (ZLINK_DONTWAIT));
        if (rc == ZLINK_RECV_OK)
            return rc;
        TEST_ASSERT_EQUAL_INT (ZLINK_RECV_NO_DATA, rc);
        TEST_ASSERT_EQUAL_INT (EAGAIN, zlink_errno ());
        msleep (1);
    }

    TEST_FAIL_MESSAGE ("timed out waiting for zlink_dealer_recv_part");
    return ZLINK_RECV_INTERNAL_ERROR;
}

void arm_dealer_recv_part (void *dealer_)
{
    uint8_t message_type = 0;
    uint64_t request_seq = 0;
    zlink_part_flag_t has_more = ZLINK_PART_FINAL;
    zlink_msg_t part;
    zlink_msg_init (&part);
    const zlink_recv_result_t rc =
      zlink_dealer_recv_part (dealer_, &message_type, &request_seq, &part, &has_more,
                              static_cast<zlink_recv_flags_t> (ZLINK_DONTWAIT));
    TEST_ASSERT_EQUAL_INT (ZLINK_RECV_NO_DATA, rc);
    TEST_ASSERT_EQUAL_INT (EAGAIN, zlink_errno ());
    TEST_ASSERT_SUCCESS_ERRNO (zlink_msg_close (&part));
}

void set_routing_id_text (void *handle_, const char *text_)
{
    TEST_ASSERT_SUCCESS_ERRNO (zlink_set_routing_id (handle_, text_, strlen (text_)));
}

zlink_routing_id_t get_routing_id_value (void *handle_)
{
    zlink_routing_id_t rid;
    memset (&rid, 0, sizeof (rid));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_get_routing_id (handle_, &rid));
    return rid;
}

std::string msg_to_string (const zlink_msg_t *part_)
{
    zlink_msg_t *mutable_part = const_cast<zlink_msg_t *> (part_);
    return std::string (static_cast<const char *> (zlink_msg_data (mutable_part)),
                        zlink_msg_size (part_));
}

int drain_completion_via_poller (void *subject_)
{
    void *poller = zlink_poller_new ();
    if (!poller)
        return -1;
    int rc = -1;
    if (zlink_poller_add (poller, subject_, NULL, ZLINK_POLLCOMPLETION) == ZLINK_CONFIG_OK) {
        zlink_poller_event_t event;
        rc = zlink_poller_wait (poller, &event, 1, 0, NULL);
        (void) zlink_poller_remove (poller, subject_);
    }
    (void) zlink_poller_destroy (&poller);
    return rc;
}

bool wait_for_reply (reply_probe_t *probe_)
{
    const auto deadline =
      std::chrono::steady_clock::now () + std::chrono::milliseconds (SETTLE_TIME * 20);
    while (std::chrono::steady_clock::now () < deadline) {
        {
            std::unique_lock<std::mutex> lock (probe_->mutex);
            if (probe_->cv.wait_for (lock, std::chrono::milliseconds (10),
                                     [probe_] () { return probe_->done; }))
                return true;
        }
        if (probe_->progress_handle)
            (void) drain_completion_via_poller (probe_->progress_handle);
    }

    return false;
}

bool wait_for_reply_count (reply_probe_t *probe_, size_t expected_count_)
{
    const auto deadline =
      std::chrono::steady_clock::now () + std::chrono::milliseconds (SETTLE_TIME * 20);
    while (std::chrono::steady_clock::now () < deadline) {
        {
            std::unique_lock<std::mutex> lock (probe_->mutex);
            if (probe_->cv.wait_for (lock, std::chrono::milliseconds (10),
                                     [probe_, expected_count_] () {
                                         return probe_->callback_count >= expected_count_;
                                     }))
                return true;
        }
        if (probe_->progress_handle)
            (void) drain_completion_via_poller (probe_->progress_handle);
    }

    return false;
}

bool wait_for_request_handler (request_handler_probe_t *probe_)
{
    std::unique_lock<std::mutex> lock (probe_->mutex);
    return probe_->cv.wait_for (lock, std::chrono::milliseconds (SETTLE_TIME * 4),
                                [probe_] () { return probe_->invoked; });
}

bool wait_for_multi_request_count (multi_request_probe_t *probe_, size_t expected_count_)
{
    std::unique_lock<std::mutex> lock (probe_->mutex);
    return probe_->cv.wait_for (
      lock, std::chrono::milliseconds (SETTLE_TIME * 20),
      [probe_, expected_count_] () { return probe_->events.size () >= expected_count_; });
}

void recv_router_request_into_probe (void *router_, request_handler_probe_t *probe_)
{
    const zlink_routing_id_t *peer_rid = NULL;
    uint64_t request_seq = 0;
    zlink_msg_t *parts = NULL;
    size_t part_count = 0;
    TEST_ASSERT_SUCCESS_ERRNO (zlink_router_recv (router_, &peer_rid,
                                                  &request_seq, &parts, &part_count, 0));
    TEST_ASSERT_NOT_NULL (probe_);
    TEST_ASSERT_NOT_NULL (peer_rid);
    {
        std::lock_guard<std::mutex> lock (probe_->mutex);
        probe_->invoked = true;
        probe_->request_seq = request_seq;
        probe_->peer_rid_value = *peer_rid;
        probe_->peer_rid.assign (reinterpret_cast<const char *> (peer_rid->data), peer_rid->size);
        probe_->request_payload = part_count > 0 ? msg_to_string (&parts[0]) : std::string ();
    }
    zlink_multipart_close (parts, part_count);
    probe_->cv.notify_all ();
}

void recv_router_request_into_event (void *router_, multi_request_probe_t *probe_)
{
    const zlink_routing_id_t *peer_rid = NULL;
    uint64_t request_seq = 0;
    zlink_msg_t *parts = NULL;
    size_t part_count = 0;
    TEST_ASSERT_SUCCESS_ERRNO (zlink_router_recv (router_, &peer_rid,
                                                  &request_seq, &parts, &part_count, 0));
    TEST_ASSERT_NOT_NULL (probe_);
    TEST_ASSERT_NOT_NULL (peer_rid);

    request_event_t event;
    event.request_seq = request_seq;
    event.peer_rid_value = *peer_rid;
    event.peer_rid.assign (reinterpret_cast<const char *> (peer_rid->data), peer_rid->size);
    event.request_payload = part_count > 0 ? msg_to_string (&parts[0]) : std::string ();

    {
        std::lock_guard<std::mutex> lock (probe_->mutex);
        probe_->events.push_back (event);
    }
    zlink_multipart_close (parts, part_count);
    probe_->cv.notify_all ();
}

int run_request_reply_exit_child ()
{
    void *ctx = zlink_ctx_new ();
    if (!ctx)
        return 10;

    void *router = zlink_socket (ctx, ZLINK_SOCKET_ROUTER);
    void *dealer = zlink_socket (ctx, ZLINK_SOCKET_DEALER);
    if (!router || !dealer)
        return 11;

    set_routing_id_text (dealer, "rr-exit-dealer");
    if (zlink_bind (router, "inproc://rr-exit-regression") != 0
        || zlink_connect (dealer, "inproc://rr-exit-regression") != 0)
        return 12;

    msleep (SETTLE_TIME);

    zlink_msg_t request_part;
    zlink_msg_init (&request_part);
    init_string_part (&request_part, "ping");

    reply_probe_t reply_probe;
    reply_probe.progress_handle = dealer;
    if (zlink_dealer_request (dealer, &request_part, 1, &capture_reply, &reply_probe, 0, 3000)
        != ZLINK_SUBMIT_OK)
        return 13;

    const zlink_routing_id_t *source_rid = NULL;
    uint64_t request_seq = 0;
    zlink_msg_t *parts = NULL;
    size_t part_count = 0;
    if (zlink_router_recv (router, &source_rid, &request_seq, &parts, &part_count,
                           ZLINK_RECV_FLAGS_NONE)
        != ZLINK_RECV_OK)
        return 14;

    zlink_msg_t reply_part;
    zlink_msg_init (&reply_part);
    init_string_part (&reply_part, "pong");
    if (zlink_router_reply (router, source_rid, request_seq, &reply_part, 1) != ZLINK_SUBMIT_OK)
        return 15;
    zlink_multipart_close (parts, part_count);

    if (!wait_for_reply (&reply_probe))
        return 16;

    if (zlink_close (dealer) != 0 || zlink_close (router) != 0 || zlink_ctx_term (ctx) != 0)
        return 17;

    return 0;
}

void test_request_reply_process_exits_cleanly_after_round_trip ()
{
#if defined _WIN32
    TEST_IGNORE_MESSAGE ("POSIX-only subprocess regression test");
#else
    pid_t child = fork ();
    TEST_ASSERT_TRUE (child >= 0);

    if (child == 0) {
        setup_test_environment (5);
        const int rc = run_request_reply_exit_child ();
        fflush (NULL);
        std::_Exit (rc);
    }

    const auto deadline = std::chrono::steady_clock::now () + std::chrono::seconds (3);
    int status = 0;
    pid_t wait_rc = 0;
    while (std::chrono::steady_clock::now () < deadline) {
        wait_rc = waitpid (child, &status, WNOHANG);
        TEST_ASSERT_TRUE (wait_rc >= 0);
        if (wait_rc == child)
            break;
        msleep (10);
    }

    if (wait_rc != child) {
        kill (child, SIGKILL);
        (void) waitpid (child, &status, 0);
        TEST_FAIL_MESSAGE ("request/reply child process did not exit after round trip");
    }

    TEST_ASSERT_TRUE (WIFEXITED (status));
    TEST_ASSERT_EQUAL_INT (0, WEXITSTATUS (status));
#endif
}

void capture_reply (zlink_request_result_t result_,
                    zlink_msg_t *parts_,
                    size_t part_count_,
                    void *userdata_)
{
    reply_probe_t *probe = static_cast<reply_probe_t *> (userdata_);
    if (!probe)
        return;

    std::lock_guard<std::mutex> lock (probe->mutex);
    probe->done = true;
    probe->result = result_;
    probe->part_count = part_count_;
    probe->payload = part_count_ > 0 ? msg_to_string (&parts_[0]) : std::string ();
    ++probe->callback_count;
    probe->cv.notify_all ();
}

void send_captured_reply (void *router_,
                          request_handler_probe_t *handler_probe_,
                          const char *reply_payload_)
{
    zlink_msg_t reply_part;
    zlink_msg_init (&reply_part);
    init_string_part (&reply_part, reply_payload_);

    TEST_ASSERT_SUCCESS_ERRNO (zlink_router_reply (router_, &handler_probe_->peer_rid_value,
                                                   handler_probe_->request_seq, &reply_part, 1));
}

void send_router_reply_to_event (void *router_,
                                 const request_event_t &event_,
                                 const char *reply_payload_)
{
    zlink_msg_t reply_part;
    zlink_msg_init (&reply_part);
    init_string_part (&reply_part, reply_payload_);

    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_router_reply (router_, &event_.peer_rid_value, event_.request_seq, &reply_part, 1));
}


void test_dealer_to_router_request_reply_basic ()
{
    void *router = test_context_socket (ZLINK_SOCKET_ROUTER);
    void *dealer = test_context_socket (ZLINK_SOCKET_DEALER);
    TEST_ASSERT_NOT_NULL (router);
    TEST_ASSERT_NOT_NULL (dealer);

    TEST_ASSERT_SUCCESS_ERRNO (zlink_set_routing_id (dealer, "dealer-A", 8));

    request_handler_probe_t handler_probe;
    TEST_ASSERT_SUCCESS_ERRNO (zlink_bind (router, "inproc://zmp-dealer-router-request-reply"));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_connect (dealer, "inproc://zmp-dealer-router-request-reply"));
    msleep (SETTLE_TIME);

    zlink_msg_t request_part;
    zlink_msg_init (&request_part);
    init_string_part (&request_part, "dealer-request");

    reply_probe_t reply_probe;
    reply_probe.progress_handle = dealer;
    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_dealer_request (dealer, &request_part, 1, &capture_reply, &reply_probe, 0, 3000));

    recv_router_request_into_probe (router, &handler_probe);
    msleep (SETTLE_TIME);
    send_captured_reply (router, &handler_probe, "router-reply");
    TEST_ASSERT_TRUE (wait_for_reply (&reply_probe));

    {
        std::lock_guard<std::mutex> lock (handler_probe.mutex);
        TEST_ASSERT_TRUE (handler_probe.invoked);
        TEST_ASSERT_TRUE (handler_probe.request_seq != 0);
        TEST_ASSERT_EQUAL_STRING_LEN ("dealer-A", handler_probe.peer_rid.c_str (),
                                      handler_probe.peer_rid.size ());
        TEST_ASSERT_EQUAL_STRING_LEN ("dealer-request", handler_probe.request_payload.c_str (),
                                      handler_probe.request_payload.size ());
    }

    {
        std::lock_guard<std::mutex> lock (reply_probe.mutex);
        TEST_ASSERT_TRUE (reply_probe.done);
        TEST_ASSERT_EQUAL_INT (ZLINK_REQUEST_OK, reply_probe.result);
        TEST_ASSERT_EQUAL_UINT64 (1, reply_probe.part_count);
        TEST_ASSERT_EQUAL_STRING_LEN ("router-reply", reply_probe.payload.c_str (),
                                      reply_probe.payload.size ());
    }

    test_context_socket_close_zero_linger (dealer);
    test_context_socket_close_zero_linger (router);
}

void test_dealer_receives_unsolicited_message_after_request_reply ()
{
    void *router = test_context_socket (ZLINK_SOCKET_ROUTER);
    void *dealer = test_context_socket (ZLINK_SOCKET_DEALER);
    TEST_ASSERT_NOT_NULL (router);
    TEST_ASSERT_NOT_NULL (dealer);

    TEST_ASSERT_SUCCESS_ERRNO (zlink_set_routing_id (dealer, "dealer-unsolicited", 18));

    request_handler_probe_t handler_probe;
    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_bind (router, "inproc://zmp-dealer-unsolicited-after-request"));
    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_connect (dealer, "inproc://zmp-dealer-unsolicited-after-request"));
    msleep (SETTLE_TIME);

    zlink_msg_t request_part;
    zlink_msg_init (&request_part);
    init_string_part (&request_part, "request");

    reply_probe_t reply_probe;
    reply_probe.progress_handle = dealer;
    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_dealer_request (dealer, &request_part, 1, &capture_reply, &reply_probe, 0, 3000));

    recv_router_request_into_probe (router, &handler_probe);
    send_captured_reply (router, &handler_probe, "reply");
    TEST_ASSERT_TRUE (wait_for_reply (&reply_probe));
    msleep (SETTLE_TIME);

    zlink_msg_t unsolicited_part;
    zlink_msg_init (&unsolicited_part);
    init_string_part (&unsolicited_part, "unsolicited");
    TEST_ASSERT_EQUAL_INT (
      ZLINK_SUBMIT_OK,
      zlink_send_part_rid (router, &handler_probe.peer_rid_value, &unsolicited_part,
                           ZLINK_SEND_FLAGS_NONE, ZLINK_PART_FINAL));

    uint8_t message_type = 0;
    uint64_t request_seq = 0;
    zlink_part_flag_t has_more = ZLINK_PART_MORE;
    zlink_msg_t received_part;
    zlink_msg_init (&received_part);
    TEST_ASSERT_EQUAL_INT (
      ZLINK_RECV_OK,
      recv_dealer_part_with_retry (dealer, &message_type, &request_seq, &received_part,
                                   &has_more));
    TEST_ASSERT_EQUAL_UINT8 (ZLINK_DEALER_MESSAGE_RAW, message_type);
    TEST_ASSERT_EQUAL_UINT64 (0, request_seq);
    TEST_ASSERT_EQUAL_INT (ZLINK_PART_FINAL, has_more);
    TEST_ASSERT_EQUAL_STRING ("unsolicited", part_to_string_and_close (&received_part).c_str ());

    test_context_socket_close_zero_linger (dealer);
    test_context_socket_close_zero_linger (router);
}

void test_dealer_to_router_request_reply_over_tcp_with_explicit_routing_id ()
{
    void *router = test_context_socket (ZLINK_SOCKET_ROUTER);
    void *dealer = test_context_socket (ZLINK_SOCKET_DEALER);
    TEST_ASSERT_NOT_NULL (router);
    TEST_ASSERT_NOT_NULL (dealer);

    char endpoint[MAX_SOCKET_STRING];
    bind_loopback_ipv4 (router, endpoint, sizeof (endpoint));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_set_routing_id (dealer, "dealer-tcp", 10));

    TEST_ASSERT_SUCCESS_ERRNO (zlink_connect (dealer, endpoint));
    msleep (SETTLE_TIME * 50);

    request_handler_probe_t handler_probe;

    zlink_msg_t request_part;
    zlink_msg_init (&request_part);
    init_string_part (&request_part, "dealer-request-tcp");

    reply_probe_t reply_probe;
    reply_probe.progress_handle = dealer;
    TEST_ASSERT_SUCCESS_ERRNO (zlink_dealer_request (dealer, &request_part, 1, &capture_reply,
                                                     &reply_probe, ZLINK_SEND_FLAGS_NONE, 5000));

    recv_router_request_into_probe (router, &handler_probe);
    msleep (SETTLE_TIME);
    send_captured_reply (router, &handler_probe, "router-reply-tcp");
    TEST_ASSERT_TRUE (wait_for_reply (&reply_probe));

    {
        std::lock_guard<std::mutex> lock (handler_probe.mutex);
        TEST_ASSERT_TRUE (handler_probe.invoked);
        TEST_ASSERT_TRUE (handler_probe.request_seq != 0);
        TEST_ASSERT_EQUAL_STRING_LEN ("dealer-tcp", handler_probe.peer_rid.c_str (),
                                      handler_probe.peer_rid.size ());
        TEST_ASSERT_EQUAL_STRING_LEN ("dealer-request-tcp", handler_probe.request_payload.c_str (),
                                      handler_probe.request_payload.size ());
    }

    {
        std::lock_guard<std::mutex> lock (reply_probe.mutex);
        TEST_ASSERT_TRUE (reply_probe.done);
        TEST_ASSERT_EQUAL_INT (ZLINK_REQUEST_OK, reply_probe.result);
        TEST_ASSERT_EQUAL_UINT64 (1, reply_probe.part_count);
        TEST_ASSERT_EQUAL_STRING_LEN ("router-reply-tcp", reply_probe.payload.c_str (),
                                      reply_probe.payload.size ());
    }

    test_context_socket_close_zero_linger (dealer);
    test_context_socket_close_zero_linger (router);
}

void test_router_to_router_request_reply_basic ()
{
    void *server_router = test_context_socket (ZLINK_SOCKET_ROUTER);
    void *client_router = test_context_socket (ZLINK_SOCKET_ROUTER);
    TEST_ASSERT_NOT_NULL (server_router);
    TEST_ASSERT_NOT_NULL (client_router);

    const char server_rid[] = "router-srv";
    const char client_rid[] = "router-cli";

    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_set_routing_id (server_router, server_rid, strlen (server_rid)));
    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_set_routing_id (client_router, client_rid, strlen (client_rid)));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_set_router_option (
      client_router, ZLINK_ROUTER_OPT_CONNECT_ROUTING_ID, server_rid, strlen (server_rid)));

    request_handler_probe_t handler_probe;

    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_bind (server_router, "inproc://zmp-router-router-request-reply"));
    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_connect (client_router, "inproc://zmp-router-router-request-reply"));
    msleep (SETTLE_TIME);

    zlink_msg_t request_part;
    zlink_msg_init (&request_part);
    init_string_part (&request_part, "router-request");

    zlink_routing_id_t peer_rid;
    memset (&peer_rid, 0, sizeof (peer_rid));
    memcpy (peer_rid.data, server_rid, strlen (server_rid));
    peer_rid.size = strlen (server_rid);

    reply_probe_t reply_probe;
    reply_probe.progress_handle = client_router;
    TEST_ASSERT_SUCCESS_ERRNO (zlink_router_request (client_router, &peer_rid, &request_part, 1,
                                                     &capture_reply, &reply_probe, 0, 5000));

    recv_router_request_into_probe (server_router, &handler_probe);
    msleep (SETTLE_TIME);
    send_captured_reply (server_router, &handler_probe, "router-router-reply");
    TEST_ASSERT_TRUE (wait_for_reply (&reply_probe));

    {
        std::lock_guard<std::mutex> lock (handler_probe.mutex);
        TEST_ASSERT_TRUE (handler_probe.invoked);
        TEST_ASSERT_TRUE (handler_probe.request_seq != 0);
        TEST_ASSERT_EQUAL_STRING_LEN (client_rid, handler_probe.peer_rid.c_str (),
                                      handler_probe.peer_rid.size ());
        TEST_ASSERT_EQUAL_STRING_LEN ("router-request", handler_probe.request_payload.c_str (),
                                      handler_probe.request_payload.size ());
    }

    {
        std::lock_guard<std::mutex> lock (reply_probe.mutex);
        TEST_ASSERT_TRUE (reply_probe.done);
        TEST_ASSERT_EQUAL_INT (ZLINK_REQUEST_OK, reply_probe.result);
        TEST_ASSERT_EQUAL_UINT64 (1, reply_probe.part_count);
        TEST_ASSERT_EQUAL_STRING_LEN ("router-router-reply", reply_probe.payload.c_str (),
                                      reply_probe.payload.size ());
    }

    test_context_socket_close_zero_linger (client_router);
    test_context_socket_close_zero_linger (server_router);
}

void test_connect_only_router_requester_receives_reply ()
{
    void *server_router = test_context_socket (ZLINK_SOCKET_ROUTER);
    void *client_router = test_context_socket (ZLINK_SOCKET_ROUTER);
    TEST_ASSERT_NOT_NULL (server_router);
    TEST_ASSERT_NOT_NULL (client_router);

    const char server_rid[] = "connect-only-srv";
    const char client_rid[] = "connect-only-cli";

    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_set_routing_id (server_router, server_rid, strlen (server_rid)));
    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_set_routing_id (client_router, client_rid, strlen (client_rid)));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_set_router_option (
      client_router, ZLINK_ROUTER_OPT_CONNECT_ROUTING_ID, server_rid, strlen (server_rid)));

    request_handler_probe_t handler_probe;

    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_bind (server_router, "inproc://zmp-router-connect-only-requester"));
    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_connect (client_router, "inproc://zmp-router-connect-only-requester"));
    msleep (SETTLE_TIME);

    zlink_msg_t request_part;
    zlink_msg_init (&request_part);
    init_string_part (&request_part, "connect-only-request");

    zlink_routing_id_t peer_rid;
    memset (&peer_rid, 0, sizeof (peer_rid));
    memcpy (peer_rid.data, server_rid, strlen (server_rid));
    peer_rid.size = strlen (server_rid);

    reply_probe_t reply_probe;
    reply_probe.progress_handle = client_router;
    TEST_ASSERT_SUCCESS_ERRNO (zlink_router_request (client_router, &peer_rid, &request_part, 1,
                                                     &capture_reply, &reply_probe, 0, 5000));

    recv_router_request_into_probe (server_router, &handler_probe);
    msleep (SETTLE_TIME);
    send_captured_reply (server_router, &handler_probe, "connect-only-reply");
    TEST_ASSERT_TRUE (wait_for_reply (&reply_probe));

    {
        std::lock_guard<std::mutex> lock (handler_probe.mutex);
        TEST_ASSERT_TRUE (handler_probe.invoked);
        TEST_ASSERT_TRUE (handler_probe.request_seq != 0);
        TEST_ASSERT_EQUAL_STRING_LEN (client_rid, handler_probe.peer_rid.c_str (),
                                      handler_probe.peer_rid.size ());
        TEST_ASSERT_EQUAL_STRING_LEN ("connect-only-request", handler_probe.request_payload.c_str (),
                                      handler_probe.request_payload.size ());
    }

    {
        std::lock_guard<std::mutex> lock (reply_probe.mutex);
        TEST_ASSERT_TRUE (reply_probe.done);
        TEST_ASSERT_EQUAL_INT (ZLINK_REQUEST_OK, reply_probe.result);
        TEST_ASSERT_EQUAL_UINT64 (1, reply_probe.part_count);
        TEST_ASSERT_EQUAL_STRING_LEN ("connect-only-reply", reply_probe.payload.c_str (),
                                      reply_probe.payload.size ());
    }

    test_context_socket_close_zero_linger (client_router);
    test_context_socket_close_zero_linger (server_router);
}

void test_multiple_in_flight_requests_complete_independently ()
{
    void *server_router = test_context_socket (ZLINK_SOCKET_ROUTER);
    void *client_router = test_context_socket (ZLINK_SOCKET_ROUTER);
    TEST_ASSERT_NOT_NULL (server_router);
    TEST_ASSERT_NOT_NULL (client_router);

    const char server_rid[] = "router-srv";
    const char client_rid[] = "router-cli";
    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_set_routing_id (server_router, server_rid, strlen (server_rid)));
    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_set_routing_id (client_router, client_rid, strlen (client_rid)));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_set_router_option (
      client_router, ZLINK_ROUTER_OPT_CONNECT_ROUTING_ID, server_rid, strlen (server_rid)));

    multi_request_probe_t request_probe;

    TEST_ASSERT_SUCCESS_ERRNO (zlink_bind (server_router, "inproc://zmp-router-multi-inflight"));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_connect (client_router, "inproc://zmp-router-multi-inflight"));
    msleep (SETTLE_TIME);

    zlink_routing_id_t peer_rid;
    memset (&peer_rid, 0, sizeof (peer_rid));
    memcpy (peer_rid.data, server_rid, strlen (server_rid));
    peer_rid.size = strlen (server_rid);

    zlink_msg_t request_a;
    zlink_msg_t request_b;
    zlink_msg_init (&request_a);
    zlink_msg_init (&request_b);
    init_string_part (&request_a, "request-A");
    init_string_part (&request_b, "request-B");

    reply_probe_t reply_a;
    reply_probe_t reply_b;
    reply_a.progress_handle = client_router;
    reply_b.progress_handle = client_router;
    TEST_ASSERT_SUCCESS_ERRNO (zlink_router_request (client_router, &peer_rid, &request_a, 1,
                                                     &capture_reply, &reply_a, 0, 3000));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_router_request (client_router, &peer_rid, &request_b, 1,
                                                     &capture_reply, &reply_b, 0, 3000));

    recv_router_request_into_event (server_router, &request_probe);
    recv_router_request_into_event (server_router, &request_probe);

    request_event_t first;
    request_event_t second;
    {
        std::lock_guard<std::mutex> lock (request_probe.mutex);
        first = request_probe.events[0];
        second = request_probe.events[1];
    }

    TEST_ASSERT_TRUE (first.request_seq != second.request_seq);
    send_router_reply_to_event (server_router, first, "reply-A");
    send_router_reply_to_event (server_router, second, "reply-B");

    TEST_ASSERT_TRUE (wait_for_reply (&reply_a));
    TEST_ASSERT_TRUE (wait_for_reply (&reply_b));

    {
        std::lock_guard<std::mutex> lock (reply_a.mutex);
        TEST_ASSERT_EQUAL_INT (ZLINK_REQUEST_OK, reply_a.result);
        TEST_ASSERT_EQUAL_STRING_LEN ("reply-A", reply_a.payload.c_str (), reply_a.payload.size ());
    }
    {
        std::lock_guard<std::mutex> lock (reply_b.mutex);
        TEST_ASSERT_EQUAL_INT (ZLINK_REQUEST_OK, reply_b.result);
        TEST_ASSERT_EQUAL_STRING_LEN ("reply-B", reply_b.payload.c_str (), reply_b.payload.size ());
    }

    msleep (SETTLE_TIME);
    test_context_socket_close_zero_linger (client_router);
    test_context_socket_close_zero_linger (server_router);
}

void test_out_of_order_replies_match_original_request ()
{
    void *server_router = test_context_socket (ZLINK_SOCKET_ROUTER);
    void *client_router = test_context_socket (ZLINK_SOCKET_ROUTER);
    TEST_ASSERT_NOT_NULL (server_router);
    TEST_ASSERT_NOT_NULL (client_router);

    const char server_rid[] = "router-srv";
    const char client_rid[] = "router-cli";
    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_set_routing_id (server_router, server_rid, strlen (server_rid)));
    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_set_routing_id (client_router, client_rid, strlen (client_rid)));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_set_router_option (
      client_router, ZLINK_ROUTER_OPT_CONNECT_ROUTING_ID, server_rid, strlen (server_rid)));

    multi_request_probe_t request_probe;

    TEST_ASSERT_SUCCESS_ERRNO (zlink_bind (server_router, "inproc://zmp-router-out-of-order"));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_connect (client_router, "inproc://zmp-router-out-of-order"));
    msleep (SETTLE_TIME);

    zlink_routing_id_t peer_rid;
    memset (&peer_rid, 0, sizeof (peer_rid));
    memcpy (peer_rid.data, server_rid, strlen (server_rid));
    peer_rid.size = strlen (server_rid);

    zlink_msg_t request_a;
    zlink_msg_t request_b;
    zlink_msg_init (&request_a);
    zlink_msg_init (&request_b);
    init_string_part (&request_a, "request-first");
    init_string_part (&request_b, "request-second");

    reply_probe_t reply_a;
    reply_probe_t reply_b;
    reply_a.progress_handle = client_router;
    reply_b.progress_handle = client_router;
    TEST_ASSERT_SUCCESS_ERRNO (zlink_router_request (client_router, &peer_rid, &request_a, 1,
                                                     &capture_reply, &reply_a, 0, 3000));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_router_request (client_router, &peer_rid, &request_b, 1,
                                                     &capture_reply, &reply_b, 0, 3000));

    recv_router_request_into_event (server_router, &request_probe);
    recv_router_request_into_event (server_router, &request_probe);

    request_event_t first;
    request_event_t second;
    {
        std::lock_guard<std::mutex> lock (request_probe.mutex);
        first = request_probe.events[0];
        second = request_probe.events[1];
    }

    send_router_reply_to_event (server_router, second, "reply-second");
    send_router_reply_to_event (server_router, first, "reply-first");

    TEST_ASSERT_TRUE (wait_for_reply (&reply_a));
    TEST_ASSERT_TRUE (wait_for_reply (&reply_b));

    {
        std::lock_guard<std::mutex> lock (reply_a.mutex);
        TEST_ASSERT_EQUAL_STRING_LEN ("reply-first", reply_a.payload.c_str (),
                                      reply_a.payload.size ());
    }
    {
        std::lock_guard<std::mutex> lock (reply_b.mutex);
        TEST_ASSERT_EQUAL_STRING_LEN ("reply-second", reply_b.payload.c_str (),
                                      reply_b.payload.size ());
    }

    msleep (SETTLE_TIME);
    test_context_socket_close_zero_linger (client_router);
    test_context_socket_close_zero_linger (server_router);
}

void test_extra_reply_is_dropped_after_first_completion ()
{
    void *server_router = test_context_socket (ZLINK_SOCKET_ROUTER);
    void *client_router = test_context_socket (ZLINK_SOCKET_ROUTER);
    TEST_ASSERT_NOT_NULL (server_router);
    TEST_ASSERT_NOT_NULL (client_router);

    const char server_rid[] = "router-srv";
    const char client_rid[] = "router-cli";
    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_set_routing_id (server_router, server_rid, strlen (server_rid)));
    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_set_routing_id (client_router, client_rid, strlen (client_rid)));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_set_router_option (
      client_router, ZLINK_ROUTER_OPT_CONNECT_ROUTING_ID, server_rid, strlen (server_rid)));

    request_handler_probe_t handler_probe;

    TEST_ASSERT_SUCCESS_ERRNO (zlink_bind (server_router, "inproc://zmp-router-extra-reply"));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_connect (client_router, "inproc://zmp-router-extra-reply"));
    msleep (SETTLE_TIME);

    zlink_routing_id_t peer_rid;
    memset (&peer_rid, 0, sizeof (peer_rid));
    memcpy (peer_rid.data, server_rid, strlen (server_rid));
    peer_rid.size = strlen (server_rid);

    zlink_msg_t request_part;
    zlink_msg_init (&request_part);
    init_string_part (&request_part, "request-extra");

    reply_probe_t reply_probe;
    reply_probe.progress_handle = client_router;
    TEST_ASSERT_SUCCESS_ERRNO (zlink_router_request (client_router, &peer_rid, &request_part, 1,
                                                     &capture_reply, &reply_probe, 0, 3000));

    recv_router_request_into_probe (server_router, &handler_probe);
    TEST_ASSERT_TRUE (wait_for_request_handler (&handler_probe));
    send_captured_reply (server_router, &handler_probe, "reply-first");

    TEST_ASSERT_TRUE (wait_for_reply_count (&reply_probe, 1));
    send_captured_reply (server_router, &handler_probe, "reply-second");
    msleep (SETTLE_TIME * 2);

    {
        std::lock_guard<std::mutex> lock (reply_probe.mutex);
        TEST_ASSERT_EQUAL_UINT64 (1, reply_probe.callback_count);
        TEST_ASSERT_EQUAL_STRING_LEN ("reply-first", reply_probe.payload.c_str (),
                                      reply_probe.payload.size ());
    }

    msleep (SETTLE_TIME);
    test_context_socket_close_zero_linger (client_router);
    test_context_socket_close_zero_linger (server_router);
}

void test_dealer_to_dealer_reply_routes_to_source_peer_and_closes ()
{
    void *server_dealer = test_context_socket (ZLINK_SOCKET_DEALER);
    void *client_a = test_context_socket (ZLINK_SOCKET_DEALER);
    void *client_b = test_context_socket (ZLINK_SOCKET_DEALER);
    TEST_ASSERT_NOT_NULL (server_dealer);
    TEST_ASSERT_NOT_NULL (client_a);
    TEST_ASSERT_NOT_NULL (client_b);

    TEST_ASSERT_SUCCESS_ERRNO (zlink_bind (server_dealer, "inproc://zmp-dealer-dealer-reply"));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_connect (client_a, "inproc://zmp-dealer-dealer-reply"));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_connect (client_b, "inproc://zmp-dealer-dealer-reply"));
    msleep (SETTLE_TIME);

    zlink_msg_t request_a;
    zlink_msg_t request_b;
    zlink_msg_init (&request_a);
    zlink_msg_init (&request_b);
    init_string_part (&request_a, "from-a");
    init_string_part (&request_b, "from-b");

    reply_probe_t reply_a;
    reply_probe_t reply_b;
    reply_a.progress_handle = client_a;
    reply_b.progress_handle = client_b;
    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_dealer_request (client_a, &request_a, 1, &capture_reply, &reply_a, 0, 3000));
    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_dealer_request (client_b, &request_b, 1, &capture_reply, &reply_b, 0, 3000));

    uint64_t seq_a = 0;
    uint64_t seq_b = 0;
    for (int i = 0; i < 2; ++i) {
        uint8_t message_type = 0;
        uint64_t request_seq = 0;
        zlink_part_flag_t has_more = ZLINK_PART_FINAL;
        zlink_msg_t part;
        zlink_msg_init (&part);
        TEST_ASSERT_SUCCESS_ERRNO (recv_dealer_part_with_retry (server_dealer, &message_type,
                                                                &request_seq, &part, &has_more));
        TEST_ASSERT_EQUAL_UINT8 (ZLINK_DEALER_MESSAGE_REQUEST, message_type);
        TEST_ASSERT_TRUE (request_seq != 0);
        TEST_ASSERT_EQUAL_INT (ZLINK_PART_FINAL, has_more);
        const std::string payload = part_to_string_and_close (&part);
        if (payload == "from-a")
            seq_a = request_seq;
        else if (payload == "from-b")
            seq_b = request_seq;
        else
            TEST_FAIL_MESSAGE ("unexpected dealer request payload");
    }

    TEST_ASSERT_TRUE (seq_a != 0);
    TEST_ASSERT_TRUE (seq_b != 0);
    TEST_ASSERT_TRUE (seq_a != seq_b);

    zlink_msg_t reply_part_b;
    zlink_msg_t reply_part_a;
    zlink_msg_init (&reply_part_b);
    zlink_msg_init (&reply_part_a);
    init_string_part (&reply_part_b, "reply-b");
    init_string_part (&reply_part_a, "reply-a");
    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_dealer_reply_part (server_dealer, seq_b, &reply_part_b, ZLINK_PART_FINAL));
    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_dealer_reply_part (server_dealer, seq_a, &reply_part_a, ZLINK_PART_FINAL));

    TEST_ASSERT_TRUE (wait_for_reply (&reply_a));
    TEST_ASSERT_TRUE (wait_for_reply (&reply_b));

    {
        std::lock_guard<std::mutex> lock (reply_a.mutex);
        TEST_ASSERT_EQUAL_INT (ZLINK_REQUEST_OK, reply_a.result);
        TEST_ASSERT_EQUAL_STRING_LEN ("reply-a", reply_a.payload.c_str (), reply_a.payload.size ());
    }
    {
        std::lock_guard<std::mutex> lock (reply_b.mutex);
        TEST_ASSERT_EQUAL_INT (ZLINK_REQUEST_OK, reply_b.result);
        TEST_ASSERT_EQUAL_STRING_LEN ("reply-b", reply_b.payload.c_str (), reply_b.payload.size ());
    }

    msleep (SETTLE_TIME);
    test_context_socket_close (client_a);
    test_context_socket_close (client_b);
    test_context_socket_close (server_dealer);
}

void test_dealer_to_dealer_multipart_reply_preserves_large_first_part ()
{
    void *server_dealer = test_context_socket (ZLINK_SOCKET_DEALER);
    void *client_dealer = test_context_socket (ZLINK_SOCKET_DEALER);
    TEST_ASSERT_NOT_NULL (server_dealer);
    TEST_ASSERT_NOT_NULL (client_dealer);

    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_bind (server_dealer, "inproc://zmp-dealer-large-first-reply"));
    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_connect (client_dealer, "inproc://zmp-dealer-large-first-reply"));
    msleep (SETTLE_TIME);

    zlink_msg_t request_part;
    zlink_msg_init (&request_part);
    init_string_part (&request_part, "request");

    reply_probe_t reply_probe;
    reply_probe.progress_handle = client_dealer;
    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_dealer_request (client_dealer, &request_part, 1, &capture_reply, &reply_probe, 0,
                            3000));

    uint8_t message_type = 0;
    uint64_t request_seq = 0;
    zlink_part_flag_t has_more = ZLINK_PART_FINAL;
    zlink_msg_t received;
    zlink_msg_init (&received);
    TEST_ASSERT_SUCCESS_ERRNO (recv_dealer_part_with_retry (
      server_dealer, &message_type, &request_seq, &received, &has_more));
    TEST_ASSERT_EQUAL_UINT8 (ZLINK_DEALER_MESSAGE_REQUEST, message_type);
    TEST_ASSERT_TRUE (request_seq != 0);
    TEST_ASSERT_EQUAL_INT (ZLINK_PART_FINAL, has_more);
    TEST_ASSERT_EQUAL_STRING ("request", part_to_string_and_close (&received).c_str ());

    const std::string large_first = std::string (320, 'x');
    zlink_msg_t reply_first;
    zlink_msg_t reply_second;
    zlink_msg_init (&reply_first);
    zlink_msg_init (&reply_second);
    TEST_ASSERT_SUCCESS_ERRNO (zlink_msg_init_size (&reply_first, large_first.size ()));
    memcpy (zlink_msg_data (&reply_first), large_first.data (), large_first.size ());
    init_string_part (&reply_second, "reply-body");
    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_dealer_reply_part (server_dealer, request_seq, &reply_first, ZLINK_PART_MORE));
    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_dealer_reply_part (server_dealer, request_seq, &reply_second, ZLINK_PART_FINAL));

    TEST_ASSERT_TRUE (wait_for_reply (&reply_probe));
    {
        std::lock_guard<std::mutex> lock (reply_probe.mutex);
        TEST_ASSERT_EQUAL_INT (ZLINK_REQUEST_OK, reply_probe.result);
        TEST_ASSERT_EQUAL_UINT64 (2, reply_probe.part_count);
        TEST_ASSERT_EQUAL_STRING_LEN (large_first.c_str (), reply_probe.payload.c_str (),
                                      large_first.size ());
    }

    msleep (SETTLE_TIME);
    test_context_socket_close (client_dealer);
    test_context_socket_close (server_dealer);
}

void test_dealer_request_receive_without_reply_closes_cleanly ()
{
    void *server_dealer = test_context_socket (ZLINK_SOCKET_DEALER);
    void *client_dealer = test_context_socket (ZLINK_SOCKET_DEALER);
    TEST_ASSERT_NOT_NULL (server_dealer);
    TEST_ASSERT_NOT_NULL (client_dealer);

    TEST_ASSERT_SUCCESS_ERRNO (zlink_bind (server_dealer, "inproc://zmp-dealer-unreplied-close"));
    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_connect (client_dealer, "inproc://zmp-dealer-unreplied-close"));
    msleep (SETTLE_TIME);

    zlink_msg_t request_part;
    zlink_msg_init (&request_part);
    init_string_part (&request_part, "unreplied");

    reply_probe_t reply_probe;
    reply_probe.progress_handle = client_dealer;
    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_dealer_request (client_dealer, &request_part, 1, &capture_reply, &reply_probe, 0, 50));

    uint8_t message_type = 0;
    uint64_t request_seq = 0;
    zlink_part_flag_t has_more = ZLINK_PART_FINAL;
    zlink_msg_t received;
    zlink_msg_init (&received);
    TEST_ASSERT_SUCCESS_ERRNO (recv_dealer_part_with_retry (server_dealer, &message_type,
                                                            &request_seq, &received, &has_more));
    TEST_ASSERT_EQUAL_UINT8 (ZLINK_DEALER_MESSAGE_REQUEST, message_type);
    TEST_ASSERT_TRUE (request_seq != 0);
    const std::string received_payload = part_to_string_and_close (&received);
    TEST_ASSERT_EQUAL_STRING_LEN ("unreplied", received_payload.c_str (), received_payload.size ());

    TEST_ASSERT_TRUE (wait_for_reply (&reply_probe));
    {
        std::lock_guard<std::mutex> lock (reply_probe.mutex);
        TEST_ASSERT_EQUAL_INT (ZLINK_REQUEST_TIMED_OUT, reply_probe.result);
    }

    msleep (SETTLE_TIME);
    test_context_socket_close (client_dealer);
    test_context_socket_close (server_dealer);
}

void test_router_request_rejects_non_router_target ()
{
    void *dealer = test_context_socket (ZLINK_SOCKET_DEALER);
    void *router = test_context_socket (ZLINK_SOCKET_ROUTER);
    TEST_ASSERT_NOT_NULL (dealer);
    TEST_ASSERT_NOT_NULL (router);

    TEST_ASSERT_SUCCESS_ERRNO (zlink_set_routing_id (dealer, "dealer-peer", 11));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_set_routing_id (router, "router-cli", 10));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_bind (dealer, "inproc://zmp-router-wrong-target"));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_connect (router, "inproc://zmp-router-wrong-target"));
    msleep (SETTLE_TIME);

    zlink_routing_id_t peer_rid;
    memset (&peer_rid, 0, sizeof (peer_rid));
    memcpy (peer_rid.data, "dealer-peer", 11);
    peer_rid.size = 11;

    zlink_msg_t request_part;
    zlink_msg_init (&request_part);
    init_string_part (&request_part, "router-request");

    reply_probe_t reply_probe;
    reply_probe.progress_handle = router;
    TEST_ASSERT_SUCCESS_ERRNO (zlink_router_request (router, &peer_rid, &request_part, 1,
                                                     &capture_reply, &reply_probe, 0, 50));
    TEST_ASSERT_TRUE (wait_for_reply (&reply_probe));

    {
        std::lock_guard<std::mutex> lock (reply_probe.mutex);
        TEST_ASSERT_EQUAL_INT (ZLINK_REQUEST_TIMED_OUT, reply_probe.result);
        TEST_ASSERT_EQUAL_UINT64 (0, reply_probe.part_count);
        TEST_ASSERT_EQUAL_UINT64 (1, reply_probe.callback_count);
    }

    msleep (SETTLE_TIME);
    test_context_socket_close_zero_linger (router);
    test_context_socket_close_zero_linger (dealer);
}

void test_dealer_request_uses_socket_default_timeout_when_reply_is_missing ()
{
    void *router = test_context_socket (ZLINK_SOCKET_ROUTER);
    void *dealer = test_context_socket (ZLINK_SOCKET_DEALER);
    TEST_ASSERT_NOT_NULL (router);
    TEST_ASSERT_NOT_NULL (dealer);

    TEST_ASSERT_SUCCESS_ERRNO (zlink_set_routing_id (dealer, "dealer-A", 8));
    const int default_timeout_ms = 50;
    TEST_ASSERT_SUCCESS_ERRNO (zlink_set_dealer_option (dealer, ZLINK_DEALER_OPT_REQUEST_TIMEOUT_MS,
                                                        &default_timeout_ms,
                                                        sizeof (default_timeout_ms)));
    configure_submit_retry (dealer);

    int observed_timeout_ms = 0;
    size_t observed_timeout_size = sizeof (observed_timeout_ms);
    TEST_ASSERT_SUCCESS_ERRNO (zlink_get_router_option (
      router, ZLINK_ROUTER_OPT_REQUEST_TIMEOUT_MS, &observed_timeout_ms, &observed_timeout_size));
    TEST_ASSERT_EQUAL_INT (5000, observed_timeout_ms);

    request_handler_probe_t handler_probe;

    TEST_ASSERT_SUCCESS_ERRNO (zlink_bind (router, "inproc://zmp-dealer-default-timeout"));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_connect (dealer, "inproc://zmp-dealer-default-timeout"));
    msleep (SETTLE_TIME);

    zlink_msg_t request_part;
    zlink_msg_init (&request_part);
    init_string_part (&request_part, "dealer-timeout-request");

    reply_probe_t reply_probe;
    reply_probe.progress_handle = dealer;
    const std::chrono::steady_clock::time_point start = std::chrono::steady_clock::now ();
    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_dealer_request (dealer, &request_part, 1, &capture_reply, &reply_probe, 0, 0));

    recv_router_request_into_probe (router, &handler_probe);
    TEST_ASSERT_TRUE (wait_for_reply (&reply_probe));
    const long elapsed_ms =
      static_cast<long> (std::chrono::duration_cast<std::chrono::milliseconds> (
                           std::chrono::steady_clock::now () - start)
                           .count ());

    {
        std::lock_guard<std::mutex> lock (reply_probe.mutex);
        TEST_ASSERT_TRUE (reply_probe.done);
        TEST_ASSERT_EQUAL_INT (ZLINK_REQUEST_TIMED_OUT, reply_probe.result);
        TEST_ASSERT_EQUAL_UINT64 (0, reply_probe.part_count);
        TEST_ASSERT_EQUAL_UINT64 (1, reply_probe.callback_count);
        TEST_ASSERT_TRUE_MESSAGE (elapsed_ms < 500,
                                  "submit retry must not retry request completion timeout");
    }

    msleep (SETTLE_TIME);
    test_context_socket_close_zero_linger (dealer);
    test_context_socket_close_zero_linger (router);
}
}

int main ()
{
    setup_test_environment (180);

    UNITY_BEGIN ();
    RUN_TEST (test_dealer_to_router_request_reply_basic);
    RUN_TEST (test_dealer_receives_unsolicited_message_after_request_reply);
    RUN_TEST (test_dealer_to_router_request_reply_over_tcp_with_explicit_routing_id);
    RUN_TEST (test_router_to_router_request_reply_basic);
    RUN_TEST (test_connect_only_router_requester_receives_reply);
    RUN_TEST (test_multiple_in_flight_requests_complete_independently);
    RUN_TEST (test_out_of_order_replies_match_original_request);
    RUN_TEST (test_extra_reply_is_dropped_after_first_completion);
    RUN_TEST (test_dealer_to_dealer_reply_routes_to_source_peer_and_closes);
    RUN_TEST (test_dealer_to_dealer_multipart_reply_preserves_large_first_part);
    RUN_TEST (test_dealer_request_receive_without_reply_closes_cleanly);
    RUN_TEST (test_router_request_rejects_non_router_target);
    RUN_TEST (test_dealer_request_uses_socket_default_timeout_when_reply_is_missing);
    RUN_TEST (test_request_reply_process_exits_cleanly_after_round_trip);
    const int rc = UNITY_END ();
    fflush (NULL);
    std::_Exit (rc);
}
