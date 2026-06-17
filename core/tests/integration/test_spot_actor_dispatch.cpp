/* SPDX-License-Identifier: MPL-2.0 */

#include "testutil.hpp"
#include "testutil_unity.hpp"
#include "zlink_testing.hpp"

#include <chrono>
#include <condition_variable>
#include <map>
#include <mutex>
#include <set>
#include <string.h>
#include <thread>
#include <vector>

namespace
{
struct test_actor_handle_t
{
    void *node;
    zlink_actor_ref_t ref;
};

std::set<test_actor_handle_t *> g_test_actor_handles;
std::map<std::string, void *> g_test_actor_nodes_by_ref;

void init_text_msg (zlink_msg_t *msg_, const char *text_);
std::string msg_text (zlink_msg_t *msg_);

struct request_wait_t
{
    request_wait_t () : result (ZLINK_REQUEST_INTERNAL_ERROR), done (false) {}

    std::mutex mutex;
    std::condition_variable cv;
    zlink_request_result_t result;
    bool done;
};

struct entry_join_wait_t
{
    entry_join_wait_t () : done (false)
    {
        memset (&result, 0, sizeof (result));
        result.result = ZLINK_REQUEST_INTERNAL_ERROR;
    }

    std::mutex mutex;
    std::condition_variable cv;
    zlink_actor_join_entry_spot_result_t result;
    std::string reply_payload;
    bool done;
};

struct lifecycle_probe_t
{
    lifecycle_probe_t () : joined (0), left (0)
    {
        memset (&last_join, 0, sizeof (last_join));
        memset (&last_leave, 0, sizeof (last_leave));
    }

    std::mutex mutex;
    std::condition_variable cv;
    int joined;
    int left;
    zlink_spot_actor_lifecycle_info_t last_join;
    zlink_spot_actor_lifecycle_info_t last_leave;
};

void request_wait_handler (zlink_request_result_t result_,
                           zlink_msg_t *parts_,
                           size_t part_count_,
                           void *userdata_)
{
    for (size_t i = 0; i < part_count_; ++i)
        zlink_msg_close (&parts_[i]);
    request_wait_t *wait = static_cast<request_wait_t *> (userdata_);
    std::lock_guard<std::mutex> lock (wait->mutex);
    wait->result = result_;
    wait->done = true;
    wait->cv.notify_all ();
}

void entry_join_wait_handler (const zlink_actor_join_entry_spot_result_t *result_,
                              zlink_msg_t *parts_,
                              size_t part_count_,
                              void *userdata_)
{
    entry_join_wait_t *wait = static_cast<entry_join_wait_t *> (userdata_);
    std::lock_guard<std::mutex> lock (wait->mutex);
    if (result_)
        wait->result = *result_;
    if (parts_ && part_count_ > 0)
        wait->reply_payload = msg_text (&parts_[0]);
    if (parts_ && part_count_ > 0)
        zlink_multipart_close (parts_, part_count_);
    wait->done = true;
    wait->cv.notify_all ();
}

void lifecycle_join_handler (void *,
                             const zlink_spot_actor_lifecycle_info_t *info_,
                             void *userdata_)
{
    lifecycle_probe_t *probe = static_cast<lifecycle_probe_t *> (userdata_);
    std::lock_guard<std::mutex> lock (probe->mutex);
    ++probe->joined;
    if (info_)
        probe->last_join = *info_;
    probe->cv.notify_all ();
}

void lifecycle_leave_handler (void *,
                              const zlink_spot_actor_lifecycle_info_t *info_,
                              void *userdata_)
{
    lifecycle_probe_t *probe = static_cast<lifecycle_probe_t *> (userdata_);
    std::lock_guard<std::mutex> lock (probe->mutex);
    ++probe->left;
    if (info_)
        probe->last_leave = *info_;
    probe->cv.notify_all ();
}

void record_lifecycle_event (lifecycle_probe_t *probe_,
                             const zlink_spot_actor_lifecycle_event_t &event_)
{
    if (!probe_)
        return;
    std::lock_guard<std::mutex> lock (probe_->mutex);
    if (event_.kind == ZLINK_SPOT_ACTOR_LIFECYCLE_JOINED) {
        ++probe_->joined;
        probe_->last_join = event_.info;
    } else if (event_.kind == ZLINK_SPOT_ACTOR_LIFECYCLE_LEFT) {
        ++probe_->left;
        probe_->last_leave = event_.info;
    }
    probe_->cv.notify_all ();
}

void drain_lifecycle_events (void *spot_, lifecycle_probe_t *probe_)
{
    for (;;) {
        zlink_spot_actor_lifecycle_event_t event;
        const zlink_recv_result_t rc =
          zlink_spot_recv_actor_lifecycle (spot_, &event, ZLINK_RECV_FLAGS_DONTWAIT);
        if (rc == ZLINK_RECV_NO_DATA)
            return;
        if (rc != ZLINK_RECV_OK)
            return;
        record_lifecycle_event (probe_, event);
    }
}

zlink_request_result_t wait_request_result (request_wait_t *wait_, uint32_t timeout_ms_)
{
    std::unique_lock<std::mutex> lock (wait_->mutex);
    if (!wait_->done) {
        const uint32_t wait_ms = timeout_ms_ == 0 ? 1000 : timeout_ms_ + 1000;
        wait_->cv.wait_for (lock, std::chrono::milliseconds (wait_ms),
                            [wait_] { return wait_->done; });
    }
    return wait_->done ? wait_->result : ZLINK_REQUEST_TIMED_OUT;
}

zlink_actor_join_entry_spot_result_t
wait_spot_node_actor_join_entry_spot (void *node_,
                                      const zlink_actor_ref_t *actor_,
                                      const zlink_routing_id_t *dest_node_rid_,
                                      zlink_msg_t *parts_,
                                      size_t part_count_,
                                      zlink_submit_result_t *submit_out_,
                                      uint32_t timeout_ms_,
                                      std::string *reply_payload_out_ = NULL)
{
    entry_join_wait_t wait;
    const zlink_submit_result_t submit = ::zlink_spot_node_actor_join_entry_spot (
      node_, actor_, dest_node_rid_, parts_, part_count_, entry_join_wait_handler, &wait,
      ZLINK_DONTWAIT, timeout_ms_);
    if (submit_out_)
        *submit_out_ = submit;
    if (submit != ZLINK_SUBMIT_OK)
        return wait.result;

    std::unique_lock<std::mutex> lock (wait.mutex);
    if (!wait.done) {
        const uint32_t wait_ms = timeout_ms_ == 0 ? 1000 : timeout_ms_ + 1000;
        wait.cv.wait_for (lock, std::chrono::milliseconds (wait_ms), [&] { return wait.done; });
    }
    if (!wait.done)
        wait.result.result = ZLINK_REQUEST_TIMED_OUT;
    if (reply_payload_out_)
        *reply_payload_out_ = wait.reply_payload;
    return wait.result;
}

zlink_actor_join_entry_spot_result_t
wait_spot_node_actor_join_entry_spot (void *node_,
                                      const zlink_actor_ref_t *actor_,
                                      const zlink_routing_id_t *dest_node_rid_,
                                      zlink_submit_result_t *submit_out_,
                                      uint32_t timeout_ms_,
                                      std::string *reply_payload_out_ = NULL)
{
    return wait_spot_node_actor_join_entry_spot (node_, actor_, dest_node_rid_, NULL, 0,
                                                submit_out_, timeout_ms_, reply_payload_out_);
}

struct join_handler_adapter_t
{
    zlink_reply_handler_fn handler;
    void *userdata;
};

void join_handler_adapter (const zlink_actor_join_result_t *result_,
                           zlink_msg_t *parts_,
                           size_t part_count_,
                           void *userdata_)
{
    join_handler_adapter_t *adapter = static_cast<join_handler_adapter_t *> (userdata_);
    adapter->handler (result_->result, parts_, part_count_, adapter->userdata);
    delete adapter;
}

zlink_request_result_t
wait_spot_node_actor_destroy (void *node_, const zlink_actor_ref_t *actor_, uint32_t timeout_ms_)
{
    request_wait_t wait;
    const zlink_submit_result_t submit =
      ::zlink_spot_node_actor_destroy (node_, actor_, request_wait_handler, &wait, timeout_ms_);
    if (submit != ZLINK_SUBMIT_OK)
        return ZLINK_REQUEST_INVALID_ARGUMENT;
    return wait_request_result (&wait, timeout_ms_);
}

zlink_request_result_t wait_spot_node_actor_leave_spot (void *node_,
                                                        const zlink_actor_ref_t *actor_,
                                                        const zlink_routing_id_t *current_spot_rid_,
                                                        uint32_t timeout_ms_)
{
    request_wait_t wait;
    const zlink_submit_result_t submit = ::zlink_spot_node_actor_leave_spot (
      node_, actor_, current_spot_rid_, request_wait_handler, &wait, timeout_ms_);
    if (submit != ZLINK_SUBMIT_OK)
        return ZLINK_REQUEST_INVALID_ARGUMENT;
    return wait_request_result (&wait, timeout_ms_);
}

zlink_request_result_t wait_stream_bind_actor (void *node_,
                                               void *stream_,
                                               const zlink_routing_id_t *session_rid_,
                                               const zlink_actor_ref_t *actor_,
                                               uint32_t timeout_ms_)
{
    TEST_ASSERT_EQUAL (ZLINK_CONFIG_OK, zlink_stream_attach_actor_gateway (stream_, node_));
    request_wait_t wait;
    const zlink_submit_result_t submit = ::zlink_stream_bind_actor (
      stream_, session_rid_, actor_, request_wait_handler, &wait, timeout_ms_);
    if (submit != ZLINK_SUBMIT_OK)
        return ZLINK_REQUEST_INVALID_ARGUMENT;
    return wait_request_result (&wait, timeout_ms_);
}

zlink_request_result_t wait_stream_unbind_actor (void *node_,
                                                 void *stream_,
                                                 const zlink_routing_id_t *session_rid_,
                                                 const char *actor_id_,
                                                 uint32_t timeout_ms_)
{
    LIBZLINK_UNUSED (node_);
    request_wait_t wait;
    const zlink_submit_result_t submit = ::zlink_stream_unbind_actor (
      stream_, session_rid_, actor_id_, request_wait_handler, &wait, timeout_ms_);
    if (submit != ZLINK_SUBMIT_OK)
        return ZLINK_REQUEST_INVALID_ARGUMENT;
    return wait_request_result (&wait, timeout_ms_);
}

zlink_submit_result_t test_stream_send_bound_actor_part (void *node_,
                                                         void *stream_,
                                                         const zlink_routing_id_t *session_rid_,
                                                         const char *actor_id_,
                                                         zlink_msg_t *part_,
                                                         zlink_send_flags_t flags_,
                                                         zlink_part_flag_t part_flag_)
{
    LIBZLINK_UNUSED (node_);
    return ::zlink_stream_send_bound_actor_part (stream_, session_rid_, actor_id_, part_, flags_,
                                                 part_flag_);
}

zlink_submit_result_t wait_spot_node_actor_join_spot (void *node_,
                                                      const zlink_actor_ref_t *actor_,
                                                      const zlink_routing_id_t *dest_node_rid_,
                                                      const zlink_routing_id_t *dest_spot_rid_,
                                                      zlink_msg_t *parts_,
                                                      size_t part_count_,
                                                      zlink_reply_handler_fn handler_,
                                                      void *userdata_,
                                                      zlink_send_flags_t flags_,
                                                      uint32_t timeout_ms_)
{
    join_handler_adapter_t *adapter = new join_handler_adapter_t;
    adapter->handler = handler_;
    adapter->userdata = userdata_;
    const zlink_submit_result_t submit = ::zlink_spot_node_actor_join_spot (
      node_, actor_, dest_node_rid_, dest_spot_rid_, parts_, part_count_, join_handler_adapter,
      adapter, flags_, timeout_ms_);
    if (submit != ZLINK_SUBMIT_OK)
        delete adapter;
    return submit;
}

std::string test_rid_key (const zlink_routing_id_t &rid_)
{
    return std::string (reinterpret_cast<const char *> (rid_.data), rid_.size);
}

std::string test_actor_ref_key (const zlink_actor_ref_t &ref_)
{
    return test_rid_key (ref_.node_rid) + ":" + ref_.actor_id;
}

test_actor_handle_t *test_actor_new (void *node_, const char *actor_id_)
{
    zlink_actor_ref_t ref;
    if (zlink_spot_node_actor_new (node_, actor_id_, &ref) != ZLINK_CONFIG_OK)
        return NULL;
    test_actor_handle_t *handle = new test_actor_handle_t;
    handle->node = node_;
    handle->ref = ref;
    g_test_actor_handles.insert (handle);
    g_test_actor_nodes_by_ref[test_actor_ref_key (ref)] = node_;
    return handle;
}

zlink_config_result_t test_actor_get_ref (void *actor_, zlink_actor_ref_t *out_)
{
    test_actor_handle_t *handle = static_cast<test_actor_handle_t *> (actor_);
    if (!handle || g_test_actor_handles.count (handle) == 0 || !out_) {
        errno = EFAULT;
        return ZLINK_CONFIG_INVALID_HANDLE;
    }
    *out_ = handle->ref;
    return ZLINK_CONFIG_OK;
}

zlink_request_result_t test_actor_destroy (void **actor_p_, uint32_t timeout_ms_)
{
    if (!actor_p_ || !*actor_p_) {
        errno = EFAULT;
        return ZLINK_REQUEST_INVALID_ARGUMENT;
    }
    test_actor_handle_t *handle = static_cast<test_actor_handle_t *> (*actor_p_);
    if (g_test_actor_handles.count (handle) == 0) {
        errno = EBUSY;
        return ZLINK_REQUEST_BUSY;
    }
    const zlink_request_result_t rc =
      wait_spot_node_actor_destroy (handle->node, &handle->ref, timeout_ms_);
    if (rc == ZLINK_REQUEST_OK) {
        g_test_actor_nodes_by_ref.erase (test_actor_ref_key (handle->ref));
        g_test_actor_handles.erase (handle);
        delete handle;
        *actor_p_ = NULL;
    }
    return rc;
}

zlink_submit_result_t test_actor_join_spot (void *actor_,
                                            void *spot_,
                                            zlink_msg_t *message_,
                                            zlink_reply_handler_fn handler_,
                                            void *userdata_,
                                            zlink_send_flags_t flags_,
                                            uint32_t timeout_ms_)
{
    test_actor_handle_t *handle = static_cast<test_actor_handle_t *> (actor_);
    if (!handle || g_test_actor_handles.count (handle) == 0)
        return ZLINK_SUBMIT_INVALID_ARGUMENT;
    zlink_routing_id_t spot_rid;
    if (zlink_get_routing_id (spot_, &spot_rid) != ZLINK_CONFIG_OK)
        return ZLINK_SUBMIT_INVALID_ARGUMENT;
    return wait_spot_node_actor_join_spot (handle->node, &handle->ref, &handle->ref.node_rid,
                                           &spot_rid, message_, 1, handler_, userdata_, flags_,
                                           timeout_ms_);
}

zlink_config_result_t test_actor_leave_spot (void *actor_, void *spot_)
{
    test_actor_handle_t *handle = static_cast<test_actor_handle_t *> (actor_);
    if (!handle || g_test_actor_handles.count (handle) == 0)
        return ZLINK_CONFIG_INVALID_HANDLE;
    zlink_routing_id_t spot_rid;
    if (zlink_get_routing_id (spot_, &spot_rid) != ZLINK_CONFIG_OK)
        return ZLINK_CONFIG_INVALID_ARGUMENT;
    const zlink_request_result_t rc =
      wait_spot_node_actor_leave_spot (handle->node, &handle->ref, &spot_rid, 0);
    return rc == ZLINK_REQUEST_OK ? ZLINK_CONFIG_OK : ZLINK_CONFIG_INVALID_STATE;
}

zlink_recv_result_t test_actor_recv_part (void *subject_,
                                          zlink_actor_recv_info_t *info_out_,
                                          zlink_msg_t *part_out_,
                                          zlink_part_flag_t *has_more_out_,
                                          zlink_recv_flags_t flags_)
{
    test_actor_handle_t *handle = static_cast<test_actor_handle_t *> (subject_);
    if (g_test_actor_handles.count (handle) != 0) {
        return zlink_spot_node_actor_recv_part (handle->node, &handle->ref, info_out_, part_out_,
                                                has_more_out_, flags_);
    }
    const zlink_actor_ref_t *ref = static_cast<const zlink_actor_ref_t *> (subject_);
    std::map<std::string, void *>::const_iterator it =
      g_test_actor_nodes_by_ref.find (test_actor_ref_key (*ref));
    if (it == g_test_actor_nodes_by_ref.end ()) {
        errno = EFAULT;
        return ZLINK_RECV_INVALID_HANDLE;
    }
    return zlink_spot_node_actor_recv_part (it->second, ref, info_out_, part_out_, has_more_out_,
                                            flags_);
}

zlink_submit_result_t
test_actor_send_bound_session_msg (void *actor_, zlink_msg_t *message_, zlink_send_flags_t flags_)
{
    test_actor_handle_t *handle = static_cast<test_actor_handle_t *> (actor_);
    if (!handle || g_test_actor_handles.count (handle) == 0)
        return ZLINK_SUBMIT_INVALID_ARGUMENT;
    return zlink_spot_node_actor_send_bound_session_msg (handle->node, &handle->ref, message_,
                                                         flags_);
}

zlink_submit_result_t test_actor_send_bound_session_packet (void *actor_,
                                                            zlink_msg_t *header_,
                                                            zlink_msg_t *body_,
                                                            zlink_send_flags_t flags_)
{
    if (!header_ || !body_)
        return ZLINK_SUBMIT_INVALID_ARGUMENT;
    const size_t header_size = zlink_msg_size (header_);
    const size_t body_size = zlink_msg_size (body_);
    if (header_size > UINT16_MAX || body_size > UINT32_MAX
        || header_size > SIZE_MAX - body_size - 6u)
        return ZLINK_SUBMIT_INVALID_ARGUMENT;

    zlink_msg_t frame;
    if (zlink_msg_init_size (&frame, 6u + header_size + body_size) != ZLINK_CONFIG_OK)
        return ZLINK_SUBMIT_INTERNAL_ERROR;
    unsigned char *data = static_cast<unsigned char *> (zlink_msg_data (&frame));
    data[0] = static_cast<unsigned char> ((header_size >> 8) & 0xffu);
    data[1] = static_cast<unsigned char> (header_size & 0xffu);
    data[2] = static_cast<unsigned char> ((body_size >> 24) & 0xffu);
    data[3] = static_cast<unsigned char> ((body_size >> 16) & 0xffu);
    data[4] = static_cast<unsigned char> ((body_size >> 8) & 0xffu);
    data[5] = static_cast<unsigned char> (body_size & 0xffu);
    memcpy (data + 6, zlink_msg_data (header_), header_size);
    memcpy (data + 6 + header_size, zlink_msg_data (body_), body_size);

    const zlink_submit_result_t rc = test_actor_send_bound_session_msg (actor_, &frame, flags_);
    if (rc != ZLINK_SUBMIT_OK) {
        zlink_msg_close (&frame);
        return rc;
    }
    zlink_msg_close (header_);
    zlink_msg_init (header_);
    zlink_msg_close (body_);
    zlink_msg_init (body_);
    return ZLINK_SUBMIT_OK;
}

#define zlink_spot_node_actor_new(node_, actor_id_) test_actor_new (node_, actor_id_)
#define zlink_actor_get_ref(actor_, out_) test_actor_get_ref (actor_, out_)
#define zlink_actor_destroy(actor_p_, timeout_ms_) test_actor_destroy (actor_p_, timeout_ms_)
#define zlink_actor_join_spot(actor_, spot_, message_, handler_, userdata_, flags_, timeout_ms_)   \
    test_actor_join_spot (actor_, spot_, message_, handler_, userdata_, flags_, timeout_ms_)
#define zlink_actor_leave_spot(actor_, spot_) test_actor_leave_spot (actor_, spot_)
#define zlink_actor_recv_part(subject_, info_out_, part_out_, has_more_out_, flags_)               \
    test_actor_recv_part (subject_, info_out_, part_out_, has_more_out_, flags_)
#define zlink_actor_send_bound_session_msg(actor_, message_, flags_)                               \
    test_actor_send_bound_session_msg (actor_, message_, flags_)
#define zlink_actor_send_bound_session_packet(actor_, header_, body_, flags_)                      \
    test_actor_send_bound_session_packet (actor_, header_, body_, flags_)
#define zlink_spot_node_destroy_remote_actor(node_, actor_, timeout_ms_)                           \
    wait_spot_node_actor_destroy (node_, actor_, timeout_ms_)
void set_rid (zlink_routing_id_t *rid_, const char *text_)
{
    memset (rid_, 0, sizeof (*rid_));
    rid_->size = static_cast<uint8_t> (strlen (text_));
    memcpy (rid_->data, text_, rid_->size);
}

void init_text_msg (zlink_msg_t *msg_, const char *text_)
{
    TEST_ASSERT_SUCCESS_ERRNO (zlink_msg_init_size (msg_, strlen (text_)));
    memcpy (zlink_msg_data (msg_), text_, strlen (text_));
}

std::string msg_text (zlink_msg_t *msg_)
{
    return std::string (static_cast<const char *> (zlink_msg_data (msg_)), zlink_msg_size (msg_));
}

zlink_recv_result_t test_spot_actor_join_recv_single (void *spot_,
                                                      zlink_actor_join_info_t *info_out_,
                                                      zlink_msg_t *message_out_,
                                                      zlink_recv_flags_t flags_)
{
    zlink_msg_t *parts = NULL;
    size_t part_count = 0;
    const zlink_recv_result_t rc =
      (zlink_spot_actor_join_recv) (spot_, info_out_, &parts, &part_count, flags_);
    if (rc != ZLINK_RECV_OK)
        return rc;
    if (part_count == 0) {
        zlink_msg_init (message_out_);
        return ZLINK_RECV_OK;
    }
    if (zlink_msg_adopt (message_out_, &parts[0]) != ZLINK_CONFIG_OK) {
        zlink_multipart_close (parts, part_count);
        return ZLINK_RECV_INTERNAL_ERROR;
    }
    if (part_count > 1)
        zlink_multipart_close (parts + 1, part_count - 1);
    zlink_multipart_close (parts, 1);
    return ZLINK_RECV_OK;
}

zlink_recv_result_t test_spot_actor_join_recv_parts (void *spot_,
                                                     zlink_actor_join_info_t *info_out_,
                                                     zlink_msg_t **parts_out_,
                                                     size_t *part_count_out_,
                                                     zlink_recv_flags_t flags_)
{
    return (zlink_spot_actor_join_recv) (spot_, info_out_, parts_out_, part_count_out_, flags_);
}

zlink_submit_result_t test_spot_actor_join_reply_single (void *spot_,
                                                         const zlink_actor_join_info_t *info_,
                                                         int32_t join_result_code_,
                                                         zlink_msg_t *message_)
{
    return (zlink_spot_actor_join_reply) (spot_, info_, join_result_code_, message_,
                                          message_ ? 1 : 0);
}

zlink_submit_result_t test_spot_actor_join_reply_parts (void *spot_,
                                                        const zlink_actor_join_info_t *info_,
                                                        int32_t join_result_code_,
                                                        zlink_msg_t *parts_,
                                                        size_t part_count_)
{
    return (zlink_spot_actor_join_reply) (spot_, info_, join_result_code_, parts_, part_count_);
}

#define TEST_SELECT_JOIN_RECV(_1, _2, _3, _4, _5, NAME, ...) NAME
#define zlink_spot_actor_join_recv(...)                                                            \
    TEST_SELECT_JOIN_RECV (__VA_ARGS__, test_spot_actor_join_recv_parts,                           \
                           test_spot_actor_join_recv_single)                                       \
    (__VA_ARGS__)
#define TEST_SELECT_JOIN_REPLY(_1, _2, _3, _4, _5, NAME, ...) NAME
#define zlink_spot_actor_join_reply(...)                                                           \
    TEST_SELECT_JOIN_REPLY (__VA_ARGS__, test_spot_actor_join_reply_parts,                         \
                            test_spot_actor_join_reply_single)                                     \
    (__VA_ARGS__)

void assert_rid_text (const zlink_routing_id_t &rid_, const char *text_)
{
    TEST_ASSERT_EQUAL_UINT8 (strlen (text_), rid_.size);
    TEST_ASSERT_EQUAL_MEMORY (text_, rid_.data, rid_.size);
}

void assert_same_rid (const zlink_routing_id_t &lhs_, const zlink_routing_id_t &rhs_)
{
    TEST_ASSERT_EQUAL_UINT8 (lhs_.size, rhs_.size);
    TEST_ASSERT_EQUAL_MEMORY (lhs_.data, rhs_.data, lhs_.size);
}

void test_entry_spot_facade_lookup_and_rid ()
{
    void *ctx = zlink_ctx_new ();
    TEST_ASSERT_NOT_NULL (ctx);
    void *node = zlink_spot_node_new (ctx, NULL);
    TEST_ASSERT_NOT_NULL (node);

    void *entry_a = NULL;
    void *entry_b = NULL;
    TEST_ASSERT_EQUAL (ZLINK_CONFIG_OK, zlink_spot_node_entry_spot (node, &entry_a));
    TEST_ASSERT_EQUAL (ZLINK_CONFIG_OK, zlink_spot_node_entry_spot (node, &entry_b));
    TEST_ASSERT_NOT_NULL (entry_a);
    TEST_ASSERT_NOT_NULL (entry_b);
    TEST_ASSERT_NOT_EQUAL (entry_a, entry_b);

    zlink_routing_id_t entry_a_rid;
    zlink_routing_id_t entry_b_rid;
    TEST_ASSERT_EQUAL (ZLINK_CONFIG_OK, zlink_get_routing_id (entry_a, &entry_a_rid));
    TEST_ASSERT_EQUAL (ZLINK_CONFIG_OK, zlink_get_routing_id (entry_b, &entry_b_rid));
    assert_same_rid (entry_a_rid, entry_b_rid);

    TEST_ASSERT_EQUAL (ZLINK_CONFIG_OK, zlink_set_routing_id (entry_a, "entry-main", 10));
    TEST_ASSERT_EQUAL (ZLINK_CONFIG_OK, zlink_get_routing_id (entry_b, &entry_b_rid));
    assert_rid_text (entry_b_rid, "entry-main");

    void *entry_lookup = NULL;
    TEST_ASSERT_EQUAL (ZLINK_CONFIG_OK,
                       zlink_spot_node_spot_lookup (node, &entry_b_rid, &entry_lookup));
    TEST_ASSERT_NOT_NULL (entry_lookup);
    zlink_routing_id_t lookup_rid;
    TEST_ASSERT_EQUAL (ZLINK_CONFIG_OK, zlink_get_routing_id (entry_lookup, &lookup_rid));
    assert_rid_text (lookup_rid, "entry-main");

    size_t count = 0;
    TEST_ASSERT_EQUAL (ZLINK_CONFIG_OK, zlink_spot_node_spots (node, NULL, &count));
    TEST_ASSERT_TRUE (count >= 1);
    std::vector<zlink_spot_node_spot_entry_t> spot_rows (count);
    TEST_ASSERT_EQUAL (ZLINK_CONFIG_OK, zlink_spot_node_spots (node, spot_rows.data (), &count));
    bool found_entry_row = false;
    for (size_t i = 0; i < count; ++i) {
        if (spot_rows[i].spot_rid.size == entry_b_rid.size
            && memcmp (spot_rows[i].spot_rid.data, entry_b_rid.data, entry_b_rid.size) == 0) {
            TEST_ASSERT_EQUAL (ZLINK_SPOT_KIND_ENTRY, spot_rows[i].spot_kind);
            found_entry_row = true;
        }
    }
    TEST_ASSERT_TRUE (found_entry_row);

    TEST_ASSERT_EQUAL (ZLINK_CLOSE_OK, zlink_spot_destroy (&entry_a));
    TEST_ASSERT_NULL (entry_a);
    TEST_ASSERT_EQUAL (ZLINK_CLOSE_OK, zlink_spot_destroy (&entry_lookup));
    TEST_ASSERT_EQUAL (ZLINK_CLOSE_OK, zlink_spot_destroy (&entry_b));
    TEST_ASSERT_EQUAL (ZLINK_CLOSE_OK, zlink_spot_node_destroy (&node));
    TEST_ASSERT_EQUAL (ZLINK_CLOSE_OK, zlink_ctx_term (ctx));
}

void test_spot_lookup_refcount_and_rid_index ()
{
    void *ctx = zlink_ctx_new ();
    TEST_ASSERT_NOT_NULL (ctx);
    void *node = zlink_spot_node_new (ctx, NULL);
    TEST_ASSERT_NOT_NULL (node);

    void *spot = zlink_spot_new (node);
    TEST_ASSERT_NOT_NULL (spot);
    zlink_routing_id_t old_rid;
    TEST_ASSERT_EQUAL (ZLINK_CONFIG_OK, zlink_get_routing_id (spot, &old_rid));

    void *lookup = NULL;
    TEST_ASSERT_EQUAL (ZLINK_CONFIG_OK, zlink_spot_node_spot_lookup (node, &old_rid, &lookup));
    TEST_ASSERT_NOT_NULL (lookup);
    TEST_ASSERT_EQUAL (ZLINK_CLOSE_OK, zlink_spot_destroy (&spot));
    TEST_ASSERT_NULL (spot);

    zlink_routing_id_t lookup_rid;
    TEST_ASSERT_EQUAL (ZLINK_CONFIG_OK, zlink_get_routing_id (lookup, &lookup_rid));
    assert_same_rid (old_rid, lookup_rid);

    TEST_ASSERT_EQUAL (ZLINK_CONFIG_OK, zlink_set_routing_id (lookup, "room-a", 6));
    void *unchanged = reinterpret_cast<void *> (0x1);
    TEST_ASSERT_EQUAL (ZLINK_CONFIG_NOT_FOUND,
                       zlink_spot_node_spot_lookup (node, &old_rid, &unchanged));
    TEST_ASSERT_EQUAL (reinterpret_cast<void *> (0x1), unchanged);

    TEST_ASSERT_EQUAL (ZLINK_CONFIG_OK, zlink_get_routing_id (lookup, &lookup_rid));
    assert_rid_text (lookup_rid, "room-a");
    void *new_lookup = NULL;
    TEST_ASSERT_EQUAL (ZLINK_CONFIG_OK,
                       zlink_spot_node_spot_lookup (node, &lookup_rid, &new_lookup));
    TEST_ASSERT_NOT_NULL (new_lookup);

    void *other = zlink_spot_new (node);
    TEST_ASSERT_NOT_NULL (other);
    TEST_ASSERT_NOT_EQUAL (ZLINK_CONFIG_OK, zlink_set_routing_id (other, "room-a", 6));

    TEST_ASSERT_EQUAL (ZLINK_CLOSE_OK, zlink_spot_destroy (&other));
    TEST_ASSERT_EQUAL (ZLINK_CLOSE_OK, zlink_spot_destroy (&new_lookup));
    TEST_ASSERT_EQUAL (ZLINK_CLOSE_OK, zlink_spot_destroy (&lookup));
    TEST_ASSERT_EQUAL (ZLINK_CLOSE_OK, zlink_spot_node_destroy (&node));
    TEST_ASSERT_EQUAL (ZLINK_CLOSE_OK, zlink_ctx_term (ctx));
}

bool reserve_loopback_tcp_endpoint (char *endpoint_, size_t endpoint_size_)
{
    if (!endpoint_ || endpoint_size_ == 0)
        return false;

    fd_t fd = bind_socket_resolve_port ("127.0.0.1", "0", endpoint_);
    if (fd == retired_fd)
        return false;
    close (fd);
    return true;
}

bool bind_registry_endpoints (void *registry_,
                              char *pub_endpoint_,
                              size_t pub_endpoint_size_,
                              char *router_endpoint_,
                              size_t router_endpoint_size_)
{
    LIBZLINK_UNUSED (pub_endpoint_size_);
    LIBZLINK_UNUSED (router_endpoint_size_);
    for (int i = 0; i < 128; ++i) {
        if (!reserve_loopback_tcp_endpoint (pub_endpoint_, pub_endpoint_size_)
            || !reserve_loopback_tcp_endpoint (router_endpoint_, router_endpoint_size_)
            || strcmp (pub_endpoint_, router_endpoint_) == 0)
            continue;
        if (zlink_registry_bind (registry_, pub_endpoint_, router_endpoint_) == ZLINK_BIND_OK)
            return true;
    }
    return false;
}

bool bind_spot_endpoint (void *node_, char *endpoint_, size_t endpoint_size_)
{
    for (int i = 0; i < 128; ++i) {
        if (!reserve_loopback_tcp_endpoint (endpoint_, endpoint_size_))
            continue;
        if (zlink_spot_node_set_pub_bind (node_, endpoint_) == ZLINK_CONFIG_OK)
            return true;
    }
    return false;
}

bool bind_spot_router_endpoint (void *node_, char *endpoint_, size_t endpoint_size_)
{
    for (int i = 0; i < 128; ++i) {
        if (!reserve_loopback_tcp_endpoint (endpoint_, endpoint_size_))
            continue;
        if (zlink_spot_node_set_router_bind (node_, endpoint_) == ZLINK_CONFIG_OK)
            return true;
    }
    return false;
}

bool connect_registry_with_retry (void *discovery_, const char *router_endpoint_, int timeout_ms_)
{
    return zlink_test_wait_until (timeout_ms_, [=] {
        if (zlink_discovery_connect_registry (discovery_, router_endpoint_) == ZLINK_CONNECT_OK)
            return true;
        return false;
    });
}

bool rid_equals (const zlink_routing_id_t &lhs_, const zlink_routing_id_t &rhs_)
{
    return lhs_.size == rhs_.size && memcmp (lhs_.data, rhs_.data, lhs_.size) == 0;
}

zlink_routing_id_t find_spot_rid_not_in (void *node_,
                                         const std::vector<zlink_routing_id_t> &excluded_)
{
    size_t count = 0;
    TEST_ASSERT_EQUAL (ZLINK_CONFIG_OK, zlink_spot_node_spots (node_, NULL, &count));
    std::vector<zlink_spot_node_spot_entry_t> rows (count);
    TEST_ASSERT_EQUAL (ZLINK_CONFIG_OK, zlink_spot_node_spots (node_, rows.data (), &count));
    for (size_t i = 0; i < count; ++i) {
        bool excluded = false;
        for (size_t j = 0; j < excluded_.size (); ++j) {
            if (rid_equals (rows[i].spot_rid, excluded_[j])) {
                excluded = true;
                break;
            }
        }
        if (!excluded)
            return rows[i].spot_rid;
    }
    TEST_FAIL_MESSAGE ("new Spot rid not found in snapshot");
    zlink_routing_id_t empty;
    memset (&empty, 0, sizeof (empty));
    return empty;
}

bool find_spot_snapshot_row (void *node_,
                             const zlink_routing_id_t &spot_rid_,
                             zlink_spot_node_spot_entry_t *row_out_)
{
    size_t count = 0;
    TEST_ASSERT_EQUAL (ZLINK_CONFIG_OK, zlink_spot_node_spots (node_, NULL, &count));
    std::vector<zlink_spot_node_spot_entry_t> rows (count);
    TEST_ASSERT_EQUAL (ZLINK_CONFIG_OK, zlink_spot_node_spots (node_, rows.data (), &count));
    for (size_t i = 0; i < count; ++i) {
        if (rid_equals (rows[i].spot_rid, spot_rid_)) {
            if (row_out_)
                *row_out_ = rows[i];
            return true;
        }
    }
    return false;
}

void test_spot_get_or_new_creates_and_reuses_logical_spot ()
{
    void *ctx = zlink_ctx_new ();
    TEST_ASSERT_NOT_NULL (ctx);
    void *node = zlink_spot_node_new (ctx, NULL);
    TEST_ASSERT_NOT_NULL (node);

    zlink_routing_id_t room_rid;
    set_rid (&room_rid, "room-get-or-new");

    void *first = NULL;
    uint32_t first_created = 0;
    TEST_ASSERT_EQUAL (ZLINK_CONFIG_OK,
                       zlink_spot_node_spot_get_or_new (node, &room_rid, &first, &first_created));
    TEST_ASSERT_NOT_NULL (first);
    TEST_ASSERT_EQUAL_UINT32 (1u, first_created);

    void *second = NULL;
    uint32_t second_created = 1;
    TEST_ASSERT_EQUAL (ZLINK_CONFIG_OK,
                       zlink_spot_node_spot_get_or_new (node, &room_rid, &second, &second_created));
    TEST_ASSERT_NOT_NULL (second);
    TEST_ASSERT_EQUAL_UINT32 (0u, second_created);
    TEST_ASSERT_NOT_EQUAL (first, second);

    zlink_routing_id_t first_rid;
    zlink_routing_id_t second_rid;
    TEST_ASSERT_EQUAL (ZLINK_CONFIG_OK, zlink_get_routing_id (first, &first_rid));
    TEST_ASSERT_EQUAL (ZLINK_CONFIG_OK, zlink_get_routing_id (second, &second_rid));
    assert_same_rid (room_rid, first_rid);
    assert_same_rid (room_rid, second_rid);
    TEST_ASSERT_TRUE (find_spot_snapshot_row (node, room_rid, NULL));

    TEST_ASSERT_EQUAL (ZLINK_CLOSE_OK, zlink_spot_destroy (&first));
    TEST_ASSERT_EQUAL (ZLINK_CLOSE_OK, zlink_spot_destroy (&second));
    TEST_ASSERT_FALSE (find_spot_snapshot_row (node, room_rid, NULL));
    TEST_ASSERT_EQUAL (ZLINK_CLOSE_OK, zlink_spot_node_destroy (&node));
    TEST_ASSERT_EQUAL (ZLINK_CLOSE_OK, zlink_ctx_term (ctx));
}

void test_spot_get_or_new_rejects_invalid_args_and_clears_outputs ()
{
    void *ctx = zlink_ctx_new ();
    TEST_ASSERT_NOT_NULL (ctx);
    void *node = zlink_spot_node_new (ctx, NULL);
    TEST_ASSERT_NOT_NULL (node);

    zlink_routing_id_t room_rid;
    set_rid (&room_rid, "room-invalid");
    void *spot = reinterpret_cast<void *> (0x1);
    uint32_t created = 1;

    TEST_ASSERT_EQUAL (ZLINK_CONFIG_INVALID_HANDLE,
                       zlink_spot_node_spot_get_or_new (NULL, &room_rid, &spot, &created));
    TEST_ASSERT_NULL (spot);
    TEST_ASSERT_EQUAL_UINT32 (0u, created);

    spot = reinterpret_cast<void *> (0x1);
    created = 1;
    TEST_ASSERT_EQUAL (ZLINK_CONFIG_INVALID_ARGUMENT,
                       zlink_spot_node_spot_get_or_new (node, NULL, &spot, &created));
    TEST_ASSERT_NULL (spot);
    TEST_ASSERT_EQUAL_UINT32 (0u, created);

    created = 1;
    TEST_ASSERT_EQUAL (ZLINK_CONFIG_INVALID_ARGUMENT,
                       zlink_spot_node_spot_get_or_new (node, &room_rid, NULL, &created));
    TEST_ASSERT_EQUAL_UINT32 (0u, created);

    TEST_ASSERT_EQUAL (ZLINK_CLOSE_OK, zlink_spot_node_destroy (&node));
    TEST_ASSERT_EQUAL (ZLINK_CLOSE_OK, zlink_ctx_term (ctx));
}

void test_spot_get_or_new_concurrent_callers_create_once ()
{
    void *ctx = zlink_ctx_new ();
    TEST_ASSERT_NOT_NULL (ctx);
    void *node = zlink_spot_node_new (ctx, NULL);
    TEST_ASSERT_NOT_NULL (node);

    zlink_routing_id_t room_rid;
    set_rid (&room_rid, "room-concurrent");
    const size_t worker_count = 16;
    std::vector<void *> spots (worker_count, NULL);
    std::vector<uint32_t> created (worker_count, 0);
    std::vector<zlink_config_result_t> results (worker_count, ZLINK_CONFIG_INTERNAL_ERROR);
    std::mutex start_mutex;
    std::condition_variable start_cv;
    bool start = false;
    std::vector<std::thread> workers;
    workers.reserve (worker_count);

    for (size_t i = 0; i < worker_count; ++i) {
        workers.push_back (std::thread ([&, i] () {
            {
                std::unique_lock<std::mutex> lock (start_mutex);
                start_cv.wait (lock, [&] { return start; });
            }
            results[i] = zlink_spot_node_spot_get_or_new (node, &room_rid, &spots[i], &created[i]);
        }));
    }

    {
        std::lock_guard<std::mutex> lock (start_mutex);
        start = true;
    }
    start_cv.notify_all ();
    for (size_t i = 0; i < workers.size (); ++i)
        workers[i].join ();

    size_t created_count = 0;
    for (size_t i = 0; i < worker_count; ++i) {
        TEST_ASSERT_EQUAL (ZLINK_CONFIG_OK, results[i]);
        TEST_ASSERT_NOT_NULL (spots[i]);
        created_count += created[i] != 0 ? 1u : 0u;
        zlink_routing_id_t got_rid;
        TEST_ASSERT_EQUAL (ZLINK_CONFIG_OK, zlink_get_routing_id (spots[i], &got_rid));
        assert_same_rid (room_rid, got_rid);
    }
    TEST_ASSERT_EQUAL_UINT64 (1u, created_count);
    TEST_ASSERT_TRUE (find_spot_snapshot_row (node, room_rid, NULL));

    for (size_t i = 0; i < worker_count; ++i)
        TEST_ASSERT_EQUAL (ZLINK_CLOSE_OK, zlink_spot_destroy (&spots[i]));
    TEST_ASSERT_FALSE (find_spot_snapshot_row (node, room_rid, NULL));
    TEST_ASSERT_EQUAL (ZLINK_CLOSE_OK, zlink_spot_node_destroy (&node));
    TEST_ASSERT_EQUAL (ZLINK_CLOSE_OK, zlink_ctx_term (ctx));
}

struct actor_probe_t
{
    actor_probe_t () :
        join_result (ZLINK_REQUEST_INTERNAL_ERROR),
        join_done (false),
        actor_event (false),
        actor_subject_ok (false),
        actor_no_data_after_drain (false),
        double_reply_checked (false),
        failed (false),
        last_errno (0),
        actor_event_count (0),
        actor_recv_count (0),
        first_part_flag (ZLINK_PART_FINAL)
    {
        lifecycle = NULL;
        accept_join = true;
        try_destroy_in_actor_callback = false;
        destroy_in_actor_callback_blocked = false;
        memset (&last_join_info, 0, sizeof (last_join_info));
    }

    std::mutex mutex;
    std::condition_variable cv;
    zlink_request_result_t join_result;
    bool join_done;
    bool actor_event;
    bool actor_subject_ok;
    bool actor_no_data_after_drain;
    bool double_reply_checked;
    bool failed;
    int last_errno;
    int actor_event_count;
    int actor_recv_count;
    zlink_part_flag_t first_part_flag;
    zlink_actor_join_info_t last_join_info;
    lifecycle_probe_t *lifecycle;
    std::string payload;
    std::string join_payload;
    bool accept_join;
    bool try_destroy_in_actor_callback;
    bool destroy_in_actor_callback_blocked;
};

void on_join_reply (zlink_request_result_t result_,
                    zlink_msg_t *parts_,
                    size_t part_count_,
                    void *userdata_)
{
    actor_probe_t *probe = static_cast<actor_probe_t *> (userdata_);
    std::lock_guard<std::mutex> lock (probe->mutex);
    probe->join_result = result_;
    probe->join_done = true;
    if (parts_ && part_count_ > 0)
        zlink_multipart_close (parts_, part_count_);
    probe->cv.notify_all ();
}

void on_dispatch (void *spot_, const zlink_spot_dispatch_info_t *info_, void *userdata_)
{
    actor_probe_t *probe = static_cast<actor_probe_t *> (userdata_);
    if (!info_)
        return;

    if (info_->event == ZLINK_SPOT_DISPATCH_EVENT_ACTOR_LIFECYCLE_READABLE) {
        drain_lifecycle_events (spot_, probe->lifecycle);
        return;
    }

    if (info_->event == ZLINK_SPOT_DISPATCH_EVENT_ACTOR_JOIN_READABLE) {
        zlink_actor_join_info_t join_info;
        zlink_msg_t *parts = NULL;
        size_t part_count = 0;
        zlink_recv_result_t recv_rc = zlink_spot_actor_join_recv (
          info_->subject, &join_info, &parts, &part_count, ZLINK_RECV_FLAGS_DONTWAIT);
        if (recv_rc != ZLINK_RECV_OK) {
            std::lock_guard<std::mutex> lock (probe->mutex);
            probe->failed = true;
            probe->last_errno = zlink_errno ();
            probe->cv.notify_all ();
            return;
        }
        {
            std::lock_guard<std::mutex> lock (probe->mutex);
            probe->last_join_info = join_info;
            probe->join_payload = part_count > 0 ? msg_text (&parts[0]) : std::string ();
        }
        zlink_multipart_close (parts, part_count);

        zlink_msg_t reply;
        init_text_msg (&reply, "join-reply");
        zlink_submit_result_t reply_rc = zlink_spot_actor_join_reply (
          info_->subject, &join_info, probe->accept_join ? 0 : 1, &reply, 1);
        if (reply_rc != ZLINK_SUBMIT_OK) {
            zlink_msg_close (&reply);
            std::lock_guard<std::mutex> lock (probe->mutex);
            probe->failed = true;
            probe->last_errno = zlink_errno ();
            probe->cv.notify_all ();
        }
        zlink_msg_t late_reply;
        zlink_msg_init (&late_reply);
        zlink_submit_result_t late_rc =
          zlink_spot_actor_join_reply (info_->subject, &join_info, 0, &late_reply, 1);
        {
            std::lock_guard<std::mutex> lock (probe->mutex);
            probe->double_reply_checked =
              late_rc == ZLINK_SUBMIT_INVALID_STATE || late_rc == ZLINK_SUBMIT_INVALID_ARGUMENT;
            if (!probe->double_reply_checked) {
                probe->failed = true;
                probe->last_errno = zlink_errno ();
            }
            probe->cv.notify_all ();
        }
        if (late_rc != ZLINK_SUBMIT_OK)
            zlink_msg_close (&late_reply);
        return;
    }

    if (info_->event == ZLINK_SPOT_DISPATCH_EVENT_ACTOR_READABLE) {
        bool failed = false;
        bool no_data = false;
        int last_errno = 0;
        int recv_count = 0;
        zlink_part_flag_t first_flag = ZLINK_PART_FINAL;
        std::string payload;
        while (true) {
            zlink_actor_recv_info_t recv_info;
            zlink_msg_t part;
            memset (&part, 0, sizeof (part));
            zlink_part_flag_t more = ZLINK_PART_MORE;
            zlink_recv_result_t recv_rc = zlink_actor_recv_part (info_->subject, &recv_info, &part,
                                                                 &more, ZLINK_RECV_FLAGS_DONTWAIT);
            if (recv_rc == ZLINK_RECV_NO_DATA) {
                no_data = true;
                break;
            }
            if (recv_rc != ZLINK_RECV_OK) {
                failed = true;
                last_errno = zlink_errno ();
                break;
            }
            if (recv_count == 0)
                first_flag = more;
            payload += msg_text (&part);
            zlink_msg_close (&part);
            ++recv_count;
        }
        std::lock_guard<std::mutex> lock (probe->mutex);
        probe->actor_subject_ok = info_->subject_kind == ZLINK_SPOT_DISPATCH_SUBJECT_ACTOR;
        probe->actor_no_data_after_drain = no_data;
        probe->actor_event_count += 1;
        probe->actor_recv_count += recv_count;
        probe->first_part_flag = first_flag;
        if (failed) {
            probe->failed = true;
            probe->last_errno = last_errno;
        } else if (recv_count > 0) {
            probe->actor_event = true;
            probe->payload += payload;
        }
        if (probe->try_destroy_in_actor_callback) {
            void *actor_to_destroy = info_->subject;
            zlink_request_result_t destroy_rc = zlink_actor_destroy (&actor_to_destroy, 0);
            probe->destroy_in_actor_callback_blocked =
              destroy_rc == ZLINK_REQUEST_BUSY && actor_to_destroy == info_->subject;
            if (!probe->destroy_in_actor_callback_blocked) {
                probe->failed = true;
                probe->last_errno = zlink_errno ();
            }
        }
        probe->cv.notify_all ();
    }
}

void on_join_only_dispatch (void *spot_, const zlink_spot_dispatch_info_t *info_, void *userdata_)
{
    actor_probe_t *probe = static_cast<actor_probe_t *> (userdata_);
    if (!info_)
        return;
    if (info_->event == ZLINK_SPOT_DISPATCH_EVENT_ACTOR_LIFECYCLE_READABLE) {
        drain_lifecycle_events (spot_, probe->lifecycle);
        return;
    }
    if (info_->event != ZLINK_SPOT_DISPATCH_EVENT_ACTOR_JOIN_READABLE)
        return;

    zlink_actor_join_info_t join_info;
    zlink_msg_t *parts = NULL;
    size_t part_count = 0;
    zlink_recv_result_t recv_rc = zlink_spot_actor_join_recv (
      info_->subject, &join_info, &parts, &part_count, ZLINK_RECV_FLAGS_DONTWAIT);
    if (recv_rc != ZLINK_RECV_OK) {
        std::lock_guard<std::mutex> lock (probe->mutex);
        probe->failed = true;
        probe->last_errno = zlink_errno ();
        probe->cv.notify_all ();
        return;
    }
    {
        std::lock_guard<std::mutex> lock (probe->mutex);
        probe->last_join_info = join_info;
        probe->join_payload = part_count > 0 ? msg_text (&parts[0]) : std::string ();
    }
    zlink_multipart_close (parts, part_count);

    zlink_msg_t reply;
    init_text_msg (&reply, "join-reply");
    zlink_submit_result_t reply_rc = zlink_spot_actor_join_reply (
      info_->subject, &join_info, probe->accept_join ? 0 : 1, &reply, 1);
    if (reply_rc != ZLINK_SUBMIT_OK) {
        zlink_msg_close (&reply);
        std::lock_guard<std::mutex> lock (probe->mutex);
        probe->failed = true;
        probe->last_errno = zlink_errno ();
        probe->cv.notify_all ();
    }
}

struct admission_probe_t
{
    admission_probe_t () :
        calls (0),
        accept (true),
        try_reentrant_create (false),
        reentrant_create_blocked (false),
        sleep_ms (0),
        entered (false)
    {
    }

    std::mutex mutex;
    std::condition_variable cv;
    int calls;
    bool accept;
    bool try_reentrant_create;
    bool reentrant_create_blocked;
    int sleep_ms;
    bool entered;
};


struct stream_session_probe_t
{
    stream_session_probe_t () : ready (false) { memset (&rid, 0, sizeof (rid)); }

    std::mutex mutex;
    std::condition_variable cv;
    bool ready;
    zlink_routing_id_t rid;
};

void on_stream_session_probe (const zlink_routing_id_t *source_rid_,
                              zlink_msg_t *parts_,
                              size_t part_count_,
                              void *userdata_)
{
    LIBZLINK_UNUSED (parts_);
    LIBZLINK_UNUSED (part_count_);
    stream_session_probe_t *probe = static_cast<stream_session_probe_t *> (userdata_);
    if (!probe || !source_rid_)
        return;
    {
        std::lock_guard<std::mutex> lock (probe->mutex);
        probe->rid = *source_rid_;
        probe->ready = true;
    }
    probe->cv.notify_all ();
}

#if defined(ZLINK_HAVE_WINDOWS)
void test_actor_send_bound_session_raw_and_packet ()
{
    TEST_IGNORE_MESSAGE ("raw tcp helper unavailable on Windows");
}
#else
bool parse_tcp_endpoint (const char *endpoint_, char *host_, int *port_)
{
    char proto[8] = {0};
    return endpoint_ && host_ && port_
           && sscanf (endpoint_, "%7[^:]://%63[^:]:%d", proto, host_, port_) == 3
           && strcmp (proto, "tcp") == 0;
}

int connect_raw_tcp (const char *endpoint_)
{
    char host[64] = {0};
    int port = 0;
    if (!parse_tcp_endpoint (endpoint_, host, &port)) {
        errno = EINVAL;
        return -1;
    }
    const int fd = socket (AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (fd < 0)
        return -1;
    struct sockaddr_in addr;
    memset (&addr, 0, sizeof (addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons (static_cast<uint16_t> (port));
    if (inet_pton (AF_INET, host, &addr.sin_addr) != 1
        || connect (fd, reinterpret_cast<const struct sockaddr *> (&addr), sizeof (addr)) != 0) {
        const int err = errno;
        close (fd);
        errno = err;
        return -1;
    }
    return fd;
}

int set_raw_timeout (int fd_, int timeout_ms_)
{
    struct timeval tv;
    tv.tv_sec = timeout_ms_ / 1000;
    tv.tv_usec = (timeout_ms_ % 1000) * 1000;
    return setsockopt (fd_, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof (tv)) == 0
               && setsockopt (fd_, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof (tv)) == 0
             ? 0
             : -1;
}

int send_all (int fd_, const void *buf_, size_t size_)
{
    const unsigned char *src = static_cast<const unsigned char *> (buf_);
    size_t off = 0;
    while (off < size_) {
        const ssize_t n = send (fd_, src + off, size_ - off, 0);
        if (n > 0) {
            off += static_cast<size_t> (n);
            continue;
        }
        if (n < 0 && errno == EINTR)
            continue;
        return -1;
    }
    return 0;
}

int recv_exact (int fd_, void *buf_, size_t size_)
{
    unsigned char *dst = static_cast<unsigned char *> (buf_);
    size_t off = 0;
    while (off < size_) {
        const ssize_t n = recv (fd_, dst + off, size_ - off, 0);
        if (n > 0) {
            off += static_cast<size_t> (n);
            continue;
        }
        if (n < 0 && errno == EINTR)
            continue;
        return -1;
    }
    return 0;
}

void close_raw_fd (int fd_)
{
    if (fd_ >= 0)
        close (fd_);
}

void test_actor_send_bound_session_raw_and_packet ()
{
    void *ctx = zlink_ctx_new ();
    TEST_ASSERT_NOT_NULL (ctx);

    zlink_spot_node_options_t options;
    options.mode = ZLINK_SPOT_NODE_MODE_ALL;
    void *node = zlink_spot_node_new (ctx, &options);
    TEST_ASSERT_NOT_NULL (node);
    void *actor = zlink_spot_node_actor_new (node, "actor-send");
    TEST_ASSERT_NOT_NULL (actor);

    zlink_msg_t unbound_header;
    zlink_msg_t unbound_body;
    init_text_msg (&unbound_header, "unbound-h");
    init_text_msg (&unbound_body, "unbound-b");
    TEST_ASSERT_EQUAL (ZLINK_SUBMIT_NOT_FOUND,
                       zlink_actor_send_bound_session_packet (actor, &unbound_header, &unbound_body,
                                                              ZLINK_DONTWAIT));
    TEST_ASSERT_EQUAL_STRING ("unbound-h", msg_text (&unbound_header).c_str ());
    TEST_ASSERT_EQUAL_STRING ("unbound-b", msg_text (&unbound_body).c_str ());
    TEST_ASSERT_EQUAL (ZLINK_CONFIG_OK, zlink_msg_close (&unbound_header));
    TEST_ASSERT_EQUAL (ZLINK_CONFIG_OK, zlink_msg_close (&unbound_body));

    void *stream = zlink_socket (ctx, ZLINK_SOCKET_STREAM);
    TEST_ASSERT_NOT_NULL (stream);
    const int zero = 0;
    TEST_ASSERT_EQUAL (ZLINK_CONFIG_OK,
                       zlink_set_option (stream, ZLINK_OPT_LINGER, &zero, sizeof (zero)));

    char endpoint[MAX_SOCKET_STRING];
    bind_loopback_ipv4 (stream, endpoint, sizeof (endpoint));
    stream_session_probe_t probe;
    TEST_ASSERT_EQUAL (ZLINK_HANDLER_OK,
                       zlink_recv_handler (stream, on_stream_session_probe, &probe));

    const int client_fd = connect_raw_tcp (endpoint);
    TEST_ASSERT_TRUE (client_fd >= 0);
    TEST_ASSERT_EQUAL_INT (0, set_raw_timeout (client_fd, 3000));

    const char hello[] = "hello";
    TEST_ASSERT_EQUAL_INT (0, send_all (client_fd, hello, sizeof (hello) - 1));
    {
        std::unique_lock<std::mutex> lock (probe.mutex);
        TEST_ASSERT_TRUE (
          probe.cv.wait_for (lock, std::chrono::seconds (3), [&] { return probe.ready; }));
    }

    zlink_actor_ref_t ref;
    TEST_ASSERT_EQUAL (ZLINK_CONFIG_OK, zlink_actor_get_ref (actor, &ref));
    TEST_ASSERT_EQUAL (ZLINK_REQUEST_OK,
                       wait_stream_bind_actor (node, stream, &probe.rid, &ref, 1000));

    zlink_msg_t raw_reply;
    init_text_msg (&raw_reply, "actor-raw");
    TEST_ASSERT_EQUAL (ZLINK_SUBMIT_OK,
                       zlink_actor_send_bound_session_msg (actor, &raw_reply, ZLINK_DONTWAIT));
    char raw_buf[16] = {0};
    TEST_ASSERT_EQUAL_INT (0, recv_exact (client_fd, raw_buf, 9));
    TEST_ASSERT_EQUAL_STRING_LEN ("actor-raw", raw_buf, 9);

    zlink_msg_t header;
    zlink_msg_t body;
    init_text_msg (&header, "hdr");
    init_text_msg (&body, "body");
    TEST_ASSERT_EQUAL (ZLINK_SUBMIT_OK, zlink_actor_send_bound_session_packet (
                                          actor, &header, &body, ZLINK_DONTWAIT));
    unsigned char packet_buf[13] = {0};
    TEST_ASSERT_EQUAL_INT (0, recv_exact (client_fd, packet_buf, sizeof (packet_buf)));
    TEST_ASSERT_EQUAL_UINT8 (0, packet_buf[0]);
    TEST_ASSERT_EQUAL_UINT8 (3, packet_buf[1]);
    TEST_ASSERT_EQUAL_UINT8 (0, packet_buf[2]);
    TEST_ASSERT_EQUAL_UINT8 (0, packet_buf[3]);
    TEST_ASSERT_EQUAL_UINT8 (0, packet_buf[4]);
    TEST_ASSERT_EQUAL_UINT8 (4, packet_buf[5]);
    TEST_ASSERT_EQUAL_MEMORY ("hdr", packet_buf + 6, 3);
    TEST_ASSERT_EQUAL_MEMORY ("body", packet_buf + 9, 4);

    TEST_ASSERT_EQUAL (ZLINK_CLOSE_OK, zlink_close (stream));
    zlink_msg_t late_msg;
    init_text_msg (&late_msg, "late");
    TEST_ASSERT_EQUAL (ZLINK_SUBMIT_NOT_FOUND,
                       zlink_actor_send_bound_session_msg (actor, &late_msg, ZLINK_DONTWAIT));
    TEST_ASSERT_EQUAL (ZLINK_CONFIG_OK, zlink_msg_close (&late_msg));

    close_raw_fd (client_fd);
    TEST_ASSERT_EQUAL (ZLINK_REQUEST_OK, zlink_actor_destroy (&actor, 0));
    TEST_ASSERT_EQUAL (ZLINK_CLOSE_OK, zlink_spot_node_destroy (&node));
    TEST_ASSERT_EQUAL (ZLINK_CLOSE_OK, zlink_ctx_term (ctx));
}
#endif
}


void test_entry_spot_dispatch_receives_bound_actor_message ()
{
    void *ctx = zlink_ctx_new ();
    TEST_ASSERT_NOT_NULL (ctx);
    void *node = zlink_spot_node_new (ctx, NULL);
    TEST_ASSERT_NOT_NULL (node);
    void *entry = NULL;
    TEST_ASSERT_EQUAL (ZLINK_CONFIG_OK, zlink_spot_node_entry_spot (node, &entry));
    TEST_ASSERT_NOT_NULL (entry);

    actor_probe_t probe;
    TEST_ASSERT_EQUAL (ZLINK_HANDLER_OK,
                       zlink_spot_dispatch_event_handler (entry, on_dispatch, &probe));
    void *actor = zlink_spot_node_actor_new (node, "entry-dispatch");
    TEST_ASSERT_NOT_NULL (actor);
    zlink_actor_ref_t ref;
    TEST_ASSERT_EQUAL (ZLINK_CONFIG_OK, zlink_actor_get_ref (actor, &ref));

    void *stream = zlink_socket (ctx, ZLINK_SOCKET_STREAM);
    TEST_ASSERT_NOT_NULL (stream);
    zlink_routing_id_t session_rid;
    set_rid (&session_rid, "entry-session");
    TEST_ASSERT_EQUAL (ZLINK_REQUEST_OK,
                       wait_stream_bind_actor (node, stream, &session_rid, &ref, 1000));
    zlink_msg_t part;
    init_text_msg (&part, "entry-payload");
    TEST_ASSERT_EQUAL (ZLINK_SUBMIT_OK, test_stream_send_bound_actor_part (
                                          node, stream, &session_rid, "entry-dispatch", &part,
                                          ZLINK_DONTWAIT, ZLINK_PART_FINAL));

    {
        std::unique_lock<std::mutex> lock (probe.mutex);
        TEST_ASSERT_TRUE (probe.cv.wait_for (lock, std::chrono::seconds (2),
                                             [&] { return probe.actor_event || probe.failed; }));
        TEST_ASSERT_FALSE (probe.failed);
        TEST_ASSERT_EQUAL_STRING ("entry-payload", probe.payload.c_str ());
    }

    TEST_ASSERT_EQUAL (ZLINK_REQUEST_OK, zlink_actor_destroy (&actor, 1000));
    TEST_ASSERT_EQUAL (ZLINK_CLOSE_OK, zlink_close (stream));
    TEST_ASSERT_EQUAL (ZLINK_CLOSE_OK, zlink_spot_destroy (&entry));
    TEST_ASSERT_EQUAL (ZLINK_CLOSE_OK, zlink_spot_node_destroy (&node));
    TEST_ASSERT_EQUAL (ZLINK_CLOSE_OK, zlink_ctx_term (ctx));
}

void test_actor_join_bind_relay_and_dispatch_recv ()
{
    void *ctx = zlink_ctx_new ();
    TEST_ASSERT_NOT_NULL (ctx);

    zlink_spot_node_options_t options;
    options.mode = ZLINK_SPOT_NODE_MODE_ALL;
    void *node = zlink_spot_node_new (ctx, &options);
    TEST_ASSERT_NOT_NULL (node);
    void *spot = zlink_spot_new (node);
    TEST_ASSERT_NOT_NULL (spot);
    void *other_spot = zlink_spot_new (node);
    TEST_ASSERT_NOT_NULL (other_spot);
    void *foreign_node = zlink_spot_node_new (ctx, &options);
    TEST_ASSERT_NOT_NULL (foreign_node);
    void *foreign_spot = zlink_spot_new (foreign_node);
    TEST_ASSERT_NOT_NULL (foreign_spot);
    void *actor = zlink_spot_node_actor_new (node, "actor-b");
    TEST_ASSERT_NOT_NULL (actor);
    zlink_actor_ref_t ref;
    TEST_ASSERT_EQUAL (ZLINK_CONFIG_OK, zlink_actor_get_ref (actor, &ref));
    zlink_routing_id_t session_rid;
    set_rid (&session_rid, "session-1");
    void *stream_marker = zlink_socket (ctx, ZLINK_SOCKET_STREAM);
    TEST_ASSERT_NOT_NULL (stream_marker);
    TEST_ASSERT_EQUAL (ZLINK_REQUEST_OK,
                       wait_stream_bind_actor (node, stream_marker, &session_rid, &ref, 1000));

    actor_probe_t probe;
    probe.try_destroy_in_actor_callback = true;
    TEST_ASSERT_EQUAL (ZLINK_HANDLER_OK,
                       zlink_spot_dispatch_event_handler (spot, on_dispatch, &probe));

    zlink_msg_t join_msg;
    init_text_msg (&join_msg, "join");
    TEST_ASSERT_EQUAL (
      ZLINK_SUBMIT_OK,
      zlink_actor_join_spot (actor, spot, &join_msg, on_join_reply, &probe, ZLINK_DONTWAIT, 1000));

    {
        std::unique_lock<std::mutex> lock (probe.mutex);
        TEST_ASSERT_TRUE (probe.cv.wait_for (lock, std::chrono::seconds (2), [&] {
            return (probe.join_done && probe.double_reply_checked) || probe.failed;
        }));
        TEST_ASSERT_FALSE (probe.failed);
        TEST_ASSERT_EQUAL (ZLINK_REQUEST_OK, probe.join_result);
        TEST_ASSERT_TRUE (probe.double_reply_checked);
    }

    zlink_msg_t duplicate_join;
    actor_probe_t duplicate_probe;
    init_text_msg (&duplicate_join, "duplicate");
    TEST_ASSERT_EQUAL (ZLINK_SUBMIT_OK,
                       zlink_actor_join_spot (actor, spot, &duplicate_join, on_join_reply,
                                              &duplicate_probe, ZLINK_DONTWAIT, 1000));
    {
        std::unique_lock<std::mutex> lock (duplicate_probe.mutex);
        TEST_ASSERT_TRUE (duplicate_probe.cv.wait_for (lock, std::chrono::seconds (2),
                                                       [&] { return duplicate_probe.join_done; }));
        TEST_ASSERT_EQUAL (ZLINK_REQUEST_OK, duplicate_probe.join_result);
    }

    zlink_actor_ref_t joined_rows[1];
    size_t joined_count = 1;
    TEST_ASSERT_EQUAL (ZLINK_CONFIG_OK, zlink_spot_actors (spot, joined_rows, &joined_count));
    TEST_ASSERT_EQUAL_UINT (1, joined_count);
    TEST_ASSERT_EQUAL_UINT64 (ref.generation, joined_rows[0].generation);

    void *joined_actor_handle = actor;
    TEST_ASSERT_EQUAL (ZLINK_REQUEST_INVALID_STATE, zlink_actor_destroy (&joined_actor_handle, 0));
    TEST_ASSERT_EQUAL_PTR (actor, joined_actor_handle);

    zlink_msg_t part;
    init_text_msg (&part, "payload");
    TEST_ASSERT_EQUAL (ZLINK_SUBMIT_OK, test_stream_send_bound_actor_part (
                                          node, stream_marker, &session_rid, "actor-b", &part,
                                          ZLINK_DONTWAIT, ZLINK_PART_FINAL));

    {
        std::unique_lock<std::mutex> lock (probe.mutex);
        TEST_ASSERT_TRUE (probe.cv.wait_for (lock, std::chrono::seconds (2),
                                             [&] { return probe.actor_event || probe.failed; }));
        TEST_ASSERT_FALSE (probe.failed);
        TEST_ASSERT_EQUAL_STRING ("payload", probe.payload.c_str ());
    }

    TEST_ASSERT_EQUAL (ZLINK_CONFIG_OK, zlink_actor_leave_spot (actor, spot));
    void *entry = NULL;
    TEST_ASSERT_EQUAL (ZLINK_CONFIG_OK, zlink_spot_node_entry_spot (node, &entry));
    TEST_ASSERT_EQUAL (ZLINK_CONFIG_OK, zlink_actor_leave_spot (actor, entry));
    TEST_ASSERT_EQUAL (ZLINK_REQUEST_OK, zlink_actor_destroy (&actor, 0));
    TEST_ASSERT_EQUAL (ZLINK_CLOSE_OK, zlink_spot_destroy (&entry));
    TEST_ASSERT_EQUAL (ZLINK_CLOSE_OK, zlink_close (stream_marker));
    TEST_ASSERT_EQUAL (ZLINK_CLOSE_OK, zlink_spot_destroy (&foreign_spot));
    TEST_ASSERT_EQUAL (ZLINK_CLOSE_OK, zlink_spot_node_destroy (&foreign_node));
    TEST_ASSERT_EQUAL (ZLINK_CLOSE_OK, zlink_spot_destroy (&other_spot));
    TEST_ASSERT_EQUAL (ZLINK_CLOSE_OK, zlink_spot_destroy (&spot));
    TEST_ASSERT_EQUAL (ZLINK_CLOSE_OK, zlink_spot_node_destroy (&node));
    TEST_ASSERT_EQUAL (ZLINK_CLOSE_OK, zlink_ctx_term (ctx));
}

void test_actor_join_entry_spot_moves_user_spot_actor_to_entry ()
{
    void *ctx = zlink_ctx_new ();
    TEST_ASSERT_NOT_NULL (ctx);

    zlink_spot_node_options_t options;
    options.mode = ZLINK_SPOT_NODE_MODE_ALL;
    void *node = zlink_spot_node_new (ctx, &options);
    TEST_ASSERT_NOT_NULL (node);
    void *spot = zlink_spot_new (node);
    TEST_ASSERT_NOT_NULL (spot);
    void *entry = NULL;
    TEST_ASSERT_EQUAL (ZLINK_CONFIG_OK, zlink_spot_node_entry_spot (node, &entry));
    TEST_ASSERT_NOT_NULL (entry);

    actor_probe_t join_probe;
    actor_probe_t entry_dispatch_probe;
    lifecycle_probe_t user_lifecycle;
    lifecycle_probe_t entry_lifecycle;
    join_probe.lifecycle = &user_lifecycle;
    entry_dispatch_probe.lifecycle = &entry_lifecycle;
    TEST_ASSERT_EQUAL (ZLINK_HANDLER_OK, zlink_spot_dispatch_event_handler (
                                           spot, on_join_only_dispatch, &join_probe));
    TEST_ASSERT_EQUAL (ZLINK_HANDLER_OK, zlink_spot_dispatch_event_handler (entry, on_dispatch,
                                                                            &entry_dispatch_probe));

    void *actor = zlink_spot_node_actor_new (node, "entry-return");
    TEST_ASSERT_NOT_NULL (actor);
    zlink_actor_ref_t ref;
    TEST_ASSERT_EQUAL (ZLINK_CONFIG_OK, zlink_actor_get_ref (actor, &ref));
    void *stream = zlink_socket (ctx, ZLINK_SOCKET_STREAM);
    TEST_ASSERT_NOT_NULL (stream);
    zlink_routing_id_t session_rid;
    set_rid (&session_rid, "entry-return-session");
    TEST_ASSERT_EQUAL (ZLINK_REQUEST_OK,
                       wait_stream_bind_actor (node, stream, &session_rid, &ref, 1000));

    zlink_msg_t join_msg;
    init_text_msg (&join_msg, "join-user");
    TEST_ASSERT_EQUAL (ZLINK_SUBMIT_OK,
                       zlink_actor_join_spot (actor, spot, &join_msg, on_join_reply, &join_probe,
                                              ZLINK_DONTWAIT, 1000));
    {
        std::unique_lock<std::mutex> lock (join_probe.mutex);
        TEST_ASSERT_TRUE (join_probe.cv.wait_for (lock, std::chrono::seconds (2), [&] {
            return join_probe.join_done || join_probe.failed;
        }));
        TEST_ASSERT_FALSE (join_probe.failed);
        TEST_ASSERT_EQUAL (ZLINK_REQUEST_OK, join_probe.join_result);
    }
    TEST_ASSERT_TRUE (zlink_test_wait_until (2000, [&] {
        std::lock_guard<std::mutex> lock (user_lifecycle.mutex);
        return user_lifecycle.joined == 1;
    }));
    int user_left_before = 0;
    int entry_joined_before = 0;
    {
        std::lock_guard<std::mutex> user_lock (user_lifecycle.mutex);
        std::lock_guard<std::mutex> entry_lock (entry_lifecycle.mutex);
        user_left_before = user_lifecycle.left;
        entry_joined_before = entry_lifecycle.joined;
    }

    zlink_submit_result_t submit = ZLINK_SUBMIT_INTERNAL_ERROR;
    zlink_msg_t entry_join_msg;
    init_text_msg (&entry_join_msg, "entry-return-request");
    std::string entry_join_reply;
    zlink_actor_join_entry_spot_result_t result =
      wait_spot_node_actor_join_entry_spot (node, &ref, &ref.node_rid, &entry_join_msg, 1,
                                            &submit, 1000, &entry_join_reply);
    TEST_ASSERT_EQUAL (ZLINK_SUBMIT_OK, submit);
    TEST_ASSERT_EQUAL (ZLINK_REQUEST_OK, result.result);
    TEST_ASSERT_EQUAL_INT (0, result.join_result_code);
    TEST_ASSERT_EQUAL_STRING ("join-reply", entry_join_reply.c_str ());
    {
        std::lock_guard<std::mutex> lock (entry_dispatch_probe.mutex);
        TEST_ASSERT_EQUAL_STRING ("entry-return-request",
                                  entry_dispatch_probe.join_payload.c_str ());
    }
    TEST_ASSERT_EQUAL_STRING (ref.actor_id, result.actor.actor_id);
    TEST_ASSERT_EQUAL_UINT64 (ref.generation, result.actor.generation);
    assert_same_rid (ref.node_rid, result.actor.node_rid);
    assert_same_rid (ref.node_rid, result.target_node_rid);

    TEST_ASSERT_TRUE (zlink_test_wait_until (2000, [&] {
        std::lock_guard<std::mutex> user_lock (user_lifecycle.mutex);
        std::lock_guard<std::mutex> entry_lock (entry_lifecycle.mutex);
        return user_lifecycle.left == user_left_before + 1
               && entry_lifecycle.joined == entry_joined_before + 1;
    }));

    zlink_actor_ref_t entry_rows[1];
    size_t entry_count = 1;
    TEST_ASSERT_EQUAL (ZLINK_CONFIG_OK, zlink_spot_actors (entry, entry_rows, &entry_count));
    TEST_ASSERT_EQUAL_UINT (1, entry_count);
    TEST_ASSERT_EQUAL_STRING (ref.actor_id, entry_rows[0].actor_id);

    zlink_actor_join_info_t join_info;
    zlink_msg_t *join_recv_parts = NULL;
    size_t join_recv_part_count = 0;
    TEST_ASSERT_EQUAL (ZLINK_RECV_NO_DATA,
                       zlink_spot_actor_join_recv (entry, &join_info, &join_recv_parts,
                                                   &join_recv_part_count,
                                                   ZLINK_RECV_FLAGS_DONTWAIT));

    TEST_ASSERT_EQUAL (ZLINK_REQUEST_OK, zlink_actor_destroy (&actor, 1000));
    TEST_ASSERT_EQUAL (ZLINK_CLOSE_OK, zlink_close (stream));
    TEST_ASSERT_EQUAL (ZLINK_CLOSE_OK, zlink_spot_destroy (&entry));
    TEST_ASSERT_EQUAL (ZLINK_CLOSE_OK, zlink_spot_destroy (&spot));
    TEST_ASSERT_EQUAL (ZLINK_CLOSE_OK, zlink_spot_node_destroy (&node));
    TEST_ASSERT_EQUAL (ZLINK_CLOSE_OK, zlink_ctx_term (ctx));
}

void test_actor_join_entry_spot_reject_keeps_actor_in_user_spot ()
{
    void *ctx = zlink_ctx_new ();
    TEST_ASSERT_NOT_NULL (ctx);

    zlink_spot_node_options_t options;
    options.mode = ZLINK_SPOT_NODE_MODE_ALL;
    void *node = zlink_spot_node_new (ctx, &options);
    TEST_ASSERT_NOT_NULL (node);
    void *spot = zlink_spot_new (node);
    TEST_ASSERT_NOT_NULL (spot);
    void *entry = NULL;
    TEST_ASSERT_EQUAL (ZLINK_CONFIG_OK, zlink_spot_node_entry_spot (node, &entry));
    TEST_ASSERT_NOT_NULL (entry);

    actor_probe_t user_probe;
    actor_probe_t entry_probe;
    entry_probe.accept_join = false;
    TEST_ASSERT_EQUAL (ZLINK_HANDLER_OK,
                       zlink_spot_dispatch_event_handler (spot, on_join_only_dispatch,
                                                          &user_probe));
    TEST_ASSERT_EQUAL (ZLINK_HANDLER_OK,
                       zlink_spot_dispatch_event_handler (entry, on_dispatch, &entry_probe));

    void *actor = zlink_spot_node_actor_new (node, "entry-reject");
    TEST_ASSERT_NOT_NULL (actor);
    zlink_actor_ref_t ref;
    TEST_ASSERT_EQUAL (ZLINK_CONFIG_OK, zlink_actor_get_ref (actor, &ref));

    zlink_msg_t user_join_msg;
    init_text_msg (&user_join_msg, "join-user-before-reject");
    TEST_ASSERT_EQUAL (ZLINK_SUBMIT_OK,
                       zlink_actor_join_spot (actor, spot, &user_join_msg, on_join_reply,
                                              &user_probe, ZLINK_DONTWAIT, 1000));
    {
        std::unique_lock<std::mutex> lock (user_probe.mutex);
        TEST_ASSERT_TRUE (user_probe.cv.wait_for (lock, std::chrono::seconds (2), [&] {
            return user_probe.join_done || user_probe.failed;
        }));
        TEST_ASSERT_FALSE (user_probe.failed);
        TEST_ASSERT_EQUAL (ZLINK_REQUEST_OK, user_probe.join_result);
    }

    zlink_msg_t entry_join_msg;
    init_text_msg (&entry_join_msg, "entry-reject-request");
    std::string entry_join_reply;
    zlink_submit_result_t submit = ZLINK_SUBMIT_INTERNAL_ERROR;
    zlink_actor_join_entry_spot_result_t result =
      wait_spot_node_actor_join_entry_spot (node, &ref, &ref.node_rid, &entry_join_msg, 1,
                                            &submit, 1000, &entry_join_reply);
    TEST_ASSERT_EQUAL (ZLINK_SUBMIT_OK, submit);
    TEST_ASSERT_EQUAL (ZLINK_REQUEST_OK, result.result);
    TEST_ASSERT_EQUAL_INT (1, result.join_result_code);
    TEST_ASSERT_EQUAL_STRING ("join-reply", entry_join_reply.c_str ());
    {
        std::lock_guard<std::mutex> lock (entry_probe.mutex);
        TEST_ASSERT_EQUAL_STRING ("entry-reject-request", entry_probe.join_payload.c_str ());
    }

    zlink_actor_ref_t user_rows[1];
    size_t user_count = 1;
    TEST_ASSERT_EQUAL (ZLINK_CONFIG_OK, zlink_spot_actors (spot, user_rows, &user_count));
    TEST_ASSERT_EQUAL_UINT (1, user_count);
    TEST_ASSERT_EQUAL_STRING (ref.actor_id, user_rows[0].actor_id);

    zlink_actor_ref_t entry_rows[1];
    size_t entry_count = 1;
    TEST_ASSERT_EQUAL (ZLINK_CONFIG_OK, zlink_spot_actors (entry, entry_rows, &entry_count));
    TEST_ASSERT_EQUAL_UINT (0, entry_count);

    zlink_routing_id_t spot_rid;
    TEST_ASSERT_EQUAL (ZLINK_CONFIG_OK, zlink_get_routing_id (spot, &spot_rid));
    TEST_ASSERT_EQUAL (ZLINK_REQUEST_OK,
                       wait_spot_node_actor_leave_spot (node, &ref, &spot_rid, 1000));
    TEST_ASSERT_EQUAL (ZLINK_REQUEST_OK, zlink_actor_destroy (&actor, 1000));
    TEST_ASSERT_EQUAL (ZLINK_CLOSE_OK, zlink_spot_destroy (&entry));
    TEST_ASSERT_EQUAL (ZLINK_CLOSE_OK, zlink_spot_destroy (&spot));
    TEST_ASSERT_EQUAL (ZLINK_CLOSE_OK, zlink_spot_node_destroy (&node));
    TEST_ASSERT_EQUAL (ZLINK_CLOSE_OK, zlink_ctx_term (ctx));
}

void test_actor_join_entry_spot_is_idempotent_without_lifecycle ()
{
    void *ctx = zlink_ctx_new ();
    TEST_ASSERT_NOT_NULL (ctx);

    zlink_spot_node_options_t options;
    options.mode = ZLINK_SPOT_NODE_MODE_ALL;
    void *node = zlink_spot_node_new (ctx, &options);
    TEST_ASSERT_NOT_NULL (node);
    void *entry = NULL;
    TEST_ASSERT_EQUAL (ZLINK_CONFIG_OK, zlink_spot_node_entry_spot (node, &entry));
    TEST_ASSERT_NOT_NULL (entry);
    lifecycle_probe_t entry_lifecycle;
    actor_probe_t entry_dispatch_probe;
    entry_dispatch_probe.lifecycle = &entry_lifecycle;
    TEST_ASSERT_EQUAL (ZLINK_HANDLER_OK, zlink_spot_dispatch_event_handler (entry, on_dispatch,
                                                                            &entry_dispatch_probe));

    void *actor = zlink_spot_node_actor_new (node, "entry-idempotent");
    TEST_ASSERT_NOT_NULL (actor);
    zlink_actor_ref_t ref;
    TEST_ASSERT_EQUAL (ZLINK_CONFIG_OK, zlink_actor_get_ref (actor, &ref));
    TEST_ASSERT_TRUE (zlink_test_wait_until (2000, [&] {
        std::lock_guard<std::mutex> lock (entry_lifecycle.mutex);
        return entry_lifecycle.joined >= 1;
    }));
    int joined_before = 0;
    int left_before = 0;
    {
        std::lock_guard<std::mutex> lock (entry_lifecycle.mutex);
        joined_before = entry_lifecycle.joined;
        left_before = entry_lifecycle.left;
    }

    zlink_submit_result_t submit = ZLINK_SUBMIT_INTERNAL_ERROR;
    zlink_msg_t entry_join_msg;
    init_text_msg (&entry_join_msg, "same-entry-request");
    std::string entry_join_reply;
    zlink_actor_join_entry_spot_result_t result =
      wait_spot_node_actor_join_entry_spot (node, &ref, &ref.node_rid, &entry_join_msg, 1,
                                            &submit, 1000, &entry_join_reply);
    TEST_ASSERT_EQUAL (ZLINK_SUBMIT_OK, submit);
    TEST_ASSERT_EQUAL (ZLINK_REQUEST_OK, result.result);
    TEST_ASSERT_EQUAL_STRING (ref.actor_id, result.actor.actor_id);
    TEST_ASSERT_EQUAL_UINT64 (ref.generation, result.actor.generation);
    assert_same_rid (ref.node_rid, result.actor.node_rid);
    TEST_ASSERT_TRUE (result.join_epoch > 0);
    TEST_ASSERT_EQUAL_STRING ("join-reply", entry_join_reply.c_str ());
    {
        std::lock_guard<std::mutex> lock (entry_dispatch_probe.mutex);
        TEST_ASSERT_EQUAL_STRING ("same-entry-request", entry_dispatch_probe.join_payload.c_str ());
    }

    std::this_thread::sleep_for (std::chrono::milliseconds (50));
    {
        std::lock_guard<std::mutex> lock (entry_lifecycle.mutex);
        TEST_ASSERT_EQUAL_INT (joined_before, entry_lifecycle.joined);
        TEST_ASSERT_EQUAL_INT (left_before, entry_lifecycle.left);
    }

    TEST_ASSERT_EQUAL (ZLINK_REQUEST_OK, zlink_actor_destroy (&actor, 1000));
    TEST_ASSERT_EQUAL (ZLINK_CLOSE_OK, zlink_spot_destroy (&entry));
    TEST_ASSERT_EQUAL (ZLINK_CLOSE_OK, zlink_spot_node_destroy (&node));
    TEST_ASSERT_EQUAL (ZLINK_CLOSE_OK, zlink_ctx_term (ctx));
}

void test_actor_join_entry_spot_moves_actor_to_remote_node_entry ()
{
    void *ctx = zlink_ctx_new ();
    TEST_ASSERT_NOT_NULL (ctx);

    zlink_spot_node_options_t options;
    options.mode = ZLINK_SPOT_NODE_MODE_ALL;
    void *source_node = zlink_spot_node_new (ctx, &options);
    TEST_ASSERT_NOT_NULL (source_node);
    void *target_node = zlink_spot_node_new (ctx, &options);
    TEST_ASSERT_NOT_NULL (target_node);

    void *source_entry = NULL;
    void *target_entry = NULL;
    TEST_ASSERT_EQUAL (ZLINK_CONFIG_OK, zlink_spot_node_entry_spot (source_node, &source_entry));
    TEST_ASSERT_EQUAL (ZLINK_CONFIG_OK, zlink_spot_node_entry_spot (target_node, &target_entry));
    TEST_ASSERT_NOT_NULL (source_entry);
    TEST_ASSERT_NOT_NULL (target_entry);

    actor_probe_t source_dispatch_probe;
    actor_probe_t target_dispatch_probe;
    lifecycle_probe_t source_lifecycle;
    lifecycle_probe_t target_lifecycle;
    source_dispatch_probe.lifecycle = &source_lifecycle;
    target_dispatch_probe.lifecycle = &target_lifecycle;
    TEST_ASSERT_EQUAL (ZLINK_HANDLER_OK, zlink_spot_dispatch_event_handler (
                                           source_entry, on_dispatch, &source_dispatch_probe));
    TEST_ASSERT_EQUAL (ZLINK_HANDLER_OK, zlink_spot_dispatch_event_handler (
                                           target_entry, on_dispatch, &target_dispatch_probe));

    void *actor = zlink_spot_node_actor_new (source_node, "remote-entry");
    TEST_ASSERT_NOT_NULL (actor);
    zlink_actor_ref_t source_ref;
    TEST_ASSERT_EQUAL (ZLINK_CONFIG_OK, zlink_actor_get_ref (actor, &source_ref));
    TEST_ASSERT_TRUE (zlink_test_wait_until (2000, [&] {
        std::lock_guard<std::mutex> lock (source_lifecycle.mutex);
        return source_lifecycle.joined >= 1;
    }));

    void *stream = zlink_socket (ctx, ZLINK_SOCKET_STREAM);
    TEST_ASSERT_NOT_NULL (stream);
    zlink_routing_id_t session_rid;
    set_rid (&session_rid, "remote-entry-session");
    TEST_ASSERT_EQUAL (ZLINK_REQUEST_OK, wait_stream_bind_actor (source_node, stream, &session_rid,
                                                                 &source_ref, 1000));

    zlink_routing_id_t target_node_rid;
    TEST_ASSERT_EQUAL (ZLINK_CONFIG_OK, zlink_get_routing_id (target_node, &target_node_rid));
    int source_left_before = 0;
    int target_joined_before = 0;
    {
        std::lock_guard<std::mutex> source_lock (source_lifecycle.mutex);
        std::lock_guard<std::mutex> target_lock (target_lifecycle.mutex);
        source_left_before = source_lifecycle.left;
        target_joined_before = target_lifecycle.joined;
    }

    zlink_submit_result_t submit = ZLINK_SUBMIT_INTERNAL_ERROR;
    zlink_msg_t entry_join_msg;
    init_text_msg (&entry_join_msg, "remote-entry-request");
    std::string entry_join_reply;
    zlink_actor_join_entry_spot_result_t result = wait_spot_node_actor_join_entry_spot (
      source_node, &source_ref, &target_node_rid, &entry_join_msg, 1, &submit, 1000,
      &entry_join_reply);
    TEST_ASSERT_EQUAL (ZLINK_SUBMIT_OK, submit);
    TEST_ASSERT_EQUAL (ZLINK_REQUEST_OK, result.result);
    TEST_ASSERT_EQUAL_INT (0, result.join_result_code);
    TEST_ASSERT_EQUAL_STRING ("join-reply", entry_join_reply.c_str ());
    {
        std::lock_guard<std::mutex> lock (target_dispatch_probe.mutex);
        TEST_ASSERT_EQUAL_STRING ("remote-entry-request",
                                  target_dispatch_probe.join_payload.c_str ());
    }
    TEST_ASSERT_EQUAL_STRING (source_ref.actor_id, result.actor.actor_id);
    assert_same_rid (target_node_rid, result.actor.node_rid);
    assert_same_rid (target_node_rid, result.target_node_rid);

    g_test_actor_nodes_by_ref.erase (test_actor_ref_key (source_ref));
    test_actor_handle_t *actor_handle = static_cast<test_actor_handle_t *> (actor);
    actor_handle->node = target_node;
    actor_handle->ref = result.actor;
    g_test_actor_nodes_by_ref[test_actor_ref_key (result.actor)] = target_node;

    TEST_ASSERT_TRUE (zlink_test_wait_until (2000, [&] {
        std::lock_guard<std::mutex> source_lock (source_lifecycle.mutex);
        std::lock_guard<std::mutex> target_lock (target_lifecycle.mutex);
        return source_lifecycle.left == source_left_before + 1
               && target_lifecycle.joined == target_joined_before + 1;
    }));

    zlink_msg_t part;
    init_text_msg (&part, "remote-entry-payload");
    TEST_ASSERT_EQUAL (ZLINK_SUBMIT_OK, test_stream_send_bound_actor_part (
                                          source_node, stream, &session_rid, "remote-entry", &part,
                                          ZLINK_DONTWAIT, ZLINK_PART_FINAL));
    {
        std::unique_lock<std::mutex> lock (target_dispatch_probe.mutex);
        TEST_ASSERT_TRUE (target_dispatch_probe.cv.wait_for (lock, std::chrono::seconds (2), [&] {
            return target_dispatch_probe.actor_event || target_dispatch_probe.failed;
        }));
        TEST_ASSERT_FALSE (target_dispatch_probe.failed);
        TEST_ASSERT_EQUAL_STRING ("remote-entry-payload", target_dispatch_probe.payload.c_str ());
    }

    TEST_ASSERT_EQUAL (ZLINK_REQUEST_OK, zlink_actor_destroy (&actor, 1000));
    TEST_ASSERT_EQUAL (ZLINK_CLOSE_OK, zlink_close (stream));
    TEST_ASSERT_EQUAL (ZLINK_CLOSE_OK, zlink_spot_destroy (&target_entry));
    TEST_ASSERT_EQUAL (ZLINK_CLOSE_OK, zlink_spot_destroy (&source_entry));
    TEST_ASSERT_EQUAL (ZLINK_CLOSE_OK, zlink_spot_node_destroy (&target_node));
    TEST_ASSERT_EQUAL (ZLINK_CLOSE_OK, zlink_spot_node_destroy (&source_node));
    TEST_ASSERT_EQUAL (ZLINK_CLOSE_OK, zlink_ctx_term (ctx));
}

void test_actor_join_entry_spot_error_cases ()
{
    void *ctx = zlink_ctx_new ();
    TEST_ASSERT_NOT_NULL (ctx);

    zlink_spot_node_options_t options;
    options.mode = ZLINK_SPOT_NODE_MODE_ALL;
    void *node = zlink_spot_node_new (ctx, &options);
    TEST_ASSERT_NOT_NULL (node);
    void *spot = zlink_spot_new (node);
    TEST_ASSERT_NOT_NULL (spot);
    void *actor = zlink_spot_node_actor_new (node, "entry-errors");
    TEST_ASSERT_NOT_NULL (actor);
    zlink_actor_ref_t ref;
    TEST_ASSERT_EQUAL (ZLINK_CONFIG_OK, zlink_actor_get_ref (actor, &ref));
    void *stream = zlink_socket (ctx, ZLINK_SOCKET_STREAM);
    TEST_ASSERT_NOT_NULL (stream);
    zlink_routing_id_t session_rid;
    set_rid (&session_rid, "entry-error-session");
    TEST_ASSERT_EQUAL (ZLINK_REQUEST_OK,
                       wait_stream_bind_actor (node, stream, &session_rid, &ref, 1000));

    zlink_submit_result_t submit = ZLINK_SUBMIT_INTERNAL_ERROR;
    zlink_actor_ref_t stale_ref = ref;
    stale_ref.generation += 100;
    zlink_msg_t stale_request;
    init_text_msg (&stale_request, "stale-entry-request");
    zlink_actor_join_entry_spot_result_t result =
      wait_spot_node_actor_join_entry_spot (node, &stale_ref, &ref.node_rid, &stale_request, 1,
                                            &submit, 1000);
    TEST_ASSERT_EQUAL (ZLINK_SUBMIT_OK, submit);
    TEST_ASSERT_EQUAL (ZLINK_REQUEST_CONFLICT, result.result);

    zlink_actor_ref_t missing_ref = ref;
    strncpy (missing_ref.actor_id, "missing-entry-actor", ZLINK_ACTOR_ID_MAX - 1);
    zlink_msg_t missing_request;
    init_text_msg (&missing_request, "missing-entry-request");
    result = wait_spot_node_actor_join_entry_spot (node, &missing_ref, &ref.node_rid,
                                                   &missing_request, 1, &submit, 1000);
    TEST_ASSERT_EQUAL (ZLINK_SUBMIT_OK, submit);
    TEST_ASSERT_EQUAL (ZLINK_REQUEST_NOT_FOUND, result.result);

    zlink_routing_id_t missing_node_rid;
    set_rid (&missing_node_rid, "missing-entry-node");
    zlink_msg_t missing_node_request;
    init_text_msg (&missing_node_request, "missing-node-entry-request");
    result = wait_spot_node_actor_join_entry_spot (node, &ref, &missing_node_rid,
                                                   &missing_node_request, 1, &submit, 1000);
    TEST_ASSERT_EQUAL (ZLINK_SUBMIT_OK, submit);
    TEST_ASSERT_EQUAL (ZLINK_REQUEST_NOT_CONNECTED, result.result);

    entry_join_wait_t malformed_wait;
    TEST_ASSERT_EQUAL (ZLINK_SUBMIT_INVALID_ARGUMENT,
                       zlink_spot_node_actor_join_entry_spot (
                         node, &ref, &ref.node_rid, NULL, 1, entry_join_wait_handler,
                         &malformed_wait, ZLINK_DONTWAIT, 1000));

    actor_probe_t pending_probe;
    zlink_msg_t join_msg;
    init_text_msg (&join_msg, "pending");
    TEST_ASSERT_EQUAL (ZLINK_SUBMIT_OK,
                       zlink_actor_join_spot (actor, spot, &join_msg, on_join_reply, &pending_probe,
                                              ZLINK_DONTWAIT, 100));
    result = wait_spot_node_actor_join_entry_spot (node, &ref, &ref.node_rid, &submit, 1000);
    TEST_ASSERT_EQUAL (ZLINK_SUBMIT_INVALID_STATE, submit);
    {
        std::unique_lock<std::mutex> lock (pending_probe.mutex);
        TEST_ASSERT_TRUE (pending_probe.cv.wait_for (lock, std::chrono::seconds (2), [&] {
            return pending_probe.join_done || pending_probe.failed;
        }));
        TEST_ASSERT_EQUAL (ZLINK_REQUEST_TIMED_OUT, pending_probe.join_result);
    }

    TEST_ASSERT_EQUAL (ZLINK_REQUEST_OK, zlink_actor_destroy (&actor, 1000));
    TEST_ASSERT_EQUAL (ZLINK_CLOSE_OK, zlink_close (stream));
    TEST_ASSERT_EQUAL (ZLINK_CLOSE_OK, zlink_spot_destroy (&spot));
    TEST_ASSERT_EQUAL (ZLINK_CLOSE_OK, zlink_spot_node_destroy (&node));
    TEST_ASSERT_EQUAL (ZLINK_CLOSE_OK, zlink_ctx_term (ctx));
}

void test_actor_join_entry_spot_timeout_releases_request ()
{
    void *ctx = zlink_ctx_new ();
    TEST_ASSERT_NOT_NULL (ctx);

    zlink_spot_node_options_t options;
    options.mode = ZLINK_SPOT_NODE_MODE_ALL;
    void *node = zlink_spot_node_new (ctx, &options);
    TEST_ASSERT_NOT_NULL (node);
    void *entry = NULL;
    TEST_ASSERT_EQUAL (ZLINK_CONFIG_OK, zlink_spot_node_entry_spot (node, &entry));
    TEST_ASSERT_NOT_NULL (entry);

    void *actor = zlink_spot_node_actor_new (node, "entry-timeout");
    TEST_ASSERT_NOT_NULL (actor);
    zlink_actor_ref_t ref;
    TEST_ASSERT_EQUAL (ZLINK_CONFIG_OK, zlink_actor_get_ref (actor, &ref));

    zlink_msg_t request;
    init_text_msg (&request, "entry-timeout-request");
    zlink_submit_result_t submit = ZLINK_SUBMIT_INTERNAL_ERROR;
    zlink_actor_join_entry_spot_result_t result =
      wait_spot_node_actor_join_entry_spot (node, &ref, &ref.node_rid, &request, 1, &submit, 30);
    TEST_ASSERT_EQUAL (ZLINK_SUBMIT_OK, submit);
    TEST_ASSERT_EQUAL (ZLINK_REQUEST_TIMED_OUT, result.result);

    zlink_actor_join_info_t info;
    zlink_msg_t *parts = NULL;
    size_t part_count = 0;
    TEST_ASSERT_EQUAL (ZLINK_RECV_NO_DATA,
                       zlink_spot_actor_join_recv (entry, &info, &parts, &part_count,
                                                   ZLINK_RECV_FLAGS_DONTWAIT));

    TEST_ASSERT_EQUAL (ZLINK_REQUEST_OK, zlink_actor_destroy (&actor, 1000));
    TEST_ASSERT_EQUAL (ZLINK_CLOSE_OK, zlink_spot_destroy (&entry));
    TEST_ASSERT_EQUAL (ZLINK_CLOSE_OK, zlink_spot_node_destroy (&node));
    TEST_ASSERT_EQUAL (ZLINK_CLOSE_OK, zlink_ctx_term (ctx));
}

void test_actor_join_timeout_without_dispatch_handler ()
{
    void *ctx = zlink_ctx_new ();
    TEST_ASSERT_NOT_NULL (ctx);

    zlink_spot_node_options_t options;
    options.mode = ZLINK_SPOT_NODE_MODE_ALL;
    void *node = zlink_spot_node_new (ctx, &options);
    TEST_ASSERT_NOT_NULL (node);
    void *spot = zlink_spot_new (node);
    TEST_ASSERT_NOT_NULL (spot);
    void *actor = zlink_spot_node_actor_new (node, "actor-timeout");
    TEST_ASSERT_NOT_NULL (actor);
    zlink_actor_ref_t ref;
    TEST_ASSERT_EQUAL (ZLINK_CONFIG_OK, zlink_actor_get_ref (actor, &ref));
    void *stream = zlink_socket (ctx, ZLINK_SOCKET_STREAM);
    TEST_ASSERT_NOT_NULL (stream);
    zlink_routing_id_t session_rid;
    set_rid (&session_rid, "timeout-session");
    TEST_ASSERT_EQUAL (ZLINK_REQUEST_OK,
                       wait_stream_bind_actor (node, stream, &session_rid, &ref, 1000));

    actor_probe_t probe;
    zlink_msg_t join_msg;
    init_text_msg (&join_msg, "join-timeout");
    TEST_ASSERT_EQUAL (
      ZLINK_SUBMIT_OK,
      zlink_actor_join_spot (actor, spot, &join_msg, on_join_reply, &probe, ZLINK_DONTWAIT, 30));

    {
        std::unique_lock<std::mutex> lock (probe.mutex);
        TEST_ASSERT_TRUE (
          probe.cv.wait_for (lock, std::chrono::seconds (2), [&] { return probe.join_done; }));
        TEST_ASSERT_EQUAL (ZLINK_REQUEST_TIMED_OUT, probe.join_result);
    }

    zlink_actor_join_info_t info;
    zlink_msg_t *parts = NULL;
    size_t part_count = 0;
    TEST_ASSERT_EQUAL (ZLINK_RECV_NO_DATA, zlink_spot_actor_join_recv (spot, &info, &parts,
                                                                       &part_count,
                                                                       ZLINK_RECV_FLAGS_DONTWAIT));

    TEST_ASSERT_EQUAL (ZLINK_REQUEST_OK, zlink_actor_destroy (&actor, 0));
    void *no_timeout_actor = zlink_spot_node_actor_new (node, "actor-no-timeout");
    TEST_ASSERT_NOT_NULL (no_timeout_actor);
    zlink_actor_ref_t no_timeout_ref;
    TEST_ASSERT_EQUAL (ZLINK_CONFIG_OK, zlink_actor_get_ref (no_timeout_actor, &no_timeout_ref));
    TEST_ASSERT_EQUAL (ZLINK_REQUEST_OK,
                       wait_stream_bind_actor (node, stream, &session_rid, &no_timeout_ref, 1000));
    actor_probe_t no_timeout_probe;
    zlink_msg_t no_timeout_join;
    init_text_msg (&no_timeout_join, "join-no-timeout");
    TEST_ASSERT_EQUAL (ZLINK_SUBMIT_OK,
                       zlink_actor_join_spot (no_timeout_actor, spot, &no_timeout_join,
                                              on_join_reply, &no_timeout_probe, ZLINK_DONTWAIT, 0));
    {
        std::unique_lock<std::mutex> lock (no_timeout_probe.mutex);
        TEST_ASSERT_FALSE (no_timeout_probe.cv.wait_for (
          lock, std::chrono::milliseconds (80), [&] { return no_timeout_probe.join_done; }));
    }
    TEST_ASSERT_EQUAL (
      ZLINK_RECV_OK,
      zlink_spot_actor_join_recv (spot, &info, &parts, &part_count, ZLINK_RECV_FLAGS_DONTWAIT));
    zlink_multipart_close (parts, part_count);
    zlink_msg_t no_timeout_reply;
    TEST_ASSERT_EQUAL (ZLINK_CONFIG_OK, zlink_msg_init (&no_timeout_reply));
    TEST_ASSERT_EQUAL (ZLINK_SUBMIT_OK,
                       zlink_spot_actor_join_reply (spot, &info, 1, &no_timeout_reply, 1));
    {
        std::unique_lock<std::mutex> lock (no_timeout_probe.mutex);
        TEST_ASSERT_TRUE (no_timeout_probe.cv.wait_for (
          lock, std::chrono::seconds (2), [&] { return no_timeout_probe.join_done; }));
        TEST_ASSERT_EQUAL (ZLINK_REQUEST_OK, no_timeout_probe.join_result);
    }
    TEST_ASSERT_EQUAL (ZLINK_REQUEST_OK, zlink_actor_destroy (&no_timeout_actor, 0));
    TEST_ASSERT_EQUAL (ZLINK_CLOSE_OK, zlink_close (stream));
    TEST_ASSERT_EQUAL (ZLINK_CLOSE_OK, zlink_spot_destroy (&spot));
    TEST_ASSERT_EQUAL (ZLINK_CLOSE_OK, zlink_spot_node_destroy (&node));
    TEST_ASSERT_EQUAL (ZLINK_CLOSE_OK, zlink_ctx_term (ctx));
}


void test_spot_snapshot_destroy_joined_and_pending_counts ()
{
    void *ctx = zlink_ctx_new ();
    TEST_ASSERT_NOT_NULL (ctx);

    zlink_spot_node_options_t options;
    options.mode = ZLINK_SPOT_NODE_MODE_ALL;
    void *node = zlink_spot_node_new (ctx, &options);
    TEST_ASSERT_NOT_NULL (node);

    void *joined_spot = zlink_spot_new (node);
    TEST_ASSERT_NOT_NULL (joined_spot);
    zlink_routing_id_t joined_spot_rid;
    TEST_ASSERT_EQUAL (ZLINK_CONFIG_OK, zlink_get_routing_id (joined_spot, &joined_spot_rid));
    actor_probe_t joined_probe;
    TEST_ASSERT_EQUAL (ZLINK_HANDLER_OK, zlink_spot_dispatch_event_handler (
                                           joined_spot, on_join_only_dispatch, &joined_probe));
    void *actor = zlink_spot_node_actor_new (node, "snapshot-joined");
    TEST_ASSERT_NOT_NULL (actor);
    zlink_actor_ref_t actor_ref;
    TEST_ASSERT_EQUAL (ZLINK_CONFIG_OK, zlink_actor_get_ref (actor, &actor_ref));
    void *joined_stream = zlink_socket (ctx, ZLINK_SOCKET_STREAM);
    TEST_ASSERT_NOT_NULL (joined_stream);
    zlink_routing_id_t joined_session_rid;
    set_rid (&joined_session_rid, "snapshot-joined-session");
    TEST_ASSERT_EQUAL (
      ZLINK_REQUEST_OK,
      wait_stream_bind_actor (node, joined_stream, &joined_session_rid, &actor_ref, 1000));
    zlink_msg_t join_msg;
    init_text_msg (&join_msg, "join");
    TEST_ASSERT_EQUAL (ZLINK_SUBMIT_OK,
                       zlink_actor_join_spot (actor, joined_spot, &join_msg, on_join_reply,
                                              &joined_probe, ZLINK_DONTWAIT, 1000));
    {
        std::unique_lock<std::mutex> lock (joined_probe.mutex);
        TEST_ASSERT_TRUE (joined_probe.cv.wait_for (lock, std::chrono::seconds (2),
                                                    [&] { return joined_probe.join_done; }));
        TEST_ASSERT_EQUAL (ZLINK_REQUEST_OK, joined_probe.join_result);
    }

    zlink_spot_node_spot_entry_t row;
    TEST_ASSERT_TRUE (find_spot_snapshot_row (node, joined_spot_rid, &row));
    TEST_ASSERT_EQUAL (ZLINK_SPOT_KIND_USER, row.spot_kind);
    TEST_ASSERT_EQUAL_UINT32 (1, row.joined_actor_count);
    TEST_ASSERT_EQUAL (ZLINK_CLOSE_BUSY, zlink_spot_destroy (&joined_spot));
    TEST_ASSERT_EQUAL (ZLINK_CONFIG_OK, zlink_actor_leave_spot (actor, joined_spot));
    TEST_ASSERT_TRUE (find_spot_snapshot_row (node, joined_spot_rid, &row));
    TEST_ASSERT_EQUAL_UINT32 (0, row.joined_actor_count);

    void *pending_spot = zlink_spot_new (node);
    TEST_ASSERT_NOT_NULL (pending_spot);
    zlink_routing_id_t pending_spot_rid;
    TEST_ASSERT_EQUAL (ZLINK_CONFIG_OK, zlink_get_routing_id (pending_spot, &pending_spot_rid));
    void *pending_actor = zlink_spot_node_actor_new (node, "snapshot-pending");
    TEST_ASSERT_NOT_NULL (pending_actor);
    zlink_actor_ref_t pending_actor_ref;
    TEST_ASSERT_EQUAL (ZLINK_CONFIG_OK, zlink_actor_get_ref (pending_actor, &pending_actor_ref));
    void *pending_stream = zlink_socket (ctx, ZLINK_SOCKET_STREAM);
    TEST_ASSERT_NOT_NULL (pending_stream);
    zlink_routing_id_t pending_session_rid;
    set_rid (&pending_session_rid, "snapshot-pending-session");
    TEST_ASSERT_EQUAL (ZLINK_REQUEST_OK,
                       wait_stream_bind_actor (node, pending_stream, &pending_session_rid,
                                               &pending_actor_ref, 1000));
    actor_probe_t pending_probe;
    zlink_msg_t pending_join;
    init_text_msg (&pending_join, "pending");
    TEST_ASSERT_EQUAL (ZLINK_SUBMIT_OK,
                       zlink_actor_join_spot (pending_actor, pending_spot, &pending_join,
                                              on_join_reply, &pending_probe, ZLINK_DONTWAIT, 5000));
    TEST_ASSERT_TRUE (find_spot_snapshot_row (node, pending_spot_rid, &row));
    TEST_ASSERT_EQUAL (ZLINK_SPOT_KIND_USER, row.spot_kind);
    TEST_ASSERT_EQUAL_UINT32 (1, row.pending_actor_join_count);
    TEST_ASSERT_EQUAL (ZLINK_CLOSE_BUSY, zlink_spot_destroy (&pending_spot));
    zlink_actor_join_info_t pending_info;
    zlink_msg_t *pending_parts = NULL;
    size_t pending_part_count = 0;
    TEST_ASSERT_EQUAL (ZLINK_RECV_OK,
                       zlink_spot_actor_join_recv (pending_spot, &pending_info, &pending_parts,
                                                   &pending_part_count,
                                                   ZLINK_RECV_FLAGS_DONTWAIT));
    zlink_multipart_close (pending_parts, pending_part_count);
    zlink_msg_t pending_reply;
    zlink_msg_init (&pending_reply);
    TEST_ASSERT_EQUAL (ZLINK_SUBMIT_OK, zlink_spot_actor_join_reply (pending_spot, &pending_info, 1,
                                                                     &pending_reply, 1));
    {
        std::unique_lock<std::mutex> lock (pending_probe.mutex);
        TEST_ASSERT_TRUE (pending_probe.cv.wait_for (lock, std::chrono::seconds (2),
                                                     [&] { return pending_probe.join_done; }));
        TEST_ASSERT_EQUAL (ZLINK_REQUEST_OK, pending_probe.join_result);
    }
    TEST_ASSERT_EQUAL (ZLINK_CLOSE_OK, zlink_spot_destroy (&pending_spot));
    TEST_ASSERT_FALSE (find_spot_snapshot_row (node, pending_spot_rid, &row));

    TEST_ASSERT_EQUAL (ZLINK_REQUEST_OK, zlink_actor_destroy (&actor, 0));
    TEST_ASSERT_EQUAL (ZLINK_REQUEST_OK, zlink_actor_destroy (&pending_actor, 0));
    TEST_ASSERT_EQUAL (ZLINK_CLOSE_OK, zlink_close (pending_stream));
    TEST_ASSERT_EQUAL (ZLINK_CLOSE_OK, zlink_close (joined_stream));
    TEST_ASSERT_EQUAL (ZLINK_CLOSE_OK, zlink_spot_destroy (&joined_spot));
    TEST_ASSERT_EQUAL (ZLINK_CLOSE_OK, zlink_spot_node_destroy (&node));
    TEST_ASSERT_EQUAL (ZLINK_CLOSE_OK, zlink_ctx_term (ctx));
}

void test_stream_close_aborts_pending_join_without_leaving_current_spot ()
{
    void *ctx = zlink_ctx_new ();
    TEST_ASSERT_NOT_NULL (ctx);

    zlink_spot_node_options_t options;
    options.mode = ZLINK_SPOT_NODE_MODE_ALL;
    void *node = zlink_spot_node_new (ctx, &options);
    TEST_ASSERT_NOT_NULL (node);

    void *joined_spot = zlink_spot_new (node);
    TEST_ASSERT_NOT_NULL (joined_spot);
    zlink_routing_id_t joined_spot_rid;
    TEST_ASSERT_EQUAL (ZLINK_CONFIG_OK, zlink_get_routing_id (joined_spot, &joined_spot_rid));
    actor_probe_t joined_probe;
    TEST_ASSERT_EQUAL (ZLINK_HANDLER_OK, zlink_spot_dispatch_event_handler (
                                           joined_spot, on_join_only_dispatch, &joined_probe));

    void *pending_spot = zlink_spot_new (node);
    TEST_ASSERT_NOT_NULL (pending_spot);

    void *actor = zlink_spot_node_actor_new (node, "close-pending-join");
    TEST_ASSERT_NOT_NULL (actor);
    zlink_actor_ref_t actor_ref;
    TEST_ASSERT_EQUAL (ZLINK_CONFIG_OK, zlink_actor_get_ref (actor, &actor_ref));

    void *stream = zlink_socket (ctx, ZLINK_SOCKET_STREAM);
    TEST_ASSERT_NOT_NULL (stream);
    zlink_routing_id_t session_rid;
    set_rid (&session_rid, "close-pending-join-session");
    TEST_ASSERT_EQUAL (ZLINK_REQUEST_OK,
                       wait_stream_bind_actor (node, stream, &session_rid, &actor_ref, 1000));

    zlink_msg_t join_msg;
    init_text_msg (&join_msg, "join");
    TEST_ASSERT_EQUAL (ZLINK_SUBMIT_OK,
                       zlink_actor_join_spot (actor, joined_spot, &join_msg, on_join_reply,
                                              &joined_probe, ZLINK_DONTWAIT, 1000));
    {
        std::unique_lock<std::mutex> lock (joined_probe.mutex);
        TEST_ASSERT_TRUE (joined_probe.cv.wait_for (lock, std::chrono::seconds (2),
                                                    [&] { return joined_probe.join_done; }));
        TEST_ASSERT_EQUAL (ZLINK_REQUEST_OK, joined_probe.join_result);
    }

    actor_probe_t pending_probe;
    zlink_msg_t pending_join;
    init_text_msg (&pending_join, "pending");
    TEST_ASSERT_EQUAL (ZLINK_SUBMIT_OK,
                       zlink_actor_join_spot (actor, pending_spot, &pending_join, on_join_reply,
                                              &pending_probe, ZLINK_DONTWAIT, 5000));

    TEST_ASSERT_EQUAL (ZLINK_CLOSE_OK, zlink_close (stream));
    {
        std::unique_lock<std::mutex> lock (pending_probe.mutex);
        TEST_ASSERT_TRUE (pending_probe.cv.wait_for (lock, std::chrono::seconds (2),
                                                     [&] { return pending_probe.join_done; }));
        TEST_ASSERT_EQUAL (ZLINK_REQUEST_TERMINATED, pending_probe.join_result);
    }

    zlink_spot_node_spot_entry_t row;
    TEST_ASSERT_TRUE (find_spot_snapshot_row (node, joined_spot_rid, &row));
    TEST_ASSERT_EQUAL_UINT32 (1, row.joined_actor_count);
    TEST_ASSERT_EQUAL (ZLINK_CLOSE_BUSY, zlink_spot_destroy (&joined_spot));

    TEST_ASSERT_EQUAL (ZLINK_CONFIG_OK, zlink_actor_leave_spot (actor, joined_spot));
    TEST_ASSERT_EQUAL (ZLINK_REQUEST_OK, zlink_actor_destroy (&actor, 0));
    TEST_ASSERT_EQUAL (ZLINK_CLOSE_OK, zlink_spot_destroy (&pending_spot));
    TEST_ASSERT_EQUAL (ZLINK_CLOSE_OK, zlink_spot_destroy (&joined_spot));
    TEST_ASSERT_EQUAL (ZLINK_CLOSE_OK, zlink_spot_node_destroy (&node));
    TEST_ASSERT_EQUAL (ZLINK_CLOSE_OK, zlink_ctx_term (ctx));
}


void test_stream_remote_actor_owner_missing_route_and_unbind_cleanup ()
{
    void *ctx = zlink_ctx_new ();
    TEST_ASSERT_NOT_NULL (ctx);

    zlink_spot_node_options_t options;
    options.mode = ZLINK_SPOT_NODE_MODE_ALL;
    void *session_owner = zlink_spot_node_new (ctx, &options);
    TEST_ASSERT_NOT_NULL (session_owner);
    void *actor_owner = zlink_spot_node_new (ctx, &options);
    TEST_ASSERT_NOT_NULL (actor_owner);

    void *actor = zlink_spot_node_actor_new (actor_owner, "remote-gone");
    TEST_ASSERT_NOT_NULL (actor);
    zlink_actor_ref_t ref;
    TEST_ASSERT_EQUAL (ZLINK_CONFIG_OK, zlink_actor_get_ref (actor, &ref));

    void *stream = zlink_socket (ctx, ZLINK_SOCKET_STREAM);
    TEST_ASSERT_NOT_NULL (stream);
    zlink_routing_id_t session_rid;
    set_rid (&session_rid, "remote-gone-session");
    TEST_ASSERT_EQUAL (ZLINK_REQUEST_OK,
                       wait_stream_bind_actor (session_owner, stream, &session_rid, &ref, 1000));

    TEST_ASSERT_EQUAL (ZLINK_CLOSE_OK, zlink_spot_node_destroy (&actor_owner));

    zlink_msg_t part;
    init_text_msg (&part, "still-owned");
    TEST_ASSERT_EQUAL (ZLINK_SUBMIT_NOT_CONNECTED,
                       test_stream_send_bound_actor_part (session_owner, stream, &session_rid,
                                                          "remote-gone", &part, ZLINK_DONTWAIT,
                                                          ZLINK_PART_FINAL));
    TEST_ASSERT_EQUAL_STRING ("still-owned", msg_text (&part).c_str ());
    TEST_ASSERT_EQUAL (ZLINK_CONFIG_OK, zlink_msg_close (&part));

    TEST_ASSERT_EQUAL (
      ZLINK_REQUEST_OK,
      wait_stream_unbind_actor (session_owner, stream, &session_rid, "remote-gone", 1000));
    init_text_msg (&part, "gone");
    TEST_ASSERT_EQUAL (ZLINK_SUBMIT_NOT_FOUND, test_stream_send_bound_actor_part (
                                                 session_owner, stream, &session_rid, "remote-gone",
                                                 &part, ZLINK_DONTWAIT, ZLINK_PART_FINAL));
    TEST_ASSERT_EQUAL (ZLINK_CONFIG_OK, zlink_msg_close (&part));

    TEST_ASSERT_EQUAL (ZLINK_CLOSE_OK, zlink_close (stream));
    TEST_ASSERT_EQUAL (ZLINK_CLOSE_OK, zlink_spot_node_destroy (&session_owner));
    TEST_ASSERT_EQUAL (ZLINK_CLOSE_OK, zlink_ctx_term (ctx));
}


void test_stream_bind_external_remote_actor_ref_keeps_logical_binding ()
{
    void *ctx = zlink_ctx_new ();
    TEST_ASSERT_NOT_NULL (ctx);

    zlink_spot_node_options_t options;
    options.mode = ZLINK_SPOT_NODE_MODE_ALL;
    void *session_owner = zlink_spot_node_new (ctx, &options);
    TEST_ASSERT_NOT_NULL (session_owner);

    void *stream = zlink_socket (ctx, ZLINK_SOCKET_STREAM);
    TEST_ASSERT_NOT_NULL (stream);
    zlink_routing_id_t session_rid;
    set_rid (&session_rid, "external-remote-session");

    zlink_actor_ref_t remote_ref;
    memset (&remote_ref, 0, sizeof (remote_ref));
    set_rid (&remote_ref.node_rid, "external-actor-node");
    strncpy (remote_ref.actor_id, "external-remote-actor", ZLINK_ACTOR_ID_MAX - 1);
    remote_ref.generation = 42;

    TEST_ASSERT_EQUAL (ZLINK_REQUEST_OK, wait_stream_bind_actor (session_owner, stream,
                                                                 &session_rid, &remote_ref, 1000));

    size_t count = 0;
    TEST_ASSERT_EQUAL (ZLINK_CONFIG_OK,
                       zlink_stream_bound_actors (stream, &session_rid, NULL, &count));
    TEST_ASSERT_EQUAL_UINT64 (1, count);

    zlink_actor_ref_t entries[1];
    count = 1;
    memset (entries, 0, sizeof (entries));
    TEST_ASSERT_EQUAL (ZLINK_CONFIG_OK,
                       zlink_stream_bound_actors (stream, &session_rid, entries, &count));
    TEST_ASSERT_EQUAL_UINT64 (1, count);
    TEST_ASSERT_EQUAL_UINT8 (remote_ref.node_rid.size, entries[0].node_rid.size);
    TEST_ASSERT_EQUAL_MEMORY (remote_ref.node_rid.data, entries[0].node_rid.data,
                              remote_ref.node_rid.size);
    TEST_ASSERT_EQUAL_STRING (remote_ref.actor_id, entries[0].actor_id);
    TEST_ASSERT_EQUAL_UINT64 (remote_ref.generation, entries[0].generation);

    TEST_ASSERT_EQUAL (ZLINK_REQUEST_OK,
                       wait_stream_unbind_actor (session_owner, stream, &session_rid,
                                                 "external-remote-actor", 1000));

    TEST_ASSERT_EQUAL (ZLINK_CLOSE_OK, zlink_close (stream));
    TEST_ASSERT_EQUAL (ZLINK_CLOSE_OK, zlink_spot_node_destroy (&session_owner));
    TEST_ASSERT_EQUAL (ZLINK_CLOSE_OK, zlink_ctx_term (ctx));
}

void test_stream_bind_same_process_remote_actor_ref_keeps_logical_binding ()
{
    void *ctx = zlink_ctx_new ();
    TEST_ASSERT_NOT_NULL (ctx);

    zlink_spot_node_options_t options;
    options.mode = ZLINK_SPOT_NODE_MODE_ROUTED;
    void *session_owner = zlink_spot_node_new (ctx, &options);
    TEST_ASSERT_NOT_NULL (session_owner);
    void *actor_owner = zlink_spot_node_new (ctx, &options);
    TEST_ASSERT_NOT_NULL (actor_owner);

    void *actor = zlink_spot_node_actor_new (actor_owner, "same-process-remote");
    TEST_ASSERT_NOT_NULL (actor);
    zlink_actor_ref_t ref;
    TEST_ASSERT_EQUAL (ZLINK_CONFIG_OK, zlink_actor_get_ref (actor, &ref));

    void *stream = zlink_socket (ctx, ZLINK_SOCKET_STREAM);
    TEST_ASSERT_NOT_NULL (stream);
    zlink_routing_id_t session_rid;
    set_rid (&session_rid, "same-process-remote-session");

    TEST_ASSERT_EQUAL (ZLINK_REQUEST_OK,
                       wait_stream_bind_actor (session_owner, stream, &session_rid, &ref, 1000));

    TEST_ASSERT_EQUAL (ZLINK_REQUEST_OK, zlink_actor_destroy (&actor, 0));

    TEST_ASSERT_EQUAL (
      ZLINK_REQUEST_OK,
      wait_stream_unbind_actor (session_owner, stream, &session_rid, "same-process-remote", 1000));
    TEST_ASSERT_EQUAL (ZLINK_CLOSE_OK, zlink_close (stream));
    TEST_ASSERT_EQUAL (ZLINK_CLOSE_OK, zlink_spot_node_destroy (&actor_owner));
    TEST_ASSERT_EQUAL (ZLINK_CLOSE_OK, zlink_spot_node_destroy (&session_owner));
    TEST_ASSERT_EQUAL (ZLINK_CLOSE_OK, zlink_ctx_term (ctx));
}

void test_stream_remote_actor_gateway_routes_same_process_remote_ref ()
{
    void *ctx = zlink_ctx_new ();
    TEST_ASSERT_NOT_NULL (ctx);

    zlink_spot_node_options_t options;
    options.mode = ZLINK_SPOT_NODE_MODE_ROUTED;
    void *session_owner = zlink_spot_node_new (ctx, &options);
    TEST_ASSERT_NOT_NULL (session_owner);
    void *actor_owner = zlink_spot_node_new (ctx, &options);
    TEST_ASSERT_NOT_NULL (actor_owner);

    TEST_ASSERT_EQUAL (ZLINK_CONFIG_OK,
                       zlink_set_routing_id (session_owner, "ag-session-node", 15));
    TEST_ASSERT_EQUAL (ZLINK_CONFIG_OK, zlink_set_routing_id (actor_owner, "ag-actor-node", 13));

    char session_endpoint[MAX_SOCKET_STRING];
    char actor_endpoint[MAX_SOCKET_STRING];
    TEST_ASSERT_TRUE (
      bind_spot_router_endpoint (session_owner, session_endpoint, sizeof (session_endpoint)));
    TEST_ASSERT_TRUE (
      bind_spot_router_endpoint (actor_owner, actor_endpoint, sizeof (actor_endpoint)));

    void *entry = NULL;
    TEST_ASSERT_EQUAL (ZLINK_CONFIG_OK, zlink_spot_node_entry_spot (actor_owner, &entry));
    TEST_ASSERT_NOT_NULL (entry);
    actor_probe_t probe;
    TEST_ASSERT_EQUAL (ZLINK_HANDLER_OK,
                       zlink_spot_dispatch_event_handler (entry, on_dispatch, &probe));

    void *actor = zlink_spot_node_actor_new (actor_owner, "remote-route");
    TEST_ASSERT_NOT_NULL (actor);
    zlink_actor_ref_t ref;
    TEST_ASSERT_EQUAL (ZLINK_CONFIG_OK, zlink_actor_get_ref (actor, &ref));

    zlink_routing_id_t session_rid_value;
    memset (&session_rid_value, 0, sizeof (session_rid_value));
    TEST_ASSERT_EQUAL (ZLINK_CONFIG_OK, zlink_get_routing_id (session_owner, &session_rid_value));

    TEST_ASSERT_EQUAL (ZLINK_CONNECT_OK,
                       zlink_spot_node_connect_router_channel_peer_rid (
                         session_owner, "actor-node", &ref.node_rid, actor_endpoint));
    TEST_ASSERT_EQUAL (ZLINK_CONNECT_OK,
                       zlink_spot_node_connect_router_channel_peer_rid (
                         actor_owner, "actor-node", &session_rid_value, session_endpoint));

    void *stream = zlink_socket (ctx, ZLINK_SOCKET_STREAM);
    TEST_ASSERT_NOT_NULL (stream);
    zlink_routing_id_t session_rid;
    set_rid (&session_rid, "remote-route-session");
    TEST_ASSERT_EQUAL (ZLINK_REQUEST_OK,
                       wait_stream_bind_actor (session_owner, stream, &session_rid, &ref, 1000));

    TEST_ASSERT_TRUE (zlink_test_wait_until (2000, [&] {
        zlink_msg_t part;
        init_text_msg (&part, "remote-route-payload");
        const zlink_submit_result_t rc =
          test_stream_send_bound_actor_part (session_owner, stream, &session_rid, "remote-route",
                                             &part, ZLINK_DONTWAIT, ZLINK_PART_FINAL);
        if (rc != ZLINK_SUBMIT_OK)
            zlink_msg_close (&part);
        return rc == ZLINK_SUBMIT_OK;
    }));

    {
        std::unique_lock<std::mutex> lock (probe.mutex);
        TEST_ASSERT_TRUE (probe.cv.wait_for (lock, std::chrono::seconds (2),
                                             [&] { return probe.actor_event || probe.failed; }));
        TEST_ASSERT_FALSE (probe.failed);
        TEST_ASSERT_EQUAL_STRING ("remote-route-payload", probe.payload.c_str ());
    }

    TEST_ASSERT_EQUAL (ZLINK_REQUEST_OK, zlink_actor_destroy (&actor, 0));
    TEST_ASSERT_EQUAL (ZLINK_CLOSE_OK, zlink_close (stream));
    TEST_ASSERT_EQUAL (ZLINK_CLOSE_OK, zlink_spot_destroy (&entry));
    TEST_ASSERT_EQUAL (ZLINK_CLOSE_OK, zlink_spot_node_destroy (&actor_owner));
    TEST_ASSERT_EQUAL (ZLINK_CLOSE_OK, zlink_spot_node_destroy (&session_owner));
    TEST_ASSERT_EQUAL (ZLINK_CLOSE_OK, zlink_ctx_term (ctx));
}

void test_stream_remote_actor_gateway_first_send_does_not_fake_success ()
{
    void *ctx = zlink_ctx_new ();
    TEST_ASSERT_NOT_NULL (ctx);

    zlink_spot_node_options_t options;
    options.mode = ZLINK_SPOT_NODE_MODE_ROUTED;
    void *session_owner = zlink_spot_node_new (ctx, &options);
    TEST_ASSERT_NOT_NULL (session_owner);
    void *actor_owner = zlink_spot_node_new (ctx, &options);
    TEST_ASSERT_NOT_NULL (actor_owner);

    TEST_ASSERT_EQUAL (ZLINK_CONFIG_OK,
                       zlink_set_routing_id (session_owner, "fg-session-node", 15));
    TEST_ASSERT_EQUAL (ZLINK_CONFIG_OK,
                       zlink_set_routing_id (actor_owner, "fg-actor-node", 13));

    char session_endpoint[MAX_SOCKET_STRING];
    char actor_endpoint[MAX_SOCKET_STRING];
    TEST_ASSERT_TRUE (
      bind_spot_router_endpoint (session_owner, session_endpoint, sizeof (session_endpoint)));
    TEST_ASSERT_TRUE (
      bind_spot_router_endpoint (actor_owner, actor_endpoint, sizeof (actor_endpoint)));

    void *entry = NULL;
    TEST_ASSERT_EQUAL (ZLINK_CONFIG_OK, zlink_spot_node_entry_spot (actor_owner, &entry));
    TEST_ASSERT_NOT_NULL (entry);
    actor_probe_t probe;
    TEST_ASSERT_EQUAL (ZLINK_HANDLER_OK,
                       zlink_spot_dispatch_event_handler (entry, on_dispatch, &probe));

    void *actor = zlink_spot_node_actor_new (actor_owner, "first-gateway");
    TEST_ASSERT_NOT_NULL (actor);
    zlink_actor_ref_t ref;
    TEST_ASSERT_EQUAL (ZLINK_CONFIG_OK, zlink_actor_get_ref (actor, &ref));

    zlink_routing_id_t session_node_rid;
    memset (&session_node_rid, 0, sizeof (session_node_rid));
    TEST_ASSERT_EQUAL (ZLINK_CONFIG_OK, zlink_get_routing_id (session_owner, &session_node_rid));

    TEST_ASSERT_EQUAL (ZLINK_CONNECT_OK,
                       zlink_spot_node_connect_router_channel_peer_rid (
                         session_owner, "actor-node", &ref.node_rid, actor_endpoint));
    TEST_ASSERT_EQUAL (ZLINK_CONNECT_OK,
                       zlink_spot_node_connect_router_channel_peer_rid (
                         actor_owner, "session-node", &session_node_rid, session_endpoint));

    void *stream = zlink_socket (ctx, ZLINK_SOCKET_STREAM);
    TEST_ASSERT_NOT_NULL (stream);
    zlink_routing_id_t session_rid;
    set_rid (&session_rid, "first-gateway-session");
    TEST_ASSERT_EQUAL (ZLINK_REQUEST_OK,
                       wait_stream_bind_actor (session_owner, stream, &session_rid, &ref, 1000));

    zlink_msg_t part;
    init_text_msg (&part, "first-gateway-payload");
    const zlink_submit_result_t rc =
      test_stream_send_bound_actor_part (session_owner, stream, &session_rid, "first-gateway",
                                         &part, ZLINK_DONTWAIT, ZLINK_PART_FINAL);

    if (rc == ZLINK_SUBMIT_OK) {
        std::unique_lock<std::mutex> lock (probe.mutex);
        TEST_ASSERT_TRUE (probe.cv.wait_for (lock, std::chrono::seconds (2), [&] {
            return probe.actor_event || probe.failed;
        }));
        TEST_ASSERT_FALSE (probe.failed);
        TEST_ASSERT_EQUAL_STRING ("first-gateway-payload", probe.payload.c_str ());
    } else {
        TEST_ASSERT_TRUE (rc == ZLINK_SUBMIT_NOT_CONNECTED || rc == ZLINK_SUBMIT_BACKPRESSURED);
        TEST_ASSERT_EQUAL_STRING ("first-gateway-payload", msg_text (&part).c_str ());
        TEST_ASSERT_EQUAL (ZLINK_CONFIG_OK, zlink_msg_close (&part));
    }

    TEST_ASSERT_EQUAL (ZLINK_REQUEST_OK, zlink_actor_destroy (&actor, 0));
    TEST_ASSERT_EQUAL (ZLINK_CLOSE_OK, zlink_close (stream));
    TEST_ASSERT_EQUAL (ZLINK_CLOSE_OK, zlink_spot_destroy (&entry));
    TEST_ASSERT_EQUAL (ZLINK_CLOSE_OK, zlink_spot_node_destroy (&actor_owner));
    TEST_ASSERT_EQUAL (ZLINK_CLOSE_OK, zlink_spot_node_destroy (&session_owner));
    TEST_ASSERT_EQUAL (ZLINK_CLOSE_OK, zlink_ctx_term (ctx));
}


void test_stream_multipart_selector_and_unbound_relay ()
{
    void *ctx = zlink_ctx_new ();
    TEST_ASSERT_NOT_NULL (ctx);

    zlink_spot_node_options_t options;
    options.mode = ZLINK_SPOT_NODE_MODE_ALL;
    void *node = zlink_spot_node_new (ctx, &options);
    TEST_ASSERT_NOT_NULL (node);
    void *actor_a = zlink_spot_node_actor_new (node, "multi-a");
    void *actor_b = zlink_spot_node_actor_new (node, "multi-b");
    TEST_ASSERT_NOT_NULL (actor_a);
    TEST_ASSERT_NOT_NULL (actor_b);
    void *stream = zlink_socket (ctx, ZLINK_SOCKET_STREAM);
    TEST_ASSERT_NOT_NULL (stream);

    zlink_routing_id_t session_rid;
    set_rid (&session_rid, "multipart-session");
    zlink_actor_ref_t ref_a;
    zlink_actor_ref_t ref_b;
    TEST_ASSERT_EQUAL (ZLINK_CONFIG_OK, zlink_actor_get_ref (actor_a, &ref_a));
    TEST_ASSERT_EQUAL (ZLINK_CONFIG_OK, zlink_actor_get_ref (actor_b, &ref_b));
    TEST_ASSERT_EQUAL (ZLINK_REQUEST_OK,
                       wait_stream_bind_actor (node, stream, &session_rid, &ref_a, 1000));
    TEST_ASSERT_EQUAL (ZLINK_REQUEST_OK,
                       wait_stream_bind_actor (node, stream, &session_rid, &ref_b, 1000));

    zlink_msg_t missing;
    init_text_msg (&missing, "missing");
    TEST_ASSERT_EQUAL (ZLINK_SUBMIT_NOT_FOUND, test_stream_send_bound_actor_part (
                                                 node, stream, &session_rid, "missing", &missing,
                                                 ZLINK_DONTWAIT, ZLINK_PART_FINAL));
    TEST_ASSERT_EQUAL (ZLINK_CONFIG_OK, zlink_msg_close (&missing));

    zlink_msg_t first;
    init_text_msg (&first, "first");
    TEST_ASSERT_EQUAL (ZLINK_SUBMIT_OK,
                       test_stream_send_bound_actor_part (node, stream, &session_rid, "multi-a",
                                                          &first, ZLINK_DONTWAIT, ZLINK_PART_MORE));
    void *destroy_candidate = actor_a;
    TEST_ASSERT_EQUAL (ZLINK_REQUEST_BUSY, zlink_actor_destroy (&destroy_candidate, 1000));
    TEST_ASSERT_EQUAL_PTR (actor_a, destroy_candidate);

    zlink_msg_t wrong_final;
    init_text_msg (&wrong_final, "wrong");
    TEST_ASSERT_EQUAL (ZLINK_SUBMIT_INVALID_STATE,
                       test_stream_send_bound_actor_part (node, stream, &session_rid, "multi-b",
                                                          &wrong_final, ZLINK_DONTWAIT,
                                                          ZLINK_PART_FINAL));
    TEST_ASSERT_EQUAL (ZLINK_CONFIG_OK, zlink_msg_close (&wrong_final));

    zlink_msg_t final;
    init_text_msg (&final, "final");
    TEST_ASSERT_EQUAL (ZLINK_SUBMIT_OK, test_stream_send_bound_actor_part (
                                          node, stream, &session_rid, "multi-a", &final,
                                          ZLINK_DONTWAIT, ZLINK_PART_FINAL));

    zlink_spot_node_actor_entry_t rows[2];
    size_t count = 2;
    TEST_ASSERT_EQUAL (ZLINK_CONFIG_OK, zlink_spot_node_actors (node, rows, &count));
    TEST_ASSERT_EQUAL_UINT (2, count);
    uint32_t pending_a = 0;
    uint32_t pending_b = 0;
    for (size_t i = 0; i < 2; ++i) {
        if (strncmp (rows[i].actor.actor_id, "multi-a", ZLINK_ACTOR_ID_MAX) == 0) {
            pending_a = rows[i].pending_message_count;
            TEST_ASSERT_TRUE (rows[i].current_spot_rid.size > 0);
            TEST_ASSERT_TRUE (rows[i].current_spot_kind == ZLINK_SPOT_KIND_ENTRY
                              || rows[i].current_spot_kind == ZLINK_SPOT_KIND_USER);
        }
        if (strncmp (rows[i].actor.actor_id, "multi-b", ZLINK_ACTOR_ID_MAX) == 0) {
            pending_b = rows[i].pending_message_count;
            TEST_ASSERT_TRUE (rows[i].current_spot_rid.size > 0);
            TEST_ASSERT_TRUE (rows[i].current_spot_kind == ZLINK_SPOT_KIND_ENTRY
                              || rows[i].current_spot_kind == ZLINK_SPOT_KIND_USER);
        }
    }
    TEST_ASSERT_EQUAL_UINT32 (2, pending_a);
    TEST_ASSERT_EQUAL_UINT32 (0, pending_b);

    TEST_ASSERT_EQUAL (ZLINK_REQUEST_OK, zlink_actor_destroy (&actor_a, 0));
    TEST_ASSERT_EQUAL (ZLINK_REQUEST_OK, zlink_actor_destroy (&actor_b, 0));
    TEST_ASSERT_EQUAL (ZLINK_CLOSE_OK, zlink_close (stream));
    TEST_ASSERT_EQUAL (ZLINK_CLOSE_OK, zlink_spot_node_destroy (&node));
    TEST_ASSERT_EQUAL (ZLINK_CLOSE_OK, zlink_ctx_term (ctx));
}

void test_actor_queue_dispatch_receive_and_backpressure ()
{
    void *ctx = zlink_ctx_new ();
    TEST_ASSERT_NOT_NULL (ctx);

    zlink_spot_node_options_t options;
    options.mode = ZLINK_SPOT_NODE_MODE_ALL;
    void *node = zlink_spot_node_new (ctx, &options);
    TEST_ASSERT_NOT_NULL (node);
    void *spot = zlink_spot_new (node);
    TEST_ASSERT_NOT_NULL (spot);
    void *actor = zlink_spot_node_actor_new (node, "queue-dispatch");
    TEST_ASSERT_NOT_NULL (actor);
    void *stream = zlink_socket (ctx, ZLINK_SOCKET_STREAM);
    TEST_ASSERT_NOT_NULL (stream);
    zlink_routing_id_t session_rid;
    set_rid (&session_rid, "queue-session");
    zlink_actor_ref_t ref;
    TEST_ASSERT_EQUAL (ZLINK_CONFIG_OK, zlink_actor_get_ref (actor, &ref));
    TEST_ASSERT_EQUAL (ZLINK_REQUEST_OK,
                       wait_stream_bind_actor (node, stream, &session_rid, &ref, 1000));

    actor_probe_t probe;
    probe.try_destroy_in_actor_callback = true;
    TEST_ASSERT_EQUAL (ZLINK_HANDLER_OK,
                       zlink_spot_dispatch_event_handler (spot, on_dispatch, &probe));

    zlink_msg_t join_msg;
    init_text_msg (&join_msg, "join");
    TEST_ASSERT_EQUAL (
      ZLINK_SUBMIT_OK,
      zlink_actor_join_spot (actor, spot, &join_msg, on_join_reply, &probe, ZLINK_DONTWAIT, 1000));
    {
        std::unique_lock<std::mutex> lock (probe.mutex);
        TEST_ASSERT_TRUE (
          probe.cv.wait_for (lock, std::chrono::seconds (2), [&] { return probe.join_done; }));
        TEST_ASSERT_EQUAL (ZLINK_REQUEST_OK, probe.join_result);
        probe.actor_event = false;
        probe.actor_subject_ok = false;
        probe.actor_no_data_after_drain = false;
        probe.actor_event_count = 0;
        probe.actor_recv_count = 0;
        probe.payload.clear ();
    }

    zlink_actor_recv_info_t outside_info;
    zlink_msg_t outside_part;
    memset (&outside_part, 0, sizeof (outside_part));
    zlink_part_flag_t outside_flag = ZLINK_PART_FINAL;
    TEST_ASSERT_EQUAL (ZLINK_RECV_NOT_SUPPORTED,
                       zlink_actor_recv_part (actor, &outside_info, &outside_part, &outside_flag,
                                              ZLINK_RECV_FLAGS_DONTWAIT));

    zlink_msg_t first;
    init_text_msg (&first, "first");
    TEST_ASSERT_EQUAL (ZLINK_SUBMIT_OK, test_stream_send_bound_actor_part (
                                          node, stream, &session_rid, "queue-dispatch", &first,
                                          ZLINK_DONTWAIT, ZLINK_PART_MORE));
    {
        std::unique_lock<std::mutex> lock (probe.mutex);
        TEST_ASSERT_TRUE (probe.cv.wait_for (lock, std::chrono::seconds (2), [&] {
            return probe.actor_recv_count >= 1 || probe.failed;
        }));
        TEST_ASSERT_FALSE (probe.failed);
        TEST_ASSERT_TRUE (probe.actor_subject_ok);
        TEST_ASSERT_TRUE (probe.actor_no_data_after_drain);
        TEST_ASSERT_TRUE (probe.destroy_in_actor_callback_blocked);
        TEST_ASSERT_EQUAL (ZLINK_PART_MORE, probe.first_part_flag);
        TEST_ASSERT_EQUAL_STRING ("first", probe.payload.c_str ());
    }

    zlink_msg_t final;
    init_text_msg (&final, "final");
    TEST_ASSERT_EQUAL (ZLINK_SUBMIT_OK, test_stream_send_bound_actor_part (
                                          node, stream, &session_rid, "queue-dispatch", &final,
                                          ZLINK_DONTWAIT, ZLINK_PART_FINAL));
    {
        std::unique_lock<std::mutex> lock (probe.mutex);
        TEST_ASSERT_TRUE (probe.cv.wait_for (lock, std::chrono::seconds (2), [&] {
            return probe.actor_recv_count >= 2 || probe.failed;
        }));
        TEST_ASSERT_FALSE (probe.failed);
        TEST_ASSERT_EQUAL_STRING ("firstfinal", probe.payload.c_str ());
    }

    void *backpressure_actor = zlink_spot_node_actor_new (node, "queue-backpressure");
    TEST_ASSERT_NOT_NULL (backpressure_actor);
    zlink_actor_ref_t backpressure_ref;
    TEST_ASSERT_EQUAL (ZLINK_CONFIG_OK,
                       zlink_actor_get_ref (backpressure_actor, &backpressure_ref));
    TEST_ASSERT_EQUAL (ZLINK_REQUEST_OK, wait_stream_bind_actor (node, stream, &session_rid,
                                                                 &backpressure_ref, 1000));
    for (size_t i = 0; i < 1024; ++i) {
        zlink_msg_t msg;
        init_text_msg (&msg, "x");
        TEST_ASSERT_EQUAL (ZLINK_SUBMIT_OK, test_stream_send_bound_actor_part (
                                              node, stream, &session_rid, "queue-backpressure",
                                              &msg, ZLINK_DONTWAIT, ZLINK_PART_FINAL));
    }
    zlink_msg_t over_limit;
    init_text_msg (&over_limit, "owned-by-caller");
    TEST_ASSERT_EQUAL (ZLINK_SUBMIT_OK, test_stream_send_bound_actor_part (
                                          node, stream, &session_rid, "queue-backpressure",
                                          &over_limit, ZLINK_DONTWAIT, ZLINK_PART_FINAL));

    void *cleanup_actor = zlink_spot_node_actor_new (node, "queue-cleanup");
    TEST_ASSERT_NOT_NULL (cleanup_actor);
    zlink_actor_ref_t cleanup_ref;
    TEST_ASSERT_EQUAL (ZLINK_CONFIG_OK, zlink_actor_get_ref (cleanup_actor, &cleanup_ref));
    TEST_ASSERT_EQUAL (ZLINK_REQUEST_OK,
                       wait_stream_bind_actor (node, stream, &session_rid, &cleanup_ref, 1000));
    zlink_msg_t incomplete;
    init_text_msg (&incomplete, "incomplete");
    TEST_ASSERT_EQUAL (ZLINK_SUBMIT_OK, test_stream_send_bound_actor_part (
                                          node, stream, &session_rid, "queue-cleanup", &incomplete,
                                          ZLINK_DONTWAIT, ZLINK_PART_MORE));
    zlink_msg_t complete_cleanup;
    init_text_msg (&complete_cleanup, "complete");
    TEST_ASSERT_EQUAL (ZLINK_SUBMIT_OK, test_stream_send_bound_actor_part (
                                          node, stream, &session_rid, "queue-cleanup",
                                          &complete_cleanup, ZLINK_DONTWAIT, ZLINK_PART_FINAL));
    TEST_ASSERT_EQUAL (ZLINK_REQUEST_OK, zlink_actor_destroy (&cleanup_actor, 0));

    TEST_ASSERT_EQUAL (ZLINK_CONFIG_OK, zlink_actor_leave_spot (actor, spot));
    TEST_ASSERT_EQUAL (ZLINK_REQUEST_OK, zlink_actor_destroy (&actor, 0));
    TEST_ASSERT_EQUAL (ZLINK_REQUEST_OK, zlink_actor_destroy (&backpressure_actor, 0));
    TEST_ASSERT_EQUAL (ZLINK_CLOSE_OK, zlink_close (stream));
    TEST_ASSERT_EQUAL (ZLINK_CLOSE_OK, zlink_spot_destroy (&spot));
    TEST_ASSERT_EQUAL (ZLINK_CLOSE_OK, zlink_spot_node_destroy (&node));
    TEST_ASSERT_EQUAL (ZLINK_CLOSE_OK, zlink_ctx_term (ctx));
}

void test_actor_route_move_joined_publish_and_provider_cleanup ()
{
    void *ctx = zlink_ctx_new ();
    TEST_ASSERT_NOT_NULL (ctx);

    void *discovery_a = zlink_discovery_new (ctx, ZLINK_AUTO_CONNECT_SPOT_MESH, "route-move-a");
    void *discovery_b = zlink_discovery_new (ctx, ZLINK_AUTO_CONNECT_SPOT_MESH, "route-move-b");
    TEST_ASSERT_NOT_NULL (discovery_a);
    TEST_ASSERT_NOT_NULL (discovery_b);
    int enabled = 1;
    TEST_ASSERT_EQUAL (ZLINK_CONFIG_OK,
                       zlink_set_option (discovery_a, ZLINK_OPT_DISCOVERY_ACTOR_ROUTE_SYNC,
                                         &enabled, sizeof (enabled)));
    TEST_ASSERT_EQUAL (ZLINK_CONFIG_OK,
                       zlink_set_option (discovery_b, ZLINK_OPT_DISCOVERY_ACTOR_ROUTE_SYNC,
                                         &enabled, sizeof (enabled)));

    zlink_spot_node_options_t options;
    options.mode = ZLINK_SPOT_NODE_MODE_ALL;
    void *node_a = zlink_spot_node_new (ctx, &options);
    void *node_b = zlink_spot_node_new (ctx, &options);
    TEST_ASSERT_NOT_NULL (node_a);
    TEST_ASSERT_NOT_NULL (node_b);
    TEST_ASSERT_EQUAL (ZLINK_CONFIG_OK, zlink_spot_node_attach_discovery (node_a, discovery_a));
    TEST_ASSERT_EQUAL (ZLINK_CONFIG_OK, zlink_spot_node_attach_discovery (node_b, discovery_b));

    void *actor_a = zlink_spot_node_actor_new (node_a, "route-move");
    void *actor_b = zlink_spot_node_actor_new (node_b, "route-move");
    TEST_ASSERT_NOT_NULL (actor_a);
    TEST_ASSERT_NOT_NULL (actor_b);
    void *spot_b = zlink_spot_new (node_b);
    TEST_ASSERT_NOT_NULL (spot_b);

    actor_probe_t probe;
    TEST_ASSERT_EQUAL (ZLINK_HANDLER_OK,
                       zlink_spot_dispatch_event_handler (spot_b, on_dispatch, &probe));

    void *stream = zlink_socket (ctx, ZLINK_SOCKET_STREAM);
    TEST_ASSERT_NOT_NULL (stream);
    zlink_routing_id_t session_a;
    zlink_routing_id_t session_b;
    set_rid (&session_a, "route-move-a");
    set_rid (&session_b, "route-move-b");
    zlink_actor_ref_t ref_a;
    zlink_actor_ref_t ref_b;
    TEST_ASSERT_EQUAL (ZLINK_CONFIG_OK, zlink_actor_get_ref (actor_a, &ref_a));
    TEST_ASSERT_EQUAL (ZLINK_CONFIG_OK, zlink_actor_get_ref (actor_b, &ref_b));

    zlink_actor_route_t route;
    TEST_ASSERT_EQUAL (ZLINK_CONFIG_OK,
                       zlink_discovery_resolve_actor (discovery_a, "route-move", &route));
    TEST_ASSERT_TRUE (route.current_spot_kind == ZLINK_SPOT_KIND_ENTRY
                      || route.current_spot_kind == ZLINK_SPOT_KIND_USER);
    TEST_ASSERT_TRUE (route.current_spot_rid.size > 0);

    TEST_ASSERT_EQUAL (ZLINK_REQUEST_OK,
                       wait_stream_bind_actor (node_a, stream, &session_b, &ref_b, 1000));
    zlink_msg_t join_msg;
    init_text_msg (&join_msg, "join-after-bind");
    TEST_ASSERT_EQUAL (ZLINK_SUBMIT_OK,
                       zlink_actor_join_spot (actor_b, spot_b, &join_msg, on_join_reply, &probe,
                                              ZLINK_DONTWAIT, 1000));
    {
        std::unique_lock<std::mutex> lock (probe.mutex);
        TEST_ASSERT_TRUE (
          probe.cv.wait_for (lock, std::chrono::seconds (2), [&] { return probe.join_done; }));
        TEST_ASSERT_EQUAL (ZLINK_REQUEST_OK, probe.join_result);
    }

    TEST_ASSERT_EQUAL (ZLINK_REQUEST_OK,
                       wait_stream_bind_actor (node_a, stream, &session_a, &ref_a, 1000));
    TEST_ASSERT_EQUAL (ZLINK_CONFIG_OK,
                       zlink_discovery_resolve_actor (discovery_a, "route-move", &route));
    TEST_ASSERT_EQUAL_UINT64 (ref_a.generation, route.actor.generation);
    TEST_ASSERT_TRUE (route.current_spot_kind == ZLINK_SPOT_KIND_ENTRY
                      || route.current_spot_kind == ZLINK_SPOT_KIND_USER);
    TEST_ASSERT_TRUE (route.current_spot_rid.size > 0);

    TEST_ASSERT_EQUAL (ZLINK_REQUEST_OK,
                       wait_stream_bind_actor (node_a, stream, &session_b, &ref_b, 1000));
    TEST_ASSERT_EQUAL (ZLINK_CONFIG_OK,
                       zlink_discovery_resolve_actor (discovery_a, "route-move", &route));
    TEST_ASSERT_EQUAL_UINT64 (ref_b.generation, route.actor.generation);
    zlink_routing_id_t spot_b_rid;
    memset (&spot_b_rid, 0, sizeof (spot_b_rid));
    TEST_ASSERT_EQUAL (ZLINK_CONFIG_OK, zlink_get_routing_id (spot_b, &spot_b_rid));
    TEST_ASSERT_EQUAL (ZLINK_SPOT_KIND_USER, route.current_spot_kind);
    TEST_ASSERT_EQUAL_UINT8 (spot_b_rid.size, route.current_spot_rid.size);
    TEST_ASSERT_EQUAL_MEMORY (spot_b_rid.data, route.current_spot_rid.data, spot_b_rid.size);

    TEST_ASSERT_EQUAL (ZLINK_REQUEST_OK, zlink_actor_destroy (&actor_a, 0));
    TEST_ASSERT_EQUAL (ZLINK_CONFIG_OK,
                       zlink_discovery_resolve_actor (discovery_a, "route-move", &route));
    TEST_ASSERT_EQUAL_UINT64 (ref_b.generation, route.actor.generation);

    TEST_ASSERT_EQUAL (ZLINK_CONFIG_OK, zlink_actor_leave_spot (actor_b, spot_b));
    TEST_ASSERT_EQUAL (ZLINK_CONFIG_OK,
                       zlink_discovery_resolve_actor (discovery_a, "route-move", &route));
    TEST_ASSERT_EQUAL (ZLINK_SPOT_KIND_ENTRY, route.current_spot_kind);
    TEST_ASSERT_TRUE (route.current_spot_rid.size > 0);

    TEST_ASSERT_EQUAL (ZLINK_CLOSE_OK, zlink_spot_destroy (&spot_b));
    TEST_ASSERT_EQUAL (ZLINK_CLOSE_OK, zlink_spot_node_destroy (&node_b));
    TEST_ASSERT_EQUAL (ZLINK_CONFIG_NOT_FOUND,
                       zlink_discovery_resolve_actor (discovery_a, "route-move", &route));
    TEST_ASSERT_EQUAL (ENOENT, zlink_errno ());

    TEST_ASSERT_EQUAL (ZLINK_CLOSE_OK, zlink_close (stream));
    TEST_ASSERT_EQUAL (ZLINK_CLOSE_OK, zlink_spot_node_destroy (&node_a));
    TEST_ASSERT_EQUAL (ZLINK_CLOSE_OK, zlink_discovery_destroy (&discovery_b));
    TEST_ASSERT_EQUAL (ZLINK_CLOSE_OK, zlink_discovery_destroy (&discovery_a));
    TEST_ASSERT_EQUAL (ZLINK_CLOSE_OK, zlink_ctx_term (ctx));
}

void test_stream_attach_actor_gateway_contract ()
{
    void *ctx = zlink_ctx_new ();
    TEST_ASSERT_NOT_NULL (ctx);

    zlink_spot_node_options_t routed_options;
    routed_options.mode = ZLINK_SPOT_NODE_MODE_ROUTED;
    void *node_a = zlink_spot_node_new (ctx, &routed_options);
    void *node_b = zlink_spot_node_new (ctx, &routed_options);
    TEST_ASSERT_NOT_NULL (node_a);
    TEST_ASSERT_NOT_NULL (node_b);

    zlink_spot_node_options_t pubsub_options;
    pubsub_options.mode = ZLINK_SPOT_NODE_MODE_PUBSUB;
    void *pubsub_node = zlink_spot_node_new (ctx, &pubsub_options);
    TEST_ASSERT_NOT_NULL (pubsub_node);

    void *stream = zlink_socket (ctx, ZLINK_SOCKET_STREAM);
    TEST_ASSERT_NOT_NULL (stream);

    TEST_ASSERT_EQUAL (ZLINK_CONFIG_NOT_SUPPORTED,
                       zlink_stream_attach_actor_gateway (stream, pubsub_node));
    TEST_ASSERT_EQUAL (ENOTSUP, zlink_errno ());

    TEST_ASSERT_EQUAL (ZLINK_CONFIG_OK, zlink_stream_attach_actor_gateway (stream, node_a));
    TEST_ASSERT_EQUAL (ZLINK_CONFIG_OK, zlink_stream_attach_actor_gateway (stream, node_a));
    TEST_ASSERT_EQUAL (ZLINK_CONFIG_INVALID_STATE,
                       zlink_stream_attach_actor_gateway (stream, node_b));
    TEST_ASSERT_EQUAL (EBUSY, zlink_errno ());

    TEST_ASSERT_EQUAL (ZLINK_CLOSE_OK, zlink_close (stream));
    TEST_ASSERT_EQUAL (ZLINK_CLOSE_OK, zlink_spot_node_destroy (&pubsub_node));
    TEST_ASSERT_EQUAL (ZLINK_CLOSE_OK, zlink_spot_node_destroy (&node_b));
    TEST_ASSERT_EQUAL (ZLINK_CLOSE_OK, zlink_spot_node_destroy (&node_a));
    TEST_ASSERT_EQUAL (ZLINK_CLOSE_OK, zlink_ctx_term (ctx));
}

void test_stream_bind_actor_requires_attached_gateway ()
{
    void *ctx = zlink_ctx_new ();
    TEST_ASSERT_NOT_NULL (ctx);
    zlink_spot_node_options_t options;
    options.mode = ZLINK_SPOT_NODE_MODE_ROUTED;
    void *node = zlink_spot_node_new (ctx, &options);
    TEST_ASSERT_NOT_NULL (node);
    void *actor = zlink_spot_node_actor_new (node, "attach-required");
    TEST_ASSERT_NOT_NULL (actor);

    void *stream = zlink_socket (ctx, ZLINK_SOCKET_STREAM);
    TEST_ASSERT_NOT_NULL (stream);
    zlink_routing_id_t session_rid;
    set_rid (&session_rid, "missing-attach");
    zlink_actor_ref_t ref;
    TEST_ASSERT_EQUAL (ZLINK_CONFIG_OK, zlink_actor_get_ref (actor, &ref));

    request_wait_t wait;
    TEST_ASSERT_EQUAL (
      ZLINK_SUBMIT_OK,
      zlink_stream_bind_actor (stream, &session_rid, &ref, request_wait_handler, &wait, 1000));
    TEST_ASSERT_EQUAL (ZLINK_REQUEST_INVALID_STATE, wait_request_result (&wait, 1000));

    TEST_ASSERT_EQUAL (ZLINK_CONFIG_OK, zlink_stream_attach_actor_gateway (stream, node));
    TEST_ASSERT_EQUAL (ZLINK_REQUEST_OK,
                       wait_stream_bind_actor (node, stream, &session_rid, &ref, 1000));

    TEST_ASSERT_EQUAL (ZLINK_REQUEST_OK, zlink_actor_destroy (&actor, 1000));
    TEST_ASSERT_EQUAL (ZLINK_CLOSE_OK, zlink_close (stream));
    TEST_ASSERT_EQUAL (ZLINK_CLOSE_OK, zlink_spot_node_destroy (&node));
    TEST_ASSERT_EQUAL (ZLINK_CLOSE_OK, zlink_ctx_term (ctx));
}


int main ()
{
    setup_test_environment ();
    UNITY_BEGIN ();
    RUN_TEST (test_entry_spot_facade_lookup_and_rid);
    RUN_TEST (test_spot_lookup_refcount_and_rid_index);
    RUN_TEST (test_spot_get_or_new_creates_and_reuses_logical_spot);
    RUN_TEST (test_spot_get_or_new_rejects_invalid_args_and_clears_outputs);
    RUN_TEST (test_spot_get_or_new_concurrent_callers_create_once);
    RUN_TEST (test_stream_attach_actor_gateway_contract);
    RUN_TEST (test_stream_bind_actor_requires_attached_gateway);
    RUN_TEST (test_entry_spot_dispatch_receives_bound_actor_message);
    RUN_TEST (test_actor_join_bind_relay_and_dispatch_recv);
    RUN_TEST (test_actor_join_entry_spot_moves_user_spot_actor_to_entry);
    RUN_TEST (test_actor_join_entry_spot_reject_keeps_actor_in_user_spot);
    RUN_TEST (test_actor_join_entry_spot_is_idempotent_without_lifecycle);
    RUN_TEST (test_actor_join_entry_spot_moves_actor_to_remote_node_entry);
    RUN_TEST (test_actor_join_entry_spot_error_cases);
    RUN_TEST (test_actor_join_entry_spot_timeout_releases_request);
    RUN_TEST (test_actor_join_timeout_without_dispatch_handler);
    RUN_TEST (test_actor_send_bound_session_raw_and_packet);
    RUN_TEST (test_spot_snapshot_destroy_joined_and_pending_counts);
    RUN_TEST (test_stream_close_aborts_pending_join_without_leaving_current_spot);
    RUN_TEST (test_stream_remote_actor_owner_missing_route_and_unbind_cleanup);
    RUN_TEST (test_stream_bind_external_remote_actor_ref_keeps_logical_binding);
    RUN_TEST (test_stream_bind_same_process_remote_actor_ref_keeps_logical_binding);
    RUN_TEST (test_stream_remote_actor_gateway_routes_same_process_remote_ref);
    RUN_TEST (test_stream_remote_actor_gateway_first_send_does_not_fake_success);
    RUN_TEST (test_stream_multipart_selector_and_unbound_relay);
    RUN_TEST (test_actor_queue_dispatch_receive_and_backpressure);
    RUN_TEST (test_actor_route_move_joined_publish_and_provider_cleanup);
    return UNITY_END ();
}
