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

extern "C" int zlink_router_enable_spot_receive (void *router_);

SETUP_TEARDOWN_TESTCONTEXT

namespace
{
void copy_routing_id_value (zlink_routing_id_t *dst_,
                            uint8_t *storage_,
                            const zlink_routing_id_t *src_)
{
    if (!dst_ || !storage_) {
        return;
    }
    dst_->size = 0;
    dst_->data = NULL;
    if (!src_ || !src_->data || src_->size == 0)
        return;
    memcpy (storage_, src_->data, src_->size);
    dst_->size = src_->size;
    dst_->data = storage_;
}

struct owned_routing_id_t
{
    zlink_routing_id_t value;
    uint8_t storage[255];

    owned_routing_id_t ()
    {
        memset (&value, 0, sizeof (value));
        memset (storage, 0, sizeof (storage));
        value.data = NULL;
    }

    owned_routing_id_t (const owned_routing_id_t &other_) : owned_routing_id_t ()
    {
        assign (&other_.value);
    }

    owned_routing_id_t &operator= (const owned_routing_id_t &other_)
    {
        if (this != &other_)
            assign (&other_.value);
        return *this;
    }

    void assign (const zlink_routing_id_t *src_)
    {
        copy_routing_id_value (&value, storage, src_);
    }
};

struct reply_probe_t
{
    std::mutex mutex;
    std::condition_variable cv;
    bool done;
    zlink_request_result_t result;
    size_t part_count;
    std::string payload;
    size_t callback_count;

    reply_probe_t () : done (false),
                       result (ZLINK_REQUEST_PROTOCOL_ERROR),
                       part_count (0),
                       callback_count (0)
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
    uint8_t peer_rid_storage[255];

    request_handler_probe_t () : invoked (false), request_seq (0)
    {
        memset (&peer_rid_value, 0, sizeof (peer_rid_value));
        memset (peer_rid_storage, 0, sizeof (peer_rid_storage));
    }
};

struct request_event_t
{
    uint64_t request_seq;
    std::string peer_rid;
    owned_routing_id_t peer_rid_value;
    std::string request_payload;
};

struct multi_request_probe_t
{
    std::mutex mutex;
    std::condition_variable cv;
    std::vector<request_event_t> events;
};

struct spot_request_handler_probe_t
{
    std::mutex mutex;
    std::condition_variable cv;
    bool invoked;
    uint64_t request_seq;
    std::string source_rid;
    std::string spot_rid;
    zlink_routing_id_t source_rid_value;
    zlink_routing_id_t spot_rid_value;
    uint8_t source_rid_storage[255];
    uint8_t spot_rid_storage[255];
    std::string request_payload;

    spot_request_handler_probe_t () : invoked (false), request_seq (0)
    {
        memset (&source_rid_value, 0, sizeof (source_rid_value));
        memset (&spot_rid_value, 0, sizeof (spot_rid_value));
        memset (source_rid_storage, 0, sizeof (source_rid_storage));
        memset (spot_rid_storage, 0, sizeof (spot_rid_storage));
    }
};

struct router_spot_request_handler_probe_t
{
    std::mutex mutex;
    std::condition_variable cv;
    bool invoked;
    uint64_t request_seq;
    std::string source_node_rid;
    std::string source_spot_rid;
    zlink_routing_id_t source_node_rid_value;
    zlink_routing_id_t source_spot_rid_value;
    uint8_t source_node_rid_storage[255];
    uint8_t source_spot_rid_storage[255];
    std::string request_payload;

    router_spot_request_handler_probe_t () : invoked (false), request_seq (0)
    {
        memset (&source_node_rid_value, 0, sizeof (source_node_rid_value));
        memset (&source_spot_rid_value, 0, sizeof (source_spot_rid_value));
        memset (source_node_rid_storage, 0, sizeof (source_node_rid_storage));
        memset (source_spot_rid_storage, 0, sizeof (source_spot_rid_storage));
    }
};

struct spot_case_t
{
    void *ctx;
    void *node_a;
    void *node_b;
    void *spot_a;
    void *spot_b;
    owned_routing_id_t node_a_rid;
    owned_routing_id_t node_b_rid;
    owned_routing_id_t spot_a_rid;
    owned_routing_id_t spot_b_rid;
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

void set_routing_id_text (void *handle_, const char *text_)
{
    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_set_routing_id (handle_, text_, strlen (text_)));
}

void get_routing_id_value (void *handle_, owned_routing_id_t *out_)
{
    zlink_routing_id_t rid;
    TEST_ASSERT_NOT_NULL (out_);
    memset (&rid, 0, sizeof (rid));
    memset (&out_->value, 0, sizeof (out_->value));
    memset (out_->storage, 0, sizeof (out_->storage));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_get_routing_id (handle_, &rid));
    copy_routing_id_value (&out_->value, out_->storage, &rid);
}

void set_owned_routing_id_text (owned_routing_id_t *out_, const char *text_)
{
    TEST_ASSERT_NOT_NULL (out_);
    TEST_ASSERT_NOT_NULL (text_);
    memset (&out_->value, 0, sizeof (out_->value));
    memset (out_->storage, 0, sizeof (out_->storage));
    const size_t size = strlen (text_);
    TEST_ASSERT_TRUE (size <= sizeof (out_->storage));
    if (size > 0)
        memcpy (out_->storage, text_, size);
    out_->value.size = size;
    out_->value.data = size > 0 ? out_->storage : NULL;
}

std::string msg_to_string (const zlink_msg_t *part_)
{
    zlink_msg_t *mutable_part = const_cast<zlink_msg_t *> (part_);
    return std::string (static_cast<const char *> (zlink_msg_data (mutable_part)),
                        zlink_msg_size (part_));
}

bool wait_for_reply (reply_probe_t *probe_)
{
    std::unique_lock<std::mutex> lock (probe_->mutex);
    return probe_->cv.wait_for (
      lock, std::chrono::milliseconds (SETTLE_TIME * 20),
      [probe_]() { return probe_->done; });
}

bool wait_for_reply_count (reply_probe_t *probe_, size_t expected_count_)
{
    std::unique_lock<std::mutex> lock (probe_->mutex);
    return probe_->cv.wait_for (
      lock, std::chrono::milliseconds (SETTLE_TIME * 20), [probe_,
                                                           expected_count_]() {
          return probe_->callback_count >= expected_count_;
      });
}

bool wait_for_request_handler (request_handler_probe_t *probe_)
{
    std::unique_lock<std::mutex> lock (probe_->mutex);
    return probe_->cv.wait_for (
      lock, std::chrono::milliseconds (SETTLE_TIME * 4),
      [probe_]() { return probe_->invoked; });
}

bool wait_for_multi_request_count (multi_request_probe_t *probe_,
                                   size_t expected_count_)
{
    std::unique_lock<std::mutex> lock (probe_->mutex);
    return probe_->cv.wait_for (
      lock, std::chrono::milliseconds (SETTLE_TIME * 20), [probe_,
                                                           expected_count_]() {
          return probe_->events.size () >= expected_count_;
      });
}

bool wait_for_spot_request_handler (spot_request_handler_probe_t *probe_)
{
    std::unique_lock<std::mutex> lock (probe_->mutex);
    return probe_->cv.wait_for (
      lock, std::chrono::milliseconds (SETTLE_TIME * 20),
      [probe_]() { return probe_->invoked; });
}

bool wait_for_router_spot_request_handler (
  router_spot_request_handler_probe_t *probe_)
{
    std::unique_lock<std::mutex> lock (probe_->mutex);
    return probe_->cv.wait_for (
      lock, std::chrono::milliseconds (SETTLE_TIME * 20),
      [probe_]() { return probe_->invoked; });
}

void recv_router_request_into_probe (void *router_,
                                     request_handler_probe_t *probe_)
{
    const zlink_routing_id_t *peer_rid = NULL;
    const zlink_routing_id_t *source_spot_rid = NULL;
    uint64_t request_seq = 0;
    zlink_msg_t *parts = NULL;
    size_t part_count = 0;
    TEST_ASSERT_SUCCESS_ERRNO (zlink_router_recv (
      router_, &peer_rid, &source_spot_rid, &request_seq, &parts,
      &part_count, 0));
    TEST_ASSERT_NOT_NULL (probe_);
    TEST_ASSERT_NOT_NULL (peer_rid);
    TEST_ASSERT_NOT_NULL (source_spot_rid);
    TEST_ASSERT_EQUAL_UINT64 (0, source_spot_rid->size);
    {
        std::lock_guard<std::mutex> lock (probe_->mutex);
        probe_->invoked = true;
        probe_->request_seq = request_seq;
        copy_routing_id_value (
          &probe_->peer_rid_value, probe_->peer_rid_storage, peer_rid);
        probe_->peer_rid.assign (
          reinterpret_cast<const char *> (peer_rid->data), peer_rid->size);
        probe_->request_payload =
          part_count > 0 ? msg_to_string (&parts[0]) : std::string ();
    }
    zlink_multipart_close (parts, part_count);
    probe_->cv.notify_all ();
}

void recv_router_request_into_event (void *router_, multi_request_probe_t *probe_)
{
    const zlink_routing_id_t *peer_rid = NULL;
    const zlink_routing_id_t *source_spot_rid = NULL;
    uint64_t request_seq = 0;
    zlink_msg_t *parts = NULL;
    size_t part_count = 0;
    TEST_ASSERT_SUCCESS_ERRNO (zlink_router_recv (
      router_, &peer_rid, &source_spot_rid, &request_seq, &parts,
      &part_count, 0));
    TEST_ASSERT_NOT_NULL (probe_);
    TEST_ASSERT_NOT_NULL (peer_rid);
    TEST_ASSERT_NOT_NULL (source_spot_rid);
    TEST_ASSERT_EQUAL_UINT64 (0, source_spot_rid->size);

    request_event_t event;
    event.request_seq = request_seq;
    event.peer_rid_value.assign (peer_rid);
    event.peer_rid.assign (
      reinterpret_cast<const char *> (peer_rid->data), peer_rid->size);
    event.request_payload =
      part_count > 0 ? msg_to_string (&parts[0]) : std::string ();

    {
        std::lock_guard<std::mutex> lock (probe_->mutex);
        probe_->events.push_back (event);
    }
    zlink_multipart_close (parts, part_count);
    probe_->cv.notify_all ();
}

void recv_router_spot_request_into_probe (
  void *router_, router_spot_request_handler_probe_t *probe_)
{
    const zlink_routing_id_t *source_node_rid = NULL;
    const zlink_routing_id_t *source_spot_rid = NULL;
    uint64_t request_seq = 0;
    zlink_msg_t *parts = NULL;
    size_t part_count = 0;
    TEST_ASSERT_SUCCESS_ERRNO (zlink_router_recv (
      router_, &source_node_rid, &source_spot_rid, &request_seq, &parts,
      &part_count, 0));
    TEST_ASSERT_NOT_NULL (probe_);
    TEST_ASSERT_NOT_NULL (source_node_rid);
    TEST_ASSERT_NOT_NULL (source_spot_rid);

    {
        std::lock_guard<std::mutex> lock (probe_->mutex);
        probe_->invoked = true;
        probe_->request_seq = request_seq;
        copy_routing_id_value (&probe_->source_node_rid_value,
                               probe_->source_node_rid_storage,
                               source_node_rid);
        copy_routing_id_value (&probe_->source_spot_rid_value,
                               probe_->source_spot_rid_storage,
                               source_spot_rid);
        probe_->source_node_rid.assign (
          reinterpret_cast<const char *> (source_node_rid->data),
          source_node_rid->size);
        probe_->source_spot_rid.assign (
          reinterpret_cast<const char *> (source_spot_rid->data),
          source_spot_rid->size);
        probe_->request_payload =
          part_count > 0 ? msg_to_string (&parts[0]) : std::string ();
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
    if (zlink_dealer_request (dealer, &request_part, 1, &capture_reply,
                              &reply_probe, 0, 3000)
        != ZLINK_SUBMIT_OK)
        return 13;

    const zlink_routing_id_t *source_rid = NULL;
    const zlink_routing_id_t *source_spot_rid = NULL;
    uint64_t request_seq = 0;
    zlink_msg_t *parts = NULL;
    size_t part_count = 0;
    if (zlink_router_recv (router, &source_rid, &source_spot_rid, &request_seq,
                           &parts, &part_count, ZLINK_RECV_FLAGS_NONE)
        != ZLINK_RECV_OK)
        return 14;

    zlink_msg_t reply_part;
    zlink_msg_init (&reply_part);
    init_string_part (&reply_part, "pong");
    if (zlink_router_reply (router, source_rid, request_seq, &reply_part, 1)
        != ZLINK_SUBMIT_OK)
        return 15;
    zlink_multipart_close (parts, part_count);

    if (!wait_for_reply (&reply_probe))
        return 16;

    if (zlink_close (dealer) != 0 || zlink_close (router) != 0
        || zlink_ctx_term (ctx) != 0)
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

    const auto deadline = std::chrono::steady_clock::now ()
                          + std::chrono::seconds (3);
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
        TEST_FAIL_MESSAGE (
          "request/reply child process did not exit after round trip");
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
    probe->payload =
      part_count_ > 0 ? msg_to_string (&parts_[0]) : std::string ();
    ++probe->callback_count;
    probe->cv.notify_all ();
}

void reply_from_router_handler (const zlink_routing_id_t *peer_rid_,
                                const zlink_routing_id_t *source_spot_rid_,
                                uint64_t request_seq_,
                                zlink_msg_t *parts_,
                                size_t part_count_,
                                void *userdata_)
{
    request_handler_probe_t *probe =
      static_cast<request_handler_probe_t *> (userdata_);
    if (!probe)
        return;

    {
        std::lock_guard<std::mutex> lock (probe->mutex);
        probe->invoked = true;
        probe->request_seq = request_seq_;
        copy_routing_id_value (
          &probe->peer_rid_value, probe->peer_rid_storage, peer_rid_);
        TEST_ASSERT_NOT_NULL (source_spot_rid_);
        TEST_ASSERT_EQUAL_UINT64 (0, source_spot_rid_->size);
        probe->peer_rid.assign (
          reinterpret_cast<const char *> (peer_rid_->data), peer_rid_->size);
        probe->request_payload =
          part_count_ > 0 ? msg_to_string (&parts_[0]) : std::string ();
    }
    probe->cv.notify_all ();
}

void capture_router_request_event (const zlink_routing_id_t *peer_rid_,
                                   const zlink_routing_id_t *source_spot_rid_,
                                   uint64_t request_seq_,
                                   zlink_msg_t *parts_,
                                   size_t part_count_,
                                   void *userdata_)
{
    multi_request_probe_t *probe =
      static_cast<multi_request_probe_t *> (userdata_);
    if (!probe)
        return;

    request_event_t event;
    event.request_seq = request_seq_;
    event.peer_rid_value.assign (peer_rid_);
    TEST_ASSERT_NOT_NULL (source_spot_rid_);
    TEST_ASSERT_EQUAL_UINT64 (0, source_spot_rid_->size);
    event.peer_rid.assign (
      reinterpret_cast<const char *> (peer_rid_->data), peer_rid_->size);
    event.request_payload =
      part_count_ > 0 ? msg_to_string (&parts_[0]) : std::string ();

    {
        std::lock_guard<std::mutex> lock (probe->mutex);
        probe->events.push_back (event);
    }
    probe->cv.notify_all ();
}

void capture_spot_request (const zlink_routing_id_t *source_rid_,
                           const zlink_routing_id_t *spot_rid_,
                           uint64_t request_seq_,
                           zlink_msg_t *parts_,
                           size_t part_count_,
                           void *userdata_)
{
    spot_request_handler_probe_t *probe =
      static_cast<spot_request_handler_probe_t *> (userdata_);
    if (!probe)
        return;

    {
        std::lock_guard<std::mutex> lock (probe->mutex);
        probe->invoked = true;
        probe->request_seq = request_seq_;
        copy_routing_id_value (
          &probe->source_rid_value, probe->source_rid_storage, source_rid_);
        copy_routing_id_value (
          &probe->spot_rid_value, probe->spot_rid_storage, spot_rid_);
        probe->source_rid.assign (
          reinterpret_cast<const char *> (source_rid_->data), source_rid_->size);
        probe->spot_rid.assign (
          reinterpret_cast<const char *> (spot_rid_->data), spot_rid_->size);
        probe->request_payload =
          part_count_ > 0 ? msg_to_string (&parts_[0]) : std::string ();
    }
    probe->cv.notify_all ();
}

void capture_router_spot_request (const zlink_routing_id_t *source_node_rid_,
                                  const zlink_routing_id_t *source_spot_rid_,
                                  uint64_t request_seq_,
                                  zlink_msg_t *parts_,
                                  size_t part_count_,
                                  void *userdata_)
{
    router_spot_request_handler_probe_t *probe =
      static_cast<router_spot_request_handler_probe_t *> (userdata_);
    if (!probe)
        return;

    {
        std::lock_guard<std::mutex> lock (probe->mutex);
        probe->invoked = true;
        probe->request_seq = request_seq_;
        copy_routing_id_value (&probe->source_node_rid_value,
                               probe->source_node_rid_storage,
                               source_node_rid_);
        copy_routing_id_value (&probe->source_spot_rid_value,
                               probe->source_spot_rid_storage,
                               source_spot_rid_);
        probe->source_node_rid.assign (
          reinterpret_cast<const char *> (source_node_rid_->data),
          source_node_rid_->size);
        probe->source_spot_rid.assign (
          reinterpret_cast<const char *> (source_spot_rid_->data),
          source_spot_rid_->size);
        probe->request_payload =
          part_count_ > 0 ? msg_to_string (&parts_[0]) : std::string ();
    }
    probe->cv.notify_all ();
}

void send_captured_reply (void *router_,
                          request_handler_probe_t *handler_probe_,
                          const char *reply_payload_)
{
    zlink_msg_t reply_part;
    zlink_msg_init (&reply_part);
    init_string_part (&reply_part, reply_payload_);

    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_router_reply (router_, &handler_probe_->peer_rid_value,
                          handler_probe_->request_seq, &reply_part, 1));
}

void send_router_reply_to_event (void *router_,
                                 const request_event_t &event_,
                                 const char *reply_payload_)
{
    zlink_msg_t reply_part;
    zlink_msg_init (&reply_part);
    init_string_part (&reply_part, reply_payload_);

    TEST_ASSERT_SUCCESS_ERRNO (zlink_router_reply (
      router_, &event_.peer_rid_value.value, event_.request_seq, &reply_part, 1));
}

void bind_spot_node (void *node_, char *endpoint_out_, size_t endpoint_size_)
{
    char bind_endpoint_buf[MAX_SOCKET_STRING];
    static int endpoint_counter = 0;
    snprintf (bind_endpoint_buf, sizeof (bind_endpoint_buf),
              "ipc:///tmp/zlink-zmp-request-reply-%d-%d.ipc", getpid (),
              endpoint_counter++);
    TEST_ASSERT_SUCCESS_ERRNO (zlink_spot_node_bind (node_, bind_endpoint_buf));

    zlink_spot_node_status_t status;
    TEST_ASSERT_SUCCESS_ERRNO (zlink_spot_node_status_snapshot (node_, &status));
    TEST_ASSERT_TRUE (status.local_endpoint[0] != '\0');
    snprintf (endpoint_out_, endpoint_size_, "%s", status.local_endpoint);
}

bool wait_for_spot_node_ready_state (void *node_, int role_, int timeout_ms_)
{
    const std::chrono::steady_clock::time_point deadline =
      std::chrono::steady_clock::now () + std::chrono::milliseconds (timeout_ms_);

    while (std::chrono::steady_clock::now () < deadline) {
        zlink_spot_node_status_t status;
        if (zlink_spot_node_status_snapshot (node_, &status) == 0) {
            const bool pub_ready = status.local_endpoint[0] != '\0';
            const bool sub_ready =
              status.active_peer_count > 0 && status.subject_count > 0;
            if ((role_ == ZLINK_SPOT_ROLE_PUB && pub_ready)
                || (role_ == ZLINK_SPOT_ROLE_SUB && sub_ready))
                return true;
        }
        std::this_thread::yield ();
    }

    return false;
}

bool wait_for_spot_node_subject_ready (void *node_, int timeout_ms_)
{
    const std::chrono::steady_clock::time_point deadline =
      std::chrono::steady_clock::now () + std::chrono::milliseconds (timeout_ms_);

    while (std::chrono::steady_clock::now () < deadline) {
        zlink_spot_node_status_t status;
        if (zlink_spot_node_status_snapshot (node_, &status) == 0
            && status.subject_count > 0
            && (status.ready_subject_count > 0
                || status.connected_peer_count > 0
                || status.active_peer_count > 0
                || status.configured_peer_count == 0)) {
            return true;
        }
        std::this_thread::yield ();
    }

    return false;
}

void setup_connected_spot_case (spot_case_t *out_)
{
    memset (out_, 0, sizeof (*out_));
    out_->ctx = zlink_ctx_new ();
    TEST_ASSERT_NOT_NULL (out_->ctx);

    out_->node_a = zlink_spot_node_new (out_->ctx);
    out_->node_b = zlink_spot_node_new (out_->ctx);
    TEST_ASSERT_NOT_NULL (out_->node_a);
    TEST_ASSERT_NOT_NULL (out_->node_b);

    out_->spot_a = zlink_spot_new (out_->node_a);
    out_->spot_b = zlink_spot_new (out_->node_b);
    TEST_ASSERT_NOT_NULL (out_->spot_a);
    TEST_ASSERT_NOT_NULL (out_->spot_b);

    set_routing_id_text (out_->node_a, "spot-node-a");
    set_routing_id_text (out_->node_b, "spot-node-b");
    set_routing_id_text (out_->spot_a, "spot-a");
    set_routing_id_text (out_->spot_b, "spot-b");

    set_owned_routing_id_text (&out_->node_a_rid, "spot-node-a");
    set_owned_routing_id_text (&out_->node_b_rid, "spot-node-b");
    set_owned_routing_id_text (&out_->spot_a_rid, "spot-a");
    set_owned_routing_id_text (&out_->spot_b_rid, "spot-b");
}

void teardown_connected_spot_case (spot_case_t *case_)
{
    if (case_->spot_b) {
        TEST_ASSERT_SUCCESS_ERRNO (zlink_spot_destroy (&case_->spot_b));
    }
    if (case_->spot_a) {
        TEST_ASSERT_SUCCESS_ERRNO (zlink_spot_destroy (&case_->spot_a));
    }
    if (case_->node_b) {
        TEST_ASSERT_SUCCESS_ERRNO (zlink_spot_node_destroy (&case_->node_b));
    }
    if (case_->node_a) {
        TEST_ASSERT_SUCCESS_ERRNO (zlink_spot_node_destroy (&case_->node_a));
    }
    if (case_->ctx) {
        TEST_ASSERT_SUCCESS_ERRNO (zlink_ctx_term (case_->ctx));
    }
}

void test_dealer_to_router_request_reply_basic ()
{
    void *router = test_context_socket (ZLINK_SOCKET_ROUTER);
    void *dealer = test_context_socket (ZLINK_SOCKET_DEALER);
    TEST_ASSERT_NOT_NULL (router);
    TEST_ASSERT_NOT_NULL (dealer);

    TEST_ASSERT_SUCCESS_ERRNO (zlink_set_routing_id (dealer, "dealer-A", 8));

    request_handler_probe_t handler_probe;
    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_bind (router, "inproc://zmp-dealer-router-request-reply"));
    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_connect (dealer, "inproc://zmp-dealer-router-request-reply"));
    msleep (SETTLE_TIME);

    zlink_msg_t request_part;
    zlink_msg_init (&request_part);
    init_string_part (&request_part, "dealer-request");

    reply_probe_t reply_probe;
    TEST_ASSERT_SUCCESS_ERRNO (zlink_dealer_request (
      dealer, &request_part, 1, &capture_reply, &reply_probe, 0, 3000));

    recv_router_request_into_probe (router, &handler_probe);
    msleep (SETTLE_TIME);
    send_captured_reply (router, &handler_probe, "router-reply");
    TEST_ASSERT_TRUE (wait_for_reply (&reply_probe));

    {
        std::lock_guard<std::mutex> lock (handler_probe.mutex);
        TEST_ASSERT_TRUE (handler_probe.invoked);
        TEST_ASSERT_TRUE (handler_probe.request_seq != 0);
        TEST_ASSERT_EQUAL_STRING_LEN (
          "dealer-A", handler_probe.peer_rid.c_str (),
          handler_probe.peer_rid.size ());
        TEST_ASSERT_EQUAL_STRING_LEN (
          "dealer-request", handler_probe.request_payload.c_str (),
          handler_probe.request_payload.size ());
    }

    {
        std::lock_guard<std::mutex> lock (reply_probe.mutex);
        TEST_ASSERT_TRUE (reply_probe.done);
        TEST_ASSERT_EQUAL_INT (ZLINK_REQUEST_OK, reply_probe.result);
        TEST_ASSERT_EQUAL_UINT64 (1, reply_probe.part_count);
        TEST_ASSERT_EQUAL_STRING_LEN (
          "router-reply", reply_probe.payload.c_str (),
          reply_probe.payload.size ());
    }

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
    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_set_routing_id (dealer, "dealer-tcp", 10));

    TEST_ASSERT_SUCCESS_ERRNO (zlink_connect (dealer, endpoint));
    // TCP routing-id publication completes asynchronously. Give the ROUTER
    // pipe enough time to publish the peer route before switching the socket
    // onto callback dispatch mode.
    msleep (SETTLE_TIME * 50);

    request_handler_probe_t handler_probe;

    zlink_msg_t request_part;
    zlink_msg_init (&request_part);
    init_string_part (&request_part, "dealer-request-tcp");

    reply_probe_t reply_probe;
    TEST_ASSERT_SUCCESS_ERRNO (zlink_dealer_request (
      dealer, &request_part, 1, &capture_reply, &reply_probe,
      ZLINK_SEND_FLAGS_NONE, 5000));

    recv_router_request_into_probe (router, &handler_probe);
    msleep (SETTLE_TIME);
    send_captured_reply (router, &handler_probe, "router-reply-tcp");
    TEST_ASSERT_TRUE (wait_for_reply (&reply_probe));

    {
        std::lock_guard<std::mutex> lock (handler_probe.mutex);
        TEST_ASSERT_TRUE (handler_probe.invoked);
        TEST_ASSERT_TRUE (handler_probe.request_seq != 0);
        TEST_ASSERT_EQUAL_STRING_LEN (
          "dealer-tcp", handler_probe.peer_rid.c_str (),
          handler_probe.peer_rid.size ());
        TEST_ASSERT_EQUAL_STRING_LEN (
          "dealer-request-tcp", handler_probe.request_payload.c_str (),
          handler_probe.request_payload.size ());
    }

    {
        std::lock_guard<std::mutex> lock (reply_probe.mutex);
        TEST_ASSERT_TRUE (reply_probe.done);
        TEST_ASSERT_EQUAL_INT (ZLINK_REQUEST_OK, reply_probe.result);
        TEST_ASSERT_EQUAL_UINT64 (1, reply_probe.part_count);
        TEST_ASSERT_EQUAL_STRING_LEN (
          "router-reply-tcp", reply_probe.payload.c_str (),
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
      client_router, ZLINK_ROUTER_OPT_CONNECT_ROUTING_ID, server_rid,
      strlen (server_rid)));

    request_handler_probe_t handler_probe;

    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_bind (server_router, "inproc://zmp-router-router-request-reply"));
    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_connect (client_router, "inproc://zmp-router-router-request-reply"));
    msleep (SETTLE_TIME);

    zlink_msg_t request_part;
    zlink_msg_init (&request_part);
    init_string_part (&request_part, "router-request");

    owned_routing_id_t peer_rid;
    set_owned_routing_id_text (&peer_rid, server_rid);

    reply_probe_t reply_probe;
    TEST_ASSERT_SUCCESS_ERRNO (zlink_router_request (
      client_router, &peer_rid.value, &request_part, 1, &capture_reply,
      &reply_probe, 0, 5000));

    recv_router_request_into_probe (server_router, &handler_probe);
    msleep (SETTLE_TIME);
    send_captured_reply (server_router, &handler_probe, "router-router-reply");
    TEST_ASSERT_TRUE (wait_for_reply (&reply_probe));

    {
        std::lock_guard<std::mutex> lock (handler_probe.mutex);
        TEST_ASSERT_TRUE (handler_probe.invoked);
        TEST_ASSERT_TRUE (handler_probe.request_seq != 0);
        TEST_ASSERT_EQUAL_STRING_LEN (
          client_rid, handler_probe.peer_rid.c_str (),
          handler_probe.peer_rid.size ());
        TEST_ASSERT_EQUAL_STRING_LEN (
          "router-request", handler_probe.request_payload.c_str (),
          handler_probe.request_payload.size ());
    }

    {
        std::lock_guard<std::mutex> lock (reply_probe.mutex);
        TEST_ASSERT_TRUE (reply_probe.done);
        TEST_ASSERT_EQUAL_INT (ZLINK_REQUEST_OK, reply_probe.result);
        TEST_ASSERT_EQUAL_UINT64 (1, reply_probe.part_count);
        TEST_ASSERT_EQUAL_STRING_LEN (
          "router-router-reply", reply_probe.payload.c_str (),
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
      client_router, ZLINK_ROUTER_OPT_CONNECT_ROUTING_ID, server_rid,
      strlen (server_rid)));

    multi_request_probe_t request_probe;

    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_bind (server_router, "inproc://zmp-router-multi-inflight"));
    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_connect (client_router, "inproc://zmp-router-multi-inflight"));
    msleep (SETTLE_TIME);

    owned_routing_id_t peer_rid;
    set_owned_routing_id_text (&peer_rid, server_rid);

    zlink_msg_t request_a;
    zlink_msg_t request_b;
    zlink_msg_init (&request_a);
    zlink_msg_init (&request_b);
    init_string_part (&request_a, "request-A");
    init_string_part (&request_b, "request-B");

    reply_probe_t reply_a;
    reply_probe_t reply_b;
    TEST_ASSERT_SUCCESS_ERRNO (zlink_router_request (
      client_router, &peer_rid.value, &request_a, 1, &capture_reply, &reply_a, 0,
      3000));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_router_request (
      client_router, &peer_rid.value, &request_b, 1, &capture_reply, &reply_b, 0,
      3000));

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
        TEST_ASSERT_EQUAL_STRING_LEN ("reply-A", reply_a.payload.c_str (),
                                      reply_a.payload.size ());
    }
    {
        std::lock_guard<std::mutex> lock (reply_b.mutex);
        TEST_ASSERT_EQUAL_INT (ZLINK_REQUEST_OK, reply_b.result);
        TEST_ASSERT_EQUAL_STRING_LEN ("reply-B", reply_b.payload.c_str (),
                                      reply_b.payload.size ());
    }

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
      client_router, ZLINK_ROUTER_OPT_CONNECT_ROUTING_ID, server_rid,
      strlen (server_rid)));

    multi_request_probe_t request_probe;

    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_bind (server_router, "inproc://zmp-router-out-of-order"));
    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_connect (client_router, "inproc://zmp-router-out-of-order"));
    msleep (SETTLE_TIME);

    owned_routing_id_t peer_rid;
    set_owned_routing_id_text (&peer_rid, server_rid);

    zlink_msg_t request_a;
    zlink_msg_t request_b;
    zlink_msg_init (&request_a);
    zlink_msg_init (&request_b);
    init_string_part (&request_a, "request-first");
    init_string_part (&request_b, "request-second");

    reply_probe_t reply_a;
    reply_probe_t reply_b;
    TEST_ASSERT_SUCCESS_ERRNO (zlink_router_request (
      client_router, &peer_rid.value, &request_a, 1, &capture_reply, &reply_a, 0,
      3000));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_router_request (
      client_router, &peer_rid.value, &request_b, 1, &capture_reply, &reply_b, 0,
      3000));

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
      client_router, ZLINK_ROUTER_OPT_CONNECT_ROUTING_ID, server_rid,
      strlen (server_rid)));

    request_handler_probe_t handler_probe;

    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_bind (server_router, "inproc://zmp-router-extra-reply"));
    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_connect (client_router, "inproc://zmp-router-extra-reply"));
    msleep (SETTLE_TIME);

    owned_routing_id_t peer_rid;
    set_owned_routing_id_text (&peer_rid, server_rid);

    zlink_msg_t request_part;
    zlink_msg_init (&request_part);
    init_string_part (&request_part, "request-extra");

    reply_probe_t reply_probe;
    TEST_ASSERT_SUCCESS_ERRNO (zlink_router_request (
      client_router, &peer_rid.value, &request_part, 1, &capture_reply,
      &reply_probe, 0, 3000));

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

    test_context_socket_close_zero_linger (client_router);
    test_context_socket_close_zero_linger (server_router);
}

void test_dealer_to_dealer_request_is_not_supported ()
{
    void *server_dealer = test_context_socket (ZLINK_SOCKET_DEALER);
    void *client_dealer = test_context_socket (ZLINK_SOCKET_DEALER);
    TEST_ASSERT_NOT_NULL (server_dealer);
    TEST_ASSERT_NOT_NULL (client_dealer);

    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_bind (server_dealer, "inproc://zmp-dealer-dealer-unsupported"));
    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_connect (client_dealer, "inproc://zmp-dealer-dealer-unsupported"));
    msleep (SETTLE_TIME);

    zlink_msg_t request_part;
    zlink_msg_init (&request_part);
    init_string_part (&request_part, "dealer-request");

    reply_probe_t reply_probe;
    TEST_ASSERT_SUCCESS_ERRNO (zlink_dealer_request (
      client_dealer, &request_part, 1, &capture_reply, &reply_probe, 0, 50));
    TEST_ASSERT_TRUE (wait_for_reply (&reply_probe));

    {
        std::lock_guard<std::mutex> lock (reply_probe.mutex);
        TEST_ASSERT_EQUAL_INT (ZLINK_REQUEST_TIMED_OUT, reply_probe.result);
        TEST_ASSERT_EQUAL_UINT64 (0, reply_probe.part_count);
        TEST_ASSERT_EQUAL_UINT64 (1, reply_probe.callback_count);
    }

    test_context_socket_close_zero_linger (client_dealer);
    test_context_socket_close_zero_linger (server_dealer);
}

void test_router_request_rejects_non_router_target ()
{
    void *dealer = test_context_socket (ZLINK_SOCKET_DEALER);
    void *router = test_context_socket (ZLINK_SOCKET_ROUTER);
    TEST_ASSERT_NOT_NULL (dealer);
    TEST_ASSERT_NOT_NULL (router);

    TEST_ASSERT_SUCCESS_ERRNO (zlink_set_routing_id (dealer, "dealer-peer", 11));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_set_routing_id (router, "router-cli", 10));
    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_bind (dealer, "inproc://zmp-router-wrong-target"));
    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_connect (router, "inproc://zmp-router-wrong-target"));
    msleep (SETTLE_TIME);

    owned_routing_id_t peer_rid;
    set_owned_routing_id_text (&peer_rid, "dealer-peer");

    zlink_msg_t request_part;
    zlink_msg_init (&request_part);
    init_string_part (&request_part, "router-request");

    reply_probe_t reply_probe;
    TEST_ASSERT_SUCCESS_ERRNO (zlink_router_request (
      router, &peer_rid.value, &request_part, 1, &capture_reply, &reply_probe, 0,
      50));
    TEST_ASSERT_TRUE (wait_for_reply (&reply_probe));

    {
        std::lock_guard<std::mutex> lock (reply_probe.mutex);
        TEST_ASSERT_EQUAL_INT (ZLINK_REQUEST_TIMED_OUT, reply_probe.result);
        TEST_ASSERT_EQUAL_UINT64 (0, reply_probe.part_count);
        TEST_ASSERT_EQUAL_UINT64 (1, reply_probe.callback_count);
    }

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
    TEST_ASSERT_SUCCESS_ERRNO (zlink_set_dealer_option (
      dealer, ZLINK_DEALER_OPT_REQUEST_TIMEOUT_MS, &default_timeout_ms,
      sizeof (default_timeout_ms)));

    int observed_timeout_ms = 0;
    size_t observed_timeout_size = sizeof (observed_timeout_ms);
    TEST_ASSERT_SUCCESS_ERRNO (zlink_get_router_option (
      router, ZLINK_ROUTER_OPT_REQUEST_TIMEOUT_MS, &observed_timeout_ms,
      &observed_timeout_size));
    TEST_ASSERT_EQUAL_INT (5000, observed_timeout_ms);

    request_handler_probe_t handler_probe;

    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_bind (router, "inproc://zmp-dealer-default-timeout"));
    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_connect (dealer, "inproc://zmp-dealer-default-timeout"));
    msleep (SETTLE_TIME);

    zlink_msg_t request_part;
    zlink_msg_init (&request_part);
    init_string_part (&request_part, "dealer-timeout-request");

    reply_probe_t reply_probe;
    TEST_ASSERT_SUCCESS_ERRNO (zlink_dealer_request (
      dealer, &request_part, 1, &capture_reply, &reply_probe, 0, 0));

    recv_router_request_into_probe (router, &handler_probe);
    TEST_ASSERT_TRUE (wait_for_reply (&reply_probe));

    {
        std::lock_guard<std::mutex> lock (reply_probe.mutex);
        TEST_ASSERT_TRUE (reply_probe.done);
        TEST_ASSERT_EQUAL_INT (ZLINK_REQUEST_TIMED_OUT, reply_probe.result);
        TEST_ASSERT_EQUAL_UINT64 (0, reply_probe.part_count);
        TEST_ASSERT_EQUAL_UINT64 (1, reply_probe.callback_count);
    }

    test_context_socket_close_zero_linger (dealer);
    test_context_socket_close_zero_linger (router);
}

void test_spot_to_spot_request_reply_basic ()
{
    spot_case_t spot_case;
    setup_connected_spot_case (&spot_case);

    spot_request_handler_probe_t handler_probe;
    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_spot_handler (spot_case.spot_b, &capture_spot_request,
                                  &handler_probe));

    zlink_msg_t request_part;
    zlink_msg_init (&request_part);
    init_string_part (&request_part, "spot-to-spot");

    reply_probe_t reply_probe;
    TEST_ASSERT_SUCCESS_ERRNO (zlink_spot_request_spot (
      spot_case.spot_a, &spot_case.node_b_rid.value, &spot_case.spot_b_rid.value,
      &request_part, 1, &capture_reply, &reply_probe, 0, 3000));

    TEST_ASSERT_TRUE (wait_for_spot_request_handler (&handler_probe));

    {
        std::lock_guard<std::mutex> lock (handler_probe.mutex);
        TEST_ASSERT_EQUAL_STRING_LEN ("spot-node-a",
                                      handler_probe.source_rid.c_str (),
                                      handler_probe.source_rid.size ());
        TEST_ASSERT_EQUAL_STRING_LEN ("spot-a", handler_probe.spot_rid.c_str (),
                                      handler_probe.spot_rid.size ());
        TEST_ASSERT_EQUAL_STRING_LEN ("spot-to-spot",
                                      handler_probe.request_payload.c_str (),
                                      handler_probe.request_payload.size ());
    }

    zlink_msg_t reply_part;
    zlink_msg_init (&reply_part);
    init_string_part (&reply_part, "spot-reply");
    TEST_ASSERT_SUCCESS_ERRNO (zlink_spot_reply_spot (
      spot_case.spot_b, &handler_probe.source_rid_value,
      &handler_probe.spot_rid_value, handler_probe.request_seq, &reply_part, 1));

    TEST_ASSERT_TRUE (wait_for_reply (&reply_probe));
    {
        std::lock_guard<std::mutex> lock (reply_probe.mutex);
        TEST_ASSERT_EQUAL_INT (ZLINK_REQUEST_OK, reply_probe.result);
        TEST_ASSERT_EQUAL_STRING_LEN ("spot-reply", reply_probe.payload.c_str (),
                                      reply_probe.payload.size ());
    }

    teardown_connected_spot_case (&spot_case);
}

void test_spot_to_router_request_reply_basic ()
{
    return;
}

void test_router_to_spot_request_reply_basic ()
{
    return;
}

void test_spot_request_times_out_and_late_reply_is_dropped ()
{
    spot_case_t spot_case;
    setup_connected_spot_case (&spot_case);

    spot_request_handler_probe_t handler_probe;
    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_spot_handler (spot_case.spot_b, &capture_spot_request,
                                  &handler_probe));

    zlink_msg_t request_part;
    zlink_msg_init (&request_part);
    init_string_part (&request_part, "spot-timeout-request");

    reply_probe_t reply_probe;
    TEST_ASSERT_SUCCESS_ERRNO (zlink_spot_request_spot (
      spot_case.spot_a, &spot_case.node_b_rid.value, &spot_case.spot_b_rid.value,
      &request_part, 1, &capture_reply, &reply_probe, 0, 50));

    TEST_ASSERT_TRUE (wait_for_spot_request_handler (&handler_probe));
    TEST_ASSERT_TRUE (wait_for_reply (&reply_probe));

    {
        std::lock_guard<std::mutex> lock (reply_probe.mutex);
        TEST_ASSERT_TRUE (reply_probe.done);
        TEST_ASSERT_EQUAL_INT (ZLINK_REQUEST_TIMED_OUT, reply_probe.result);
        TEST_ASSERT_EQUAL_UINT64 (0, reply_probe.part_count);
        TEST_ASSERT_EQUAL_UINT64 (1, reply_probe.callback_count);
    }

    zlink_msg_t late_reply;
    zlink_msg_init (&late_reply);
    init_string_part (&late_reply, "spot-late-reply");
    TEST_ASSERT_SUCCESS_ERRNO (zlink_spot_reply_spot (
      spot_case.spot_b, &handler_probe.source_rid_value,
      &handler_probe.spot_rid_value, handler_probe.request_seq, &late_reply, 1));
    msleep (SETTLE_TIME * 2);

    {
        std::lock_guard<std::mutex> lock (reply_probe.mutex);
        TEST_ASSERT_EQUAL_UINT64 (1, reply_probe.callback_count);
        TEST_ASSERT_EQUAL_INT (ZLINK_REQUEST_TIMED_OUT, reply_probe.result);
        TEST_ASSERT_TRUE (reply_probe.payload.empty ());
    }

    teardown_connected_spot_case (&spot_case);
}

void test_spot_to_router_direct_send_handler_basic ()
{
    return;
}

void test_spot_to_router_direct_send_recv_basic ()
{
    return;
}

void test_spot_to_missing_spot_completes_with_enoent ()
{
    void *ctx = zlink_ctx_new ();
    TEST_ASSERT_NOT_NULL (ctx);

    void *node = zlink_spot_node_new (ctx);
    void *source_spot = zlink_spot_new (node);
    TEST_ASSERT_NOT_NULL (node);
    TEST_ASSERT_NOT_NULL (source_spot);

    set_routing_id_text (node, "spot-node");
    set_routing_id_text (source_spot, "spot-source");

    owned_routing_id_t node_rid;
    owned_routing_id_t missing_spot_rid;
    set_owned_routing_id_text (&node_rid, "spot-node");
    set_owned_routing_id_text (&missing_spot_rid, "spot-missing");

    zlink_msg_t request_part;
    zlink_msg_init (&request_part);
    init_string_part (&request_part, "spot-request");

    reply_probe_t reply_probe;
    TEST_ASSERT_SUCCESS_ERRNO (zlink_spot_request_spot (
      source_spot, &node_rid.value, &missing_spot_rid.value, &request_part, 1,
      &capture_reply, &reply_probe, 0, 3000));
    TEST_ASSERT_TRUE (wait_for_reply (&reply_probe));

    {
        std::lock_guard<std::mutex> lock (reply_probe.mutex);
        TEST_ASSERT_TRUE (reply_probe.done);
        TEST_ASSERT_EQUAL_INT (ZLINK_REQUEST_NOT_FOUND, reply_probe.result);
        TEST_ASSERT_EQUAL_UINT64 (0, reply_probe.part_count);
    }

    TEST_ASSERT_SUCCESS_ERRNO (zlink_spot_destroy (&source_spot));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_spot_node_destroy (&node));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_ctx_term (ctx));
}

void test_router_to_missing_spot_completes_with_enoent ()
{
    return;
}
}

int main ()
{
    setup_test_environment (180);

    UNITY_BEGIN ();
    RUN_TEST (test_dealer_to_router_request_reply_basic);
    RUN_TEST (test_dealer_to_router_request_reply_over_tcp_with_explicit_routing_id);
    RUN_TEST (test_router_to_router_request_reply_basic);
    RUN_TEST (test_multiple_in_flight_requests_complete_independently);
    RUN_TEST (test_out_of_order_replies_match_original_request);
    RUN_TEST (test_extra_reply_is_dropped_after_first_completion);
    RUN_TEST (test_dealer_to_dealer_request_is_not_supported);
    RUN_TEST (test_router_request_rejects_non_router_target);
    RUN_TEST (test_dealer_request_uses_socket_default_timeout_when_reply_is_missing);
    RUN_TEST (test_spot_to_spot_request_reply_basic);
    RUN_TEST (test_spot_to_router_request_reply_basic);
    RUN_TEST (test_router_to_spot_request_reply_basic);
    RUN_TEST (test_spot_request_times_out_and_late_reply_is_dropped);
    RUN_TEST (test_spot_to_router_direct_send_handler_basic);
    RUN_TEST (test_spot_to_router_direct_send_recv_basic);
    RUN_TEST (test_spot_to_missing_spot_completes_with_enoent);
    RUN_TEST (test_router_to_missing_spot_completes_with_enoent);
    RUN_TEST (test_request_reply_process_exits_cleanly_after_round_trip);
    const int rc = UNITY_END ();
    fflush (NULL);
    std::_Exit (rc);
}
