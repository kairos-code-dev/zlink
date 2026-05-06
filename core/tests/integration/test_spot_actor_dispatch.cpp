/* SPDX-License-Identifier: MPL-2.0 */

#include "testutil.hpp"
#include "testutil_unity.hpp"

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
extern "C" int zlink_actor_pending_target_enqueue_for_testing (
  const zlink_actor_ref_t *target_ref_, const char *payload_);
extern "C" int zlink_actor_queue_size_for_testing (
  const zlink_actor_ref_t *actor_ref_, size_t *size_out_);

struct test_actor_handle_t
{
    void *node;
    zlink_actor_ref_t ref;
};

std::set<test_actor_handle_t *> g_test_actor_handles;
std::map<std::string, void *> g_test_actor_nodes_by_ref;

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

zlink_request_result_t test_actor_destroy (void **actor_p_,
                                           uint32_t timeout_ms_)
{
    if (!actor_p_ || !*actor_p_) {
        errno = EFAULT;
        return ZLINK_REQUEST_INVALID_ARGUMENT;
    }
    test_actor_handle_t *handle =
      static_cast<test_actor_handle_t *> (*actor_p_);
    if (g_test_actor_handles.count (handle) == 0) {
        errno = EBUSY;
        return ZLINK_REQUEST_BUSY;
    }
    const zlink_request_result_t rc =
      zlink_spot_node_actor_destroy (handle->node, &handle->ref, timeout_ms_);
    if (rc == ZLINK_REQUEST_OK) {
        g_test_actor_nodes_by_ref.erase (test_actor_ref_key (handle->ref));
        g_test_actor_handles.erase (handle);
        delete handle;
        *actor_p_ = NULL;
    }
    return rc;
}

zlink_submit_result_t test_actor_join_spot (
  void *actor_,
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
    return zlink_spot_node_actor_join_spot (
      handle->node, &handle->ref, &handle->ref.node_rid, &spot_rid, message_,
      handler_, userdata_, flags_, timeout_ms_);
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
      zlink_spot_node_actor_leave_spot (handle->node, &handle->ref, &spot_rid,
                                        0);
    return rc == ZLINK_REQUEST_OK ? ZLINK_CONFIG_OK
                                  : ZLINK_CONFIG_INVALID_STATE;
}

zlink_recv_result_t test_actor_recv_part (
  void *subject_,
  zlink_actor_recv_info_t *info_out_,
  zlink_msg_t *part_out_,
  zlink_part_flag_t *has_more_out_,
  zlink_recv_flags_t flags_)
{
    test_actor_handle_t *handle = static_cast<test_actor_handle_t *> (subject_);
    if (g_test_actor_handles.count (handle) != 0) {
        return zlink_spot_node_actor_recv_part (
          handle->node, &handle->ref, info_out_, part_out_, has_more_out_,
          flags_);
    }
    const zlink_actor_ref_t *ref =
      static_cast<const zlink_actor_ref_t *> (subject_);
    std::map<std::string, void *>::const_iterator it =
      g_test_actor_nodes_by_ref.find (test_actor_ref_key (*ref));
    if (it == g_test_actor_nodes_by_ref.end ()) {
        errno = EFAULT;
        return ZLINK_RECV_INVALID_HANDLE;
    }
    return zlink_spot_node_actor_recv_part (it->second, ref, info_out_,
                                            part_out_, has_more_out_, flags_);
}

zlink_submit_result_t test_actor_send_bound_session_msg (
  void *actor_, zlink_msg_t *message_, zlink_send_flags_t flags_)
{
    test_actor_handle_t *handle = static_cast<test_actor_handle_t *> (actor_);
    if (!handle || g_test_actor_handles.count (handle) == 0)
        return ZLINK_SUBMIT_INVALID_ARGUMENT;
    return zlink_spot_node_actor_send_bound_session_msg (
      handle->node, &handle->ref, message_, flags_);
}

zlink_submit_result_t test_actor_send_bound_session_packet (
  void *actor_,
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
    if (zlink_msg_init_size (&frame, 6u + header_size + body_size)
        != ZLINK_CONFIG_OK)
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

    const zlink_submit_result_t rc =
      test_actor_send_bound_session_msg (actor_, &frame, flags_);
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

#define zlink_spot_node_actor_new(node_, actor_id_)                            \
    test_actor_new (node_, actor_id_)
#define zlink_actor_get_ref(actor_, out_) test_actor_get_ref (actor_, out_)
#define zlink_actor_destroy(actor_p_, timeout_ms_)                             \
    test_actor_destroy (actor_p_, timeout_ms_)
#define zlink_actor_join_spot(actor_, spot_, message_, handler_, userdata_,    \
                              flags_, timeout_ms_)                            \
    test_actor_join_spot (actor_, spot_, message_, handler_, userdata_, flags_, \
                          timeout_ms_)
#define zlink_actor_leave_spot(actor_, spot_)                                  \
    test_actor_leave_spot (actor_, spot_)
#define zlink_actor_recv_part(subject_, info_out_, part_out_, has_more_out_,   \
                              flags_)                                         \
    test_actor_recv_part (subject_, info_out_, part_out_, has_more_out_, flags_)
#define zlink_actor_send_bound_session_msg(actor_, message_, flags_)           \
    test_actor_send_bound_session_msg (actor_, message_, flags_)
#define zlink_actor_send_bound_session_packet(actor_, header_, body_, flags_)  \
    test_actor_send_bound_session_packet (actor_, header_, body_, flags_)
#define zlink_spot_node_destroy_remote_actor(node_, actor_, timeout_ms_)       \
    zlink_spot_node_actor_destroy (node_, actor_, timeout_ms_)
#define zlink_spot_node_actor_join_spot(node_, actor_, dest_spot_rid_,         \
                                        message_, handler_, userdata_, flags_, \
                                        timeout_ms_)                           \
    zlink_spot_node_actor_join_spot (node_, actor_, &(actor_)->node_rid,       \
                                     dest_spot_rid_, message_, handler_,       \
                                     userdata_, flags_, timeout_ms_)

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
    return std::string (static_cast<const char *> (zlink_msg_data (msg_)),
                        zlink_msg_size (msg_));
}

void assert_rid_text (const zlink_routing_id_t &rid_, const char *text_)
{
    TEST_ASSERT_EQUAL_UINT8 (strlen (text_), rid_.size);
    TEST_ASSERT_EQUAL_MEMORY (text_, rid_.data, rid_.size);
}

void assert_same_rid (const zlink_routing_id_t &lhs_,
                      const zlink_routing_id_t &rhs_)
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
    TEST_ASSERT_EQUAL (ZLINK_CONFIG_OK,
                       zlink_spot_node_entry_spot (node, &entry_a));
    TEST_ASSERT_EQUAL (ZLINK_CONFIG_OK,
                       zlink_spot_node_entry_spot (node, &entry_b));
    TEST_ASSERT_NOT_NULL (entry_a);
    TEST_ASSERT_NOT_NULL (entry_b);
    TEST_ASSERT_NOT_EQUAL (entry_a, entry_b);

    zlink_routing_id_t entry_a_rid;
    zlink_routing_id_t entry_b_rid;
    TEST_ASSERT_EQUAL (ZLINK_CONFIG_OK,
                       zlink_get_routing_id (entry_a, &entry_a_rid));
    TEST_ASSERT_EQUAL (ZLINK_CONFIG_OK,
                       zlink_get_routing_id (entry_b, &entry_b_rid));
    assert_same_rid (entry_a_rid, entry_b_rid);

    TEST_ASSERT_EQUAL (ZLINK_CONFIG_OK,
                       zlink_set_routing_id (entry_a, "entry-main", 10));
    TEST_ASSERT_EQUAL (ZLINK_CONFIG_OK,
                       zlink_get_routing_id (entry_b, &entry_b_rid));
    assert_rid_text (entry_b_rid, "entry-main");

    void *entry_lookup = NULL;
    TEST_ASSERT_EQUAL (ZLINK_CONFIG_OK,
                       zlink_spot_node_spot_lookup (node, &entry_b_rid,
                                                    &entry_lookup));
    TEST_ASSERT_NOT_NULL (entry_lookup);
    zlink_routing_id_t lookup_rid;
    TEST_ASSERT_EQUAL (ZLINK_CONFIG_OK,
                       zlink_get_routing_id (entry_lookup, &lookup_rid));
    assert_rid_text (lookup_rid, "entry-main");

    size_t count = 0;
    TEST_ASSERT_EQUAL (ZLINK_CONFIG_OK,
                       zlink_spot_node_spots_snapshot (node, NULL, &count));
    TEST_ASSERT_TRUE (count >= 1);

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
    TEST_ASSERT_EQUAL (ZLINK_CONFIG_OK,
                       zlink_spot_node_spot_lookup (node, &old_rid, &lookup));
    TEST_ASSERT_NOT_NULL (lookup);
    TEST_ASSERT_EQUAL (ZLINK_CLOSE_OK, zlink_spot_destroy (&spot));
    TEST_ASSERT_NULL (spot);

    zlink_routing_id_t lookup_rid;
    TEST_ASSERT_EQUAL (ZLINK_CONFIG_OK,
                       zlink_get_routing_id (lookup, &lookup_rid));
    assert_same_rid (old_rid, lookup_rid);

    TEST_ASSERT_EQUAL (ZLINK_CONFIG_OK,
                       zlink_set_routing_id (lookup, "room-a", 6));
    void *unchanged = reinterpret_cast<void *> (0x1);
    TEST_ASSERT_EQUAL (ZLINK_CONFIG_NOT_FOUND,
                       zlink_spot_node_spot_lookup (node, &old_rid,
                                                    &unchanged));
    TEST_ASSERT_EQUAL (reinterpret_cast<void *> (0x1), unchanged);

    TEST_ASSERT_EQUAL (ZLINK_CONFIG_OK,
                       zlink_get_routing_id (lookup, &lookup_rid));
    assert_rid_text (lookup_rid, "room-a");
    void *new_lookup = NULL;
    TEST_ASSERT_EQUAL (ZLINK_CONFIG_OK,
                       zlink_spot_node_spot_lookup (node, &lookup_rid,
                                                    &new_lookup));
    TEST_ASSERT_NOT_NULL (new_lookup);

    void *other = zlink_spot_new (node);
    TEST_ASSERT_NOT_NULL (other);
    TEST_ASSERT_NOT_EQUAL (ZLINK_CONFIG_OK,
                           zlink_set_routing_id (other, "room-a", 6));

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
            || !reserve_loopback_tcp_endpoint (router_endpoint_,
                                               router_endpoint_size_)
            || strcmp (pub_endpoint_, router_endpoint_) == 0)
            continue;
        if (zlink_registry_bind (registry_, pub_endpoint_, router_endpoint_)
            == ZLINK_BIND_OK)
            return true;
    }
    return false;
}

bool bind_spot_endpoint (void *node_, char *endpoint_, size_t endpoint_size_)
{
    for (int i = 0; i < 128; ++i) {
        if (!reserve_loopback_tcp_endpoint (endpoint_, endpoint_size_))
            continue;
        if (zlink_spot_node_bind (node_, endpoint_) == ZLINK_BIND_OK)
            return true;
    }
    return false;
}

bool connect_registry_with_retry (void *discovery_,
                                  const char *router_endpoint_,
                                  int timeout_ms_)
{
    return zlink_test_wait_until (timeout_ms_, [=] {
        if (zlink_discovery_connect_registry (discovery_, router_endpoint_)
            == ZLINK_CONNECT_OK)
            return true;
        return false;
    });
}

bool rid_equals (const zlink_routing_id_t &lhs_,
                 const zlink_routing_id_t &rhs_)
{
    return lhs_.size == rhs_.size
           && memcmp (lhs_.data, rhs_.data, lhs_.size) == 0;
}

zlink_routing_id_t find_spot_rid_not_in (
  void *node_, const std::vector<zlink_routing_id_t> &excluded_)
{
    size_t count = 0;
    TEST_ASSERT_EQUAL (ZLINK_CONFIG_OK,
                       zlink_spot_node_spots_snapshot (node_, NULL, &count));
    std::vector<zlink_spot_node_spot_entry_t> rows (count);
    TEST_ASSERT_EQUAL (ZLINK_CONFIG_OK,
                       zlink_spot_node_spots_snapshot (node_, rows.data (),
                                                       &count));
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
    TEST_ASSERT_EQUAL (ZLINK_CONFIG_OK,
                       zlink_spot_node_spots_snapshot (node_, NULL, &count));
    std::vector<zlink_spot_node_spot_entry_t> rows (count);
    TEST_ASSERT_EQUAL (ZLINK_CONFIG_OK,
                       zlink_spot_node_spots_snapshot (node_, rows.data (),
                                                       &count));
    for (size_t i = 0; i < count; ++i) {
        if (rid_equals (rows[i].spot_rid, spot_rid_)) {
            if (row_out_)
                *row_out_ = rows[i];
            return true;
        }
    }
    return false;
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

void on_dispatch (void *,
                  const zlink_spot_dispatch_info_t *info_,
                  void *userdata_)
{
    actor_probe_t *probe = static_cast<actor_probe_t *> (userdata_);
    if (!info_)
        return;

    if (info_->event == ZLINK_SPOT_DISPATCH_EVENT_ACTOR_JOIN_READABLE) {
        zlink_actor_join_info_t join_info;
        zlink_msg_t message;
        zlink_recv_result_t recv_rc =
          zlink_spot_actor_join_recv (info_->subject, &join_info, &message,
                                      ZLINK_RECV_FLAGS_DONTWAIT);
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
            probe->join_payload = msg_text (&message);
        }
        zlink_msg_close (&message);

        zlink_msg_t reply;
        zlink_msg_init (&reply);
        zlink_submit_result_t reply_rc = zlink_spot_actor_join_reply (
          info_->subject, &join_info, probe->accept_join ? 1u : 0u, &reply);
        if (reply_rc != ZLINK_SUBMIT_OK) {
            zlink_msg_close (&reply);
            std::lock_guard<std::mutex> lock (probe->mutex);
            probe->failed = true;
            probe->last_errno = zlink_errno ();
                probe->cv.notify_all ();
        }
        zlink_msg_t late_reply;
        zlink_msg_init (&late_reply);
        zlink_submit_result_t late_rc = zlink_spot_actor_join_reply (
          info_->subject, &join_info, 1, &late_reply);
        {
            std::lock_guard<std::mutex> lock (probe->mutex);
            probe->double_reply_checked =
              late_rc == ZLINK_SUBMIT_INVALID_STATE
              || late_rc == ZLINK_SUBMIT_INVALID_ARGUMENT;
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
            zlink_recv_result_t recv_rc =
              zlink_actor_recv_part (info_->subject, &recv_info, &part, &more,
                                     ZLINK_RECV_FLAGS_DONTWAIT);
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
        probe->actor_subject_ok =
          info_->subject_kind == ZLINK_SPOT_DISPATCH_SUBJECT_ACTOR;
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
            zlink_request_result_t destroy_rc =
              zlink_actor_destroy (&actor_to_destroy, 0);
            probe->destroy_in_actor_callback_blocked =
              destroy_rc == ZLINK_REQUEST_BUSY
              && actor_to_destroy == info_->subject;
            if (!probe->destroy_in_actor_callback_blocked) {
                probe->failed = true;
                probe->last_errno = zlink_errno ();
            }
        }
        probe->cv.notify_all ();
    }
}

void on_join_only_dispatch (void *,
                            const zlink_spot_dispatch_info_t *info_,
                            void *userdata_)
{
    actor_probe_t *probe = static_cast<actor_probe_t *> (userdata_);
    if (!info_ || info_->event != ZLINK_SPOT_DISPATCH_EVENT_ACTOR_JOIN_READABLE)
        return;

    zlink_actor_join_info_t join_info;
    zlink_msg_t message;
    zlink_recv_result_t recv_rc =
      zlink_spot_actor_join_recv (info_->subject, &join_info, &message,
                                  ZLINK_RECV_FLAGS_DONTWAIT);
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
        probe->join_payload = msg_text (&message);
    }
    zlink_msg_close (&message);

    zlink_msg_t reply;
    zlink_msg_init (&reply);
    zlink_submit_result_t reply_rc = zlink_spot_actor_join_reply (
      info_->subject, &join_info, probe->accept_join ? 1u : 0u, &reply);
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

zlink_actor_admission_result_t on_actor_admission (void *node_,
                                                   const char *,
                                                   const zlink_msg_t *,
                                                   void *userdata_)
{
    admission_probe_t *probe = static_cast<admission_probe_t *> (userdata_);
    ++probe->calls;
    {
        std::lock_guard<std::mutex> lock (probe->mutex);
        probe->entered = true;
        probe->cv.notify_all ();
    }
    if (probe->try_reentrant_create) {
        void *nested = zlink_spot_node_actor_new (node_, "nested-create");
        probe->reentrant_create_blocked = nested == NULL && zlink_errno () == EFSM;
    }
    if (probe->sleep_ms > 0)
        msleep (probe->sleep_ms);
    return probe->accept ? ZLINK_ACTOR_ADMISSION_ACCEPT
                         : ZLINK_ACTOR_ADMISSION_REJECT;
}

struct stream_session_probe_t
{
    stream_session_probe_t () : ready (false)
    {
        memset (&rid, 0, sizeof (rid));
    }

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
    stream_session_probe_t *probe =
      static_cast<stream_session_probe_t *> (userdata_);
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
           && sscanf (endpoint_, "%7[^:]://%63[^:]:%d", proto, host_, port_)
                == 3
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
        || connect (fd, reinterpret_cast<const struct sockaddr *> (&addr),
                    sizeof (addr))
             != 0) {
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
               && setsockopt (fd_, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof (tv))
                    == 0
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
                       zlink_actor_send_bound_session_packet (
                         actor, &unbound_header, &unbound_body,
                         ZLINK_DONTWAIT));
    TEST_ASSERT_EQUAL_STRING ("unbound-h", msg_text (&unbound_header).c_str ());
    TEST_ASSERT_EQUAL_STRING ("unbound-b", msg_text (&unbound_body).c_str ());
    TEST_ASSERT_EQUAL (ZLINK_CONFIG_OK, zlink_msg_close (&unbound_header));
    TEST_ASSERT_EQUAL (ZLINK_CONFIG_OK, zlink_msg_close (&unbound_body));

    void *stream = zlink_socket (ctx, ZLINK_SOCKET_STREAM);
    TEST_ASSERT_NOT_NULL (stream);
    const int zero = 0;
    TEST_ASSERT_EQUAL (ZLINK_CONFIG_OK,
                       zlink_set_option (stream, ZLINK_OPT_LINGER, &zero,
                                         sizeof (zero)));

    char endpoint[MAX_SOCKET_STRING];
    bind_loopback_ipv4 (stream, endpoint, sizeof (endpoint));
    stream_session_probe_t probe;
    TEST_ASSERT_EQUAL (ZLINK_HANDLER_OK,
                       zlink_recv_handler (stream, on_stream_session_probe,
                                           &probe));

    const int client_fd = connect_raw_tcp (endpoint);
    TEST_ASSERT_TRUE (client_fd >= 0);
    TEST_ASSERT_EQUAL_INT (0, set_raw_timeout (client_fd, 3000));

    const char hello[] = "hello";
    TEST_ASSERT_EQUAL_INT (0, send_all (client_fd, hello, sizeof (hello) - 1));
    {
        std::unique_lock<std::mutex> lock (probe.mutex);
        TEST_ASSERT_TRUE (probe.cv.wait_for (
          lock, std::chrono::seconds (3), [&] { return probe.ready; }));
    }

    zlink_actor_ref_t ref;
    TEST_ASSERT_EQUAL (ZLINK_CONFIG_OK, zlink_actor_get_ref (actor, &ref));
    TEST_ASSERT_EQUAL (ZLINK_REQUEST_OK,
                       zlink_stream_bind_actor (node, stream, &probe.rid, &ref,
                                                1000));

    zlink_msg_t raw_reply;
    init_text_msg (&raw_reply, "actor-raw");
    TEST_ASSERT_EQUAL (ZLINK_SUBMIT_OK,
                       zlink_actor_send_bound_session_msg (actor, &raw_reply,
                                                          ZLINK_DONTWAIT));
    char raw_buf[16] = {0};
    TEST_ASSERT_EQUAL_INT (0, recv_exact (client_fd, raw_buf, 9));
    TEST_ASSERT_EQUAL_STRING_LEN ("actor-raw", raw_buf, 9);

    zlink_msg_t header;
    zlink_msg_t body;
    init_text_msg (&header, "hdr");
    init_text_msg (&body, "body");
    TEST_ASSERT_EQUAL (ZLINK_SUBMIT_OK,
                       zlink_actor_send_bound_session_packet (
                         actor, &header, &body, ZLINK_DONTWAIT));
    unsigned char packet_buf[13] = {0};
    TEST_ASSERT_EQUAL_INT (0, recv_exact (client_fd, packet_buf,
                                          sizeof (packet_buf)));
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
                       zlink_actor_send_bound_session_msg (actor, &late_msg,
                                                          ZLINK_DONTWAIT));
    TEST_ASSERT_EQUAL (ZLINK_CONFIG_OK, zlink_msg_close (&late_msg));

    close_raw_fd (client_fd);
    TEST_ASSERT_EQUAL (ZLINK_REQUEST_OK, zlink_actor_destroy (&actor, 0));
    TEST_ASSERT_EQUAL (ZLINK_CLOSE_OK, zlink_spot_node_destroy (&node));
    TEST_ASSERT_EQUAL (ZLINK_CLOSE_OK, zlink_ctx_term (ctx));
}
#endif
}

void test_actor_lifecycle_ref_and_lookup ()
{
    void *ctx = zlink_ctx_new ();
    TEST_ASSERT_NOT_NULL (ctx);

    zlink_spot_node_options_t options;
    options.mode = ZLINK_SPOT_NODE_MODE_ALL;
    void *node = zlink_spot_node_new (ctx, &options);
    TEST_ASSERT_NOT_NULL (node);
    void *other_node = zlink_spot_node_new (ctx, &options);
    TEST_ASSERT_NOT_NULL (other_node);

    void *entry = NULL;
    TEST_ASSERT_EQUAL (ZLINK_CONFIG_OK,
                       zlink_spot_node_entry_spot (node, &entry));
    TEST_ASSERT_NOT_NULL (entry);
    TEST_ASSERT_EQUAL (ZLINK_CONFIG_OK,
                       zlink_set_routing_id (entry, "entry-actor", 11));
    void *actor = zlink_spot_node_actor_new (node, "actor-a");
    TEST_ASSERT_NOT_NULL (actor);
    TEST_ASSERT_EQUAL (ZLINK_CONFIG_INVALID_STATE,
                       zlink_set_routing_id (entry, "entry-changed", 13));
    TEST_ASSERT_EQUAL (EBUSY, zlink_errno ());

    zlink_actor_ref_t ref;
    TEST_ASSERT_EQUAL (ZLINK_CONFIG_OK, zlink_actor_get_ref (actor, &ref));
    TEST_ASSERT_TRUE (ref.generation != 0);
    TEST_ASSERT_EQUAL_STRING ("actor-a", ref.actor_id);

    void *duplicate = zlink_spot_node_actor_new (node, "actor-a");
    TEST_ASSERT_NULL (duplicate);
    TEST_ASSERT_EQUAL (EBUSY, zlink_errno ());

    zlink_actor_ref_t looked_up;
    TEST_ASSERT_EQUAL (ZLINK_CONFIG_OK,
                       zlink_spot_node_actor_lookup (node, "actor-a",
                                                     &looked_up));
    TEST_ASSERT_EQUAL_UINT64 (ref.generation, looked_up.generation);

    zlink_actor_ref_t unchecked;
    TEST_ASSERT_EQUAL (ZLINK_CONFIG_OK,
                       zlink_remote_actor_get_ref (&ref.node_rid, "actor-a",
                                                   &unchecked));
    TEST_ASSERT_EQUAL_UINT64 (0, unchecked.generation);

    zlink_actor_recv_info_t non_owner_info;
    zlink_msg_t non_owner_part;
    zlink_part_flag_t non_owner_flag = ZLINK_PART_FINAL;
    TEST_ASSERT_EQUAL (ZLINK_CONFIG_OK, zlink_msg_init (&non_owner_part));
    TEST_ASSERT_EQUAL (ZLINK_RECV_INVALID_HANDLE,
                       zlink_spot_node_actor_recv_part (
                         other_node, &ref, &non_owner_info, &non_owner_part,
                         &non_owner_flag, ZLINK_RECV_FLAGS_DONTWAIT));
    TEST_ASSERT_EQUAL (ZLINK_CONFIG_OK, zlink_msg_close (&non_owner_part));

    zlink_spot_node_actor_entry_t actor_row;
    size_t actor_count = 1;
    TEST_ASSERT_EQUAL (ZLINK_CONFIG_OK,
                       zlink_spot_node_actors_snapshot (node, &actor_row,
                                                        &actor_count));
    TEST_ASSERT_EQUAL_UINT (1, actor_count);
    TEST_ASSERT_EQUAL_UINT32 (1, actor_row.joined);
    assert_rid_text (actor_row.joined_spot_rid, "entry-actor");

    zlink_actor_ref_t entry_rows[1];
    size_t entry_count = 1;
    TEST_ASSERT_EQUAL (ZLINK_CONFIG_OK,
                       zlink_spot_actors_snapshot (entry, entry_rows,
                                                   &entry_count));
    TEST_ASSERT_EQUAL_UINT (1, entry_count);
    TEST_ASSERT_EQUAL_UINT64 (ref.generation, entry_rows[0].generation);

    TEST_ASSERT_EQUAL (ZLINK_REQUEST_OK, zlink_actor_destroy (&actor, 0));
    TEST_ASSERT_NULL (actor);
    void *recreated = zlink_spot_node_actor_new (node, "actor-a");
    TEST_ASSERT_NOT_NULL (recreated);
    zlink_actor_ref_t recreated_ref;
    TEST_ASSERT_EQUAL (ZLINK_CONFIG_OK,
                       zlink_actor_get_ref (recreated, &recreated_ref));
    TEST_ASSERT_NOT_EQUAL (ref.generation, recreated_ref.generation);
    TEST_ASSERT_EQUAL (ZLINK_REQUEST_OK, zlink_actor_destroy (&recreated, 0));

    TEST_ASSERT_EQUAL (ZLINK_CLOSE_OK, zlink_spot_destroy (&entry));
    TEST_ASSERT_EQUAL (ZLINK_CLOSE_OK, zlink_spot_node_destroy (&other_node));
    TEST_ASSERT_EQUAL (ZLINK_CLOSE_OK, zlink_spot_node_destroy (&node));
    TEST_ASSERT_EQUAL (ZLINK_CLOSE_OK, zlink_ctx_term (ctx));
}

void test_entry_spot_dispatch_receives_bound_actor_message ()
{
    void *ctx = zlink_ctx_new ();
    TEST_ASSERT_NOT_NULL (ctx);
    void *node = zlink_spot_node_new (ctx, NULL);
    TEST_ASSERT_NOT_NULL (node);
    void *entry = NULL;
    TEST_ASSERT_EQUAL (ZLINK_CONFIG_OK,
                       zlink_spot_node_entry_spot (node, &entry));
    TEST_ASSERT_NOT_NULL (entry);

    actor_probe_t probe;
    TEST_ASSERT_EQUAL (ZLINK_HANDLER_OK,
                       zlink_spot_dispatch_event_handler (entry, on_dispatch,
                                                          &probe));
    void *actor = zlink_spot_node_actor_new (node, "entry-dispatch");
    TEST_ASSERT_NOT_NULL (actor);
    zlink_actor_ref_t ref;
    TEST_ASSERT_EQUAL (ZLINK_CONFIG_OK, zlink_actor_get_ref (actor, &ref));

    void *stream = zlink_socket (ctx, ZLINK_SOCKET_STREAM);
    TEST_ASSERT_NOT_NULL (stream);
    zlink_routing_id_t session_rid;
    set_rid (&session_rid, "entry-session");
    TEST_ASSERT_EQUAL (ZLINK_REQUEST_OK,
                       zlink_stream_bind_actor (node, stream, &session_rid,
                                                &ref, 1000));
    zlink_msg_t part;
    init_text_msg (&part, "entry-payload");
    TEST_ASSERT_EQUAL (ZLINK_SUBMIT_OK,
                       zlink_stream_send_bound_actor_part (
                         node, stream, &session_rid, "entry-dispatch", &part,
                         ZLINK_DONTWAIT, ZLINK_PART_FINAL));

    {
        std::unique_lock<std::mutex> lock (probe.mutex);
        TEST_ASSERT_TRUE (probe.cv.wait_for (
          lock, std::chrono::seconds (2),
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
                       zlink_stream_bind_actor (node, stream_marker,
                                                &session_rid, &ref, 1000));

    actor_probe_t probe;
    probe.try_destroy_in_actor_callback = true;
    TEST_ASSERT_EQUAL (ZLINK_HANDLER_OK,
                       zlink_spot_dispatch_event_handler (spot, on_dispatch,
                                                          &probe));

    zlink_msg_t join_msg;
    init_text_msg (&join_msg, "join");
    TEST_ASSERT_EQUAL (ZLINK_SUBMIT_OK,
                       zlink_actor_join_spot (actor, spot, &join_msg,
                                              on_join_reply, &probe,
                                              ZLINK_DONTWAIT, 1000));

    {
        std::unique_lock<std::mutex> lock (probe.mutex);
        TEST_ASSERT_TRUE (probe.cv.wait_for (
          lock, std::chrono::seconds (2),
          [&] { return probe.join_done || probe.failed; }));
        TEST_ASSERT_FALSE (probe.failed);
        TEST_ASSERT_EQUAL (ZLINK_REQUEST_OK, probe.join_result);
        TEST_ASSERT_TRUE (probe.double_reply_checked);
    }

    zlink_msg_t duplicate_join;
    actor_probe_t duplicate_probe;
    init_text_msg (&duplicate_join, "duplicate");
    TEST_ASSERT_EQUAL (ZLINK_SUBMIT_OK,
                       zlink_actor_join_spot (actor, spot, &duplicate_join,
                                              on_join_reply, &duplicate_probe,
                                              ZLINK_DONTWAIT, 1000));
    {
        std::unique_lock<std::mutex> lock (duplicate_probe.mutex);
        TEST_ASSERT_TRUE (duplicate_probe.cv.wait_for (
          lock, std::chrono::seconds (2),
          [&] { return duplicate_probe.join_done; }));
        TEST_ASSERT_EQUAL (ZLINK_REQUEST_OK, duplicate_probe.join_result);
    }

    zlink_actor_ref_t joined_rows[1];
    size_t joined_count = 1;
    TEST_ASSERT_EQUAL (ZLINK_CONFIG_OK,
                       zlink_spot_actors_snapshot (spot, joined_rows,
                                                   &joined_count));
    TEST_ASSERT_EQUAL_UINT (1, joined_count);
    TEST_ASSERT_EQUAL_UINT64 (ref.generation, joined_rows[0].generation);

    void *joined_actor_handle = actor;
    TEST_ASSERT_EQUAL (ZLINK_REQUEST_INVALID_STATE,
                       zlink_actor_destroy (&joined_actor_handle, 0));
    TEST_ASSERT_EQUAL_PTR (actor, joined_actor_handle);

    zlink_msg_t part;
    init_text_msg (&part, "payload");
    TEST_ASSERT_EQUAL (ZLINK_SUBMIT_OK,
                       zlink_stream_send_bound_actor_part (
                         node, stream_marker, &session_rid, "actor-b", &part,
                         ZLINK_DONTWAIT, ZLINK_PART_FINAL));

    {
        std::unique_lock<std::mutex> lock (probe.mutex);
        TEST_ASSERT_TRUE (probe.cv.wait_for (
          lock, std::chrono::seconds (2),
          [&] { return probe.actor_event || probe.failed; }));
        TEST_ASSERT_FALSE (probe.failed);
        TEST_ASSERT_EQUAL_STRING ("payload", probe.payload.c_str ());
    }

    TEST_ASSERT_EQUAL (ZLINK_CONFIG_OK, zlink_actor_leave_spot (actor, spot));
    void *entry = NULL;
    TEST_ASSERT_EQUAL (ZLINK_CONFIG_OK,
                       zlink_spot_node_entry_spot (node, &entry));
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
                       zlink_stream_bind_actor (node, stream, &session_rid,
                                                &ref, 1000));

    actor_probe_t probe;
    zlink_msg_t join_msg;
    init_text_msg (&join_msg, "join-timeout");
    TEST_ASSERT_EQUAL (ZLINK_SUBMIT_OK,
                       zlink_actor_join_spot (actor, spot, &join_msg,
                                              on_join_reply, &probe,
                                              ZLINK_DONTWAIT, 30));

    {
        std::unique_lock<std::mutex> lock (probe.mutex);
        TEST_ASSERT_TRUE (probe.cv.wait_for (
          lock, std::chrono::seconds (2), [&] { return probe.join_done; }));
        TEST_ASSERT_EQUAL (ZLINK_REQUEST_TIMED_OUT, probe.join_result);
    }

    zlink_actor_join_info_t info;
    zlink_msg_t message;
    TEST_ASSERT_EQUAL (ZLINK_CONFIG_OK, zlink_msg_init (&message));
    TEST_ASSERT_EQUAL (ZLINK_RECV_NO_DATA,
                       zlink_spot_actor_join_recv (
                         spot, &info, &message, ZLINK_RECV_FLAGS_DONTWAIT));
    TEST_ASSERT_EQUAL (ZLINK_CONFIG_OK, zlink_msg_close (&message));

    TEST_ASSERT_EQUAL (ZLINK_REQUEST_OK, zlink_actor_destroy (&actor, 0));
    void *no_timeout_actor =
      zlink_spot_node_actor_new (node, "actor-no-timeout");
    TEST_ASSERT_NOT_NULL (no_timeout_actor);
    zlink_actor_ref_t no_timeout_ref;
    TEST_ASSERT_EQUAL (ZLINK_CONFIG_OK,
                       zlink_actor_get_ref (no_timeout_actor,
                                            &no_timeout_ref));
    TEST_ASSERT_EQUAL (ZLINK_REQUEST_OK,
                       zlink_stream_bind_actor (node, stream, &session_rid,
                                                &no_timeout_ref, 1000));
    actor_probe_t no_timeout_probe;
    zlink_msg_t no_timeout_join;
    init_text_msg (&no_timeout_join, "join-no-timeout");
    TEST_ASSERT_EQUAL (ZLINK_SUBMIT_OK,
                       zlink_actor_join_spot (
                         no_timeout_actor, spot, &no_timeout_join,
                         on_join_reply, &no_timeout_probe, ZLINK_DONTWAIT,
                         0));
    {
        std::unique_lock<std::mutex> lock (no_timeout_probe.mutex);
        TEST_ASSERT_FALSE (no_timeout_probe.cv.wait_for (
          lock, std::chrono::milliseconds (80),
          [&] { return no_timeout_probe.join_done; }));
    }
    TEST_ASSERT_EQUAL (ZLINK_RECV_OK,
                       zlink_spot_actor_join_recv (
                         spot, &info, &message, ZLINK_RECV_FLAGS_DONTWAIT));
    TEST_ASSERT_EQUAL (ZLINK_CONFIG_OK, zlink_msg_close (&message));
    zlink_msg_t no_timeout_reply;
    TEST_ASSERT_EQUAL (ZLINK_CONFIG_OK, zlink_msg_init (&no_timeout_reply));
    TEST_ASSERT_EQUAL (ZLINK_SUBMIT_OK,
                       zlink_spot_actor_join_reply (spot, &info, 0u,
                                                    &no_timeout_reply));
    {
        std::unique_lock<std::mutex> lock (no_timeout_probe.mutex);
        TEST_ASSERT_TRUE (no_timeout_probe.cv.wait_for (
          lock, std::chrono::seconds (2),
          [&] { return no_timeout_probe.join_done; }));
        TEST_ASSERT_EQUAL (ZLINK_REQUEST_REJECTED,
                           no_timeout_probe.join_result);
    }
    TEST_ASSERT_EQUAL (ZLINK_REQUEST_OK,
                       zlink_actor_destroy (&no_timeout_actor, 0));
    TEST_ASSERT_EQUAL (ZLINK_CLOSE_OK, zlink_close (stream));
    TEST_ASSERT_EQUAL (ZLINK_CLOSE_OK, zlink_spot_destroy (&spot));
    TEST_ASSERT_EQUAL (ZLINK_CLOSE_OK, zlink_spot_node_destroy (&node));
    TEST_ASSERT_EQUAL (ZLINK_CLOSE_OK, zlink_ctx_term (ctx));
}

void test_actor_join_requires_session_and_leave_stale ()
{
    void *ctx = zlink_ctx_new ();
    TEST_ASSERT_NOT_NULL (ctx);

    zlink_spot_node_options_t options;
    options.mode = ZLINK_SPOT_NODE_MODE_ALL;
    void *node = zlink_spot_node_new (ctx, &options);
    TEST_ASSERT_NOT_NULL (node);
    void *spot = zlink_spot_new (node);
    TEST_ASSERT_NOT_NULL (spot);
    void *actor = zlink_spot_node_actor_new (node, "join-session-required");
    TEST_ASSERT_NOT_NULL (actor);

    actor_probe_t probe;
    zlink_msg_t no_session_join;
    init_text_msg (&no_session_join, "no-session");
    TEST_ASSERT_EQUAL (ZLINK_SUBMIT_INVALID_STATE,
                       zlink_actor_join_spot (actor, spot, &no_session_join,
                                              on_join_reply, &probe,
                                              ZLINK_DONTWAIT, 1000));
    TEST_ASSERT_EQUAL_STRING ("no-session",
                              msg_text (&no_session_join).c_str ());
    TEST_ASSERT_EQUAL (ZLINK_CONFIG_OK, zlink_msg_close (&no_session_join));

    zlink_actor_ref_t ref;
    TEST_ASSERT_EQUAL (ZLINK_CONFIG_OK, zlink_actor_get_ref (actor, &ref));
    void *stream = zlink_socket (ctx, ZLINK_SOCKET_STREAM);
    TEST_ASSERT_NOT_NULL (stream);
    zlink_routing_id_t session_rid;
    set_rid (&session_rid, "join-session-required-session");
    TEST_ASSERT_EQUAL (ZLINK_REQUEST_OK,
                       zlink_stream_bind_actor (node, stream, &session_rid,
                                                &ref, 1000));

    zlink_msg_t pending_join;
    init_text_msg (&pending_join, "pending");
    TEST_ASSERT_EQUAL (ZLINK_SUBMIT_OK,
                       zlink_actor_join_spot (actor, spot, &pending_join,
                                              on_join_reply, &probe,
                                              ZLINK_DONTWAIT, 5000));
    TEST_ASSERT_EQUAL (ZLINK_REQUEST_BUSY,
                       zlink_spot_node_actor_leave_spot (
                         node, &ref, &ref.node_rid, 0));
    void *destroy_candidate = actor;
    TEST_ASSERT_EQUAL (ZLINK_REQUEST_BUSY,
                       zlink_actor_destroy (&destroy_candidate, 0));
    TEST_ASSERT_EQUAL_PTR (actor, destroy_candidate);

    zlink_actor_join_info_t info;
    zlink_msg_t message;
    TEST_ASSERT_EQUAL (ZLINK_RECV_OK,
                       zlink_spot_actor_join_recv (
                         spot, &info, &message, ZLINK_RECV_FLAGS_DONTWAIT));
    TEST_ASSERT_EQUAL (ZLINK_CONFIG_OK, zlink_msg_close (&message));
    zlink_msg_t reply;
    TEST_ASSERT_EQUAL (ZLINK_CONFIG_OK, zlink_msg_init (&reply));
    TEST_ASSERT_EQUAL (ZLINK_SUBMIT_OK,
                       zlink_spot_actor_join_reply (spot, &info, 1u, &reply));
    {
        std::unique_lock<std::mutex> lock (probe.mutex);
        TEST_ASSERT_TRUE (probe.cv.wait_for (
          lock, std::chrono::seconds (2), [&] { return probe.join_done; }));
        TEST_ASSERT_EQUAL (ZLINK_REQUEST_OK, probe.join_result);
    }

    void *other_spot = zlink_spot_new (node);
    TEST_ASSERT_NOT_NULL (other_spot);
    zlink_routing_id_t other_rid;
    TEST_ASSERT_EQUAL (ZLINK_CONFIG_OK,
                       zlink_get_routing_id (other_spot, &other_rid));
    TEST_ASSERT_EQUAL (ZLINK_REQUEST_INVALID_STATE,
                       zlink_spot_node_actor_leave_spot (node, &ref,
                                                         &other_rid, 0));
    TEST_ASSERT_EQUAL (ZLINK_REQUEST_OK,
                       zlink_spot_node_actor_leave_spot (node, &ref,
                                                         &info.target_spot_rid,
                                                         0));

    TEST_ASSERT_EQUAL (ZLINK_REQUEST_OK, zlink_actor_destroy (&actor, 0));
    TEST_ASSERT_EQUAL (ZLINK_CLOSE_OK, zlink_spot_destroy (&other_spot));
    TEST_ASSERT_EQUAL (ZLINK_CLOSE_OK, zlink_close (stream));
    TEST_ASSERT_EQUAL (ZLINK_CLOSE_OK, zlink_spot_destroy (&spot));
    TEST_ASSERT_EQUAL (ZLINK_CLOSE_OK, zlink_spot_node_destroy (&node));
    TEST_ASSERT_EQUAL (ZLINK_CLOSE_OK, zlink_ctx_term (ctx));
}

void test_actor_remote_join_handoff_accept_reject_timeout ()
{
    void *ctx = zlink_ctx_new ();
    TEST_ASSERT_NOT_NULL (ctx);

    zlink_spot_node_options_t options;
    options.mode = ZLINK_SPOT_NODE_MODE_ALL;
    void *session_node = zlink_spot_node_new (ctx, &options);
    void *source_node = zlink_spot_node_new (ctx, &options);
    void *target_node = zlink_spot_node_new (ctx, &options);
    TEST_ASSERT_NOT_NULL (session_node);
    TEST_ASSERT_NOT_NULL (source_node);
    TEST_ASSERT_NOT_NULL (target_node);
    zlink_routing_id_t target_node_rid;
    TEST_ASSERT_EQUAL (ZLINK_CONFIG_OK,
                       zlink_get_routing_id (target_node, &target_node_rid));

    void *target_spot = zlink_spot_new (target_node);
    TEST_ASSERT_NOT_NULL (target_spot);
    zlink_routing_id_t target_spot_rid;
    TEST_ASSERT_EQUAL (ZLINK_CONFIG_OK,
                       zlink_get_routing_id (target_spot, &target_spot_rid));
    actor_probe_t probe;
    TEST_ASSERT_EQUAL (ZLINK_HANDLER_OK,
                       zlink_spot_dispatch_event_handler (target_spot,
                                                          on_dispatch,
                                                          &probe));

    void *source_actor =
      zlink_spot_node_actor_new (source_node, "remote-move");
    TEST_ASSERT_NOT_NULL (source_actor);
    zlink_actor_ref_t source_ref;
    TEST_ASSERT_EQUAL (ZLINK_CONFIG_OK,
                       zlink_actor_get_ref (source_actor, &source_ref));
    void *stream = zlink_socket (ctx, ZLINK_SOCKET_STREAM);
    TEST_ASSERT_NOT_NULL (stream);
    zlink_routing_id_t session_rid;
    set_rid (&session_rid, "remote-join-session");
    TEST_ASSERT_EQUAL (ZLINK_REQUEST_OK,
                       zlink_stream_bind_actor (session_node, stream,
                                                &session_rid, &source_ref,
                                                1000));

    zlink_msg_t join_msg;
    init_text_msg (&join_msg, "handoff-state");
    TEST_ASSERT_EQUAL (ZLINK_SUBMIT_OK,
                       (zlink_spot_node_actor_join_spot) (
                         session_node, &source_ref, &target_node_rid,
                         &target_spot_rid, &join_msg, on_join_reply, &probe,
                         ZLINK_DONTWAIT, 1000));
    {
        std::unique_lock<std::mutex> lock (probe.mutex);
        TEST_ASSERT_TRUE (probe.cv.wait_for (
          lock, std::chrono::seconds (2),
          [&] { return probe.join_done || probe.failed; }));
        TEST_ASSERT_FALSE (probe.failed);
        TEST_ASSERT_EQUAL (ZLINK_REQUEST_OK, probe.join_result);
        TEST_ASSERT_TRUE ((probe.last_join_info.flags
                           & ZLINK_ACTOR_JOIN_INFO_REMOTE)
                          != 0);
        TEST_ASSERT_EQUAL_STRING ("handoff-state",
                                  probe.join_payload.c_str ());
        TEST_ASSERT_EQUAL_UINT64 (source_ref.generation,
                                  probe.last_join_info.source_actor.generation);
        TEST_ASSERT_TRUE (rid_equals (probe.last_join_info.target_node_rid,
                                      target_node_rid));
        TEST_ASSERT_TRUE (rid_equals (
          probe.last_join_info.target_actor.node_rid, target_node_rid));
        TEST_ASSERT_TRUE (probe.last_join_info.target_actor.generation != 0);
    }

    TEST_ASSERT_EQUAL (ZLINK_CONFIG_NOT_FOUND,
                       zlink_spot_node_actor_lookup (source_node,
                                                     "remote-move",
                                                     &source_ref));
    TEST_ASSERT_EQUAL (ENOENT, zlink_errno ());
    zlink_actor_ref_t target_ref;
    TEST_ASSERT_EQUAL (ZLINK_CONFIG_OK,
                       zlink_spot_node_actor_lookup (target_node,
                                                     "remote-move",
                                                     &target_ref));
    TEST_ASSERT_TRUE (rid_equals (target_ref.node_rid, target_node_rid));
    g_test_actor_nodes_by_ref.erase (test_actor_ref_key (source_ref));
    g_test_actor_nodes_by_ref[test_actor_ref_key (target_ref)] = target_node;

    zlink_msg_t relayed;
    init_text_msg (&relayed, "after-handoff");
    TEST_ASSERT_EQUAL (ZLINK_SUBMIT_OK,
                       zlink_stream_send_bound_actor_part (
                         session_node, stream, &session_rid, "remote-move",
                         &relayed, ZLINK_DONTWAIT, ZLINK_PART_FINAL));
    {
        std::unique_lock<std::mutex> lock (probe.mutex);
        TEST_ASSERT_TRUE (probe.cv.wait_for (
          lock, std::chrono::seconds (2),
          [&] { return probe.actor_recv_count >= 1 || probe.failed; }));
        TEST_ASSERT_FALSE (probe.failed);
        TEST_ASSERT_EQUAL_STRING ("after-handoff", probe.payload.c_str ());
    }

    TEST_ASSERT_EQUAL (ZLINK_REQUEST_OK,
                       zlink_spot_node_actor_leave_spot (
                         session_node, &target_ref, &target_spot_rid, 1000));
    TEST_ASSERT_EQUAL (ZLINK_REQUEST_OK,
                       zlink_spot_node_actor_destroy (session_node,
                                                      &target_ref, 1000));

    void *reject_actor =
      zlink_spot_node_actor_new (source_node, "remote-reject");
    TEST_ASSERT_NOT_NULL (reject_actor);
    zlink_actor_ref_t reject_ref;
    TEST_ASSERT_EQUAL (ZLINK_CONFIG_OK,
                       zlink_actor_get_ref (reject_actor, &reject_ref));
    TEST_ASSERT_EQUAL (ZLINK_REQUEST_OK,
                       zlink_stream_bind_actor (session_node, stream,
                                                &session_rid, &reject_ref,
                                                1000));
    probe.accept_join = false;
    probe.join_done = false;
    probe.failed = false;
    zlink_msg_t reject_join;
    init_text_msg (&reject_join, "reject-state");
    TEST_ASSERT_EQUAL (ZLINK_SUBMIT_OK,
                       (zlink_spot_node_actor_join_spot) (
                         session_node, &reject_ref, &target_node_rid,
                         &target_spot_rid, &reject_join, on_join_reply,
                         &probe, ZLINK_DONTWAIT, 1000));
    {
        std::unique_lock<std::mutex> lock (probe.mutex);
        TEST_ASSERT_TRUE (probe.cv.wait_for (
          lock, std::chrono::seconds (2), [&] { return probe.join_done; }));
        TEST_ASSERT_EQUAL (ZLINK_REQUEST_REJECTED, probe.join_result);
    }
    TEST_ASSERT_EQUAL (ZLINK_CONFIG_OK,
                       zlink_spot_node_actor_lookup (source_node,
                                                     "remote-reject",
                                                     &reject_ref));
    zlink_actor_ref_t missing_target;
    TEST_ASSERT_EQUAL (ZLINK_CONFIG_NOT_FOUND,
                       zlink_spot_node_actor_lookup (target_node,
                                                     "remote-reject",
                                                     &missing_target));
    TEST_ASSERT_EQUAL (ZLINK_REQUEST_OK,
                       zlink_spot_node_actor_destroy (source_node,
                                                      &reject_ref, 1000));

    void *timeout_actor =
      zlink_spot_node_actor_new (source_node, "remote-timeout");
    TEST_ASSERT_NOT_NULL (timeout_actor);
    zlink_actor_ref_t timeout_ref;
    TEST_ASSERT_EQUAL (ZLINK_CONFIG_OK,
                       zlink_actor_get_ref (timeout_actor, &timeout_ref));
    TEST_ASSERT_EQUAL (ZLINK_REQUEST_OK,
                       zlink_stream_bind_actor (session_node, stream,
                                                &session_rid, &timeout_ref,
                                                1000));
    void *silent_spot = zlink_spot_new (target_node);
    TEST_ASSERT_NOT_NULL (silent_spot);
    zlink_routing_id_t silent_spot_rid;
    TEST_ASSERT_EQUAL (ZLINK_CONFIG_OK,
                       zlink_get_routing_id (silent_spot, &silent_spot_rid));
    actor_probe_t timeout_probe;
    zlink_msg_t timeout_join;
    init_text_msg (&timeout_join, "timeout-state");
    TEST_ASSERT_EQUAL (ZLINK_SUBMIT_OK,
                       (zlink_spot_node_actor_join_spot) (
                         session_node, &timeout_ref, &target_node_rid,
                         &silent_spot_rid, &timeout_join, on_join_reply,
                         &timeout_probe, ZLINK_DONTWAIT, 30));
    TEST_ASSERT_EQUAL (ZLINK_CONFIG_NOT_FOUND,
                       zlink_spot_node_actor_lookup (target_node,
                                                     "remote-timeout",
                                                     &missing_target));
    {
        size_t target_count = 0;
        TEST_ASSERT_EQUAL (ZLINK_CONFIG_OK,
                           zlink_spot_node_actors_snapshot (target_node, NULL,
                                                            &target_count));
        TEST_ASSERT_EQUAL_UINT (0, target_count);
    }
    {
        std::unique_lock<std::mutex> lock (timeout_probe.mutex);
        TEST_ASSERT_TRUE (timeout_probe.cv.wait_for (
          lock, std::chrono::seconds (2),
          [&] { return timeout_probe.join_done; }));
        TEST_ASSERT_EQUAL (ZLINK_REQUEST_TIMED_OUT,
                           timeout_probe.join_result);
    }
    TEST_ASSERT_EQUAL (ZLINK_CONFIG_OK,
                       zlink_spot_node_actor_lookup (source_node,
                                                     "remote-timeout",
                                                     &timeout_ref));
    TEST_ASSERT_EQUAL (ZLINK_CONFIG_NOT_FOUND,
                       zlink_spot_node_actor_lookup (target_node,
                                                     "remote-timeout",
                                                     &missing_target));
    TEST_ASSERT_EQUAL (ZLINK_REQUEST_OK,
                       zlink_spot_node_actor_destroy (source_node,
                                                      &timeout_ref, 1000));

    void *update_fail_spot = zlink_spot_new (target_node);
    TEST_ASSERT_NOT_NULL (update_fail_spot);
    zlink_routing_id_t update_fail_spot_rid;
    TEST_ASSERT_EQUAL (ZLINK_CONFIG_OK,
                       zlink_get_routing_id (update_fail_spot,
                                             &update_fail_spot_rid));
    void *update_fail_actor =
      zlink_spot_node_actor_new (source_node, "remote-session-update-fail");
    TEST_ASSERT_NOT_NULL (update_fail_actor);
    zlink_actor_ref_t update_fail_ref;
    TEST_ASSERT_EQUAL (ZLINK_CONFIG_OK,
                       zlink_actor_get_ref (update_fail_actor,
                                            &update_fail_ref));
    TEST_ASSERT_EQUAL (ZLINK_REQUEST_OK,
                       zlink_stream_bind_actor (session_node, stream,
                                                &session_rid,
                                                &update_fail_ref, 1000));
    actor_probe_t update_fail_probe;
    zlink_msg_t update_fail_join;
    init_text_msg (&update_fail_join, "session-update-fail");
    TEST_ASSERT_EQUAL (ZLINK_SUBMIT_OK,
                       (zlink_spot_node_actor_join_spot) (
                         session_node, &update_fail_ref, &target_node_rid,
                         &update_fail_spot_rid, &update_fail_join,
                         on_join_reply, &update_fail_probe, ZLINK_DONTWAIT,
                         5000));
    zlink_actor_join_info_t update_fail_info;
    zlink_msg_t update_fail_request;
    TEST_ASSERT_EQUAL (ZLINK_RECV_OK,
                       zlink_spot_actor_join_recv (
                         update_fail_spot, &update_fail_info,
                         &update_fail_request, ZLINK_RECV_FLAGS_DONTWAIT));
    TEST_ASSERT_EQUAL_STRING ("session-update-fail",
                              msg_text (&update_fail_request).c_str ());
    TEST_ASSERT_EQUAL (ZLINK_CONFIG_OK,
                       zlink_msg_close (&update_fail_request));
    TEST_ASSERT_EQUAL (ZLINK_REQUEST_OK,
                       zlink_stream_unbind_actor (
                         session_node, stream, &session_rid,
                         "remote-session-update-fail", 1000));
    zlink_msg_t update_fail_reply;
    zlink_msg_init (&update_fail_reply);
    TEST_ASSERT_EQUAL (ZLINK_SUBMIT_OK,
                       zlink_spot_actor_join_reply (
                         update_fail_spot, &update_fail_info, 1u,
                         &update_fail_reply));
    {
        std::unique_lock<std::mutex> lock (update_fail_probe.mutex);
        TEST_ASSERT_TRUE (update_fail_probe.cv.wait_for (
          lock, std::chrono::seconds (2),
          [&] { return update_fail_probe.join_done; }));
        TEST_ASSERT_EQUAL (ZLINK_REQUEST_CONFLICT,
                           update_fail_probe.join_result);
    }
    TEST_ASSERT_EQUAL (ZLINK_CONFIG_OK,
                       zlink_spot_node_actor_lookup (
                         source_node, "remote-session-update-fail",
                         &update_fail_ref));
    TEST_ASSERT_EQUAL (ZLINK_CONFIG_NOT_FOUND,
                       zlink_spot_node_actor_lookup (
                         target_node, "remote-session-update-fail",
                         &missing_target));
    TEST_ASSERT_EQUAL (ZLINK_REQUEST_OK,
                       zlink_actor_destroy (&update_fail_actor, 1000));
    TEST_ASSERT_EQUAL (ZLINK_CLOSE_OK,
                       zlink_spot_destroy (&update_fail_spot));

    void *pending_buffer_spot = zlink_spot_new (target_node);
    TEST_ASSERT_NOT_NULL (pending_buffer_spot);
    zlink_routing_id_t pending_buffer_spot_rid;
    TEST_ASSERT_EQUAL (ZLINK_CONFIG_OK,
                       zlink_get_routing_id (pending_buffer_spot,
                                             &pending_buffer_spot_rid));
    void *pending_buffer_actor =
      zlink_spot_node_actor_new (source_node, "remote-pending-buffer");
    TEST_ASSERT_NOT_NULL (pending_buffer_actor);
    zlink_actor_ref_t pending_buffer_source_ref;
    TEST_ASSERT_EQUAL (ZLINK_CONFIG_OK,
                       zlink_actor_get_ref (pending_buffer_actor,
                                            &pending_buffer_source_ref));
    TEST_ASSERT_EQUAL (ZLINK_REQUEST_OK,
                       zlink_stream_bind_actor (session_node, stream,
                                                &session_rid,
                                                &pending_buffer_source_ref,
                                                1000));
    actor_probe_t pending_buffer_probe;
    zlink_msg_t pending_buffer_join;
    init_text_msg (&pending_buffer_join, "pending-buffer");
    TEST_ASSERT_EQUAL (ZLINK_SUBMIT_OK,
                       (zlink_spot_node_actor_join_spot) (
                         session_node, &pending_buffer_source_ref,
                         &target_node_rid, &pending_buffer_spot_rid,
                         &pending_buffer_join, on_join_reply,
                         &pending_buffer_probe, ZLINK_DONTWAIT, 5000));
    zlink_actor_join_info_t pending_buffer_info;
    zlink_msg_t pending_buffer_request;
    TEST_ASSERT_EQUAL (ZLINK_RECV_OK,
                       zlink_spot_actor_join_recv (
                         pending_buffer_spot, &pending_buffer_info,
                         &pending_buffer_request,
                         ZLINK_RECV_FLAGS_DONTWAIT));
    TEST_ASSERT_EQUAL (ZLINK_CONFIG_OK,
                       zlink_msg_close (&pending_buffer_request));
    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_actor_pending_target_enqueue_for_testing (
        &pending_buffer_info.target_actor, "buffered-after-visibility"));
    zlink_msg_t pending_buffer_reply;
    zlink_msg_init (&pending_buffer_reply);
    TEST_ASSERT_EQUAL (ZLINK_SUBMIT_OK,
                       zlink_spot_actor_join_reply (
                         pending_buffer_spot, &pending_buffer_info, 1u,
                         &pending_buffer_reply));
    {
        std::unique_lock<std::mutex> lock (pending_buffer_probe.mutex);
        TEST_ASSERT_TRUE (pending_buffer_probe.cv.wait_for (
          lock, std::chrono::seconds (2),
          [&] { return pending_buffer_probe.join_done; }));
        TEST_ASSERT_EQUAL (ZLINK_REQUEST_OK,
                           pending_buffer_probe.join_result);
    }
    g_test_actor_nodes_by_ref.erase (
      test_actor_ref_key (pending_buffer_source_ref));
    g_test_actor_handles.erase (
      static_cast<test_actor_handle_t *> (pending_buffer_actor));
    delete static_cast<test_actor_handle_t *> (pending_buffer_actor);
    pending_buffer_actor = NULL;
    zlink_actor_ref_t pending_buffer_target_ref;
    TEST_ASSERT_EQUAL (ZLINK_CONFIG_OK,
                       zlink_spot_node_actor_lookup (
                         target_node, "remote-pending-buffer",
                         &pending_buffer_target_ref));
    size_t pending_buffer_queue_size = 0;
    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_actor_queue_size_for_testing (&pending_buffer_target_ref,
                                          &pending_buffer_queue_size));
    TEST_ASSERT_EQUAL_UINT (1, pending_buffer_queue_size);
    TEST_ASSERT_EQUAL (ZLINK_REQUEST_OK,
                       zlink_spot_node_actor_leave_spot (
                         session_node, &pending_buffer_target_ref,
                         &pending_buffer_spot_rid, 1000));
    TEST_ASSERT_EQUAL (ZLINK_REQUEST_OK,
                       zlink_spot_node_actor_destroy (
                         target_node, &pending_buffer_target_ref, 1000));
    TEST_ASSERT_EQUAL (ZLINK_CLOSE_OK,
                       zlink_spot_destroy (&pending_buffer_spot));

    void *disconnect_actor =
      zlink_spot_node_actor_new (source_node, "remote-disconnect-before");
    TEST_ASSERT_NOT_NULL (disconnect_actor);
    zlink_actor_ref_t disconnect_ref;
    TEST_ASSERT_EQUAL (ZLINK_CONFIG_OK,
                       zlink_actor_get_ref (disconnect_actor,
                                            &disconnect_ref));
    TEST_ASSERT_EQUAL (ZLINK_REQUEST_OK,
                       zlink_stream_bind_actor (session_node, stream,
                                                &session_rid, &disconnect_ref,
                                                1000));
    actor_probe_t disconnect_probe;
    zlink_msg_t disconnect_join;
    init_text_msg (&disconnect_join, "disconnect-before");
    TEST_ASSERT_EQUAL (ZLINK_SUBMIT_OK,
                       (zlink_spot_node_actor_join_spot) (
                         session_node, &disconnect_ref, &target_node_rid,
                         &silent_spot_rid, &disconnect_join, on_join_reply,
                         &disconnect_probe, ZLINK_DONTWAIT, 5000));
    TEST_ASSERT_EQUAL (ZLINK_CONFIG_NOT_FOUND,
                       zlink_spot_node_actor_lookup (
                         target_node, "remote-disconnect-before",
                         &missing_target));
    TEST_ASSERT_EQUAL (ZLINK_CLOSE_OK, zlink_close (stream));
    stream = NULL;
    {
        std::unique_lock<std::mutex> lock (disconnect_probe.mutex);
        TEST_ASSERT_TRUE (disconnect_probe.cv.wait_for (
          lock, std::chrono::seconds (2),
          [&] { return disconnect_probe.join_done; }));
        TEST_ASSERT_EQUAL (ZLINK_REQUEST_TERMINATED,
                           disconnect_probe.join_result);
    }
    TEST_ASSERT_EQUAL (ZLINK_CONFIG_OK,
                       zlink_spot_node_actor_lookup (
                         source_node, "remote-disconnect-before",
                         &disconnect_ref));
    TEST_ASSERT_EQUAL (ZLINK_CONFIG_NOT_FOUND,
                       zlink_spot_node_actor_lookup (
                         target_node, "remote-disconnect-before",
                         &missing_target));
    TEST_ASSERT_EQUAL (ZLINK_REQUEST_OK,
                       zlink_spot_node_actor_destroy (source_node,
                                                      &disconnect_ref, 1000));

    void *after_stream = zlink_socket (ctx, ZLINK_SOCKET_STREAM);
    TEST_ASSERT_NOT_NULL (after_stream);
    zlink_routing_id_t after_session_rid;
    set_rid (&after_session_rid, "remote-disconnect-after-session");
    void *after_actor =
      zlink_spot_node_actor_new (source_node, "remote-disconnect-after");
    TEST_ASSERT_NOT_NULL (after_actor);
    zlink_actor_ref_t after_ref;
    TEST_ASSERT_EQUAL (ZLINK_CONFIG_OK,
                       zlink_actor_get_ref (after_actor, &after_ref));
    TEST_ASSERT_EQUAL (ZLINK_REQUEST_OK,
                       zlink_stream_bind_actor (session_node, after_stream,
                                                &after_session_rid,
                                                &after_ref, 1000));
    probe.accept_join = true;
    probe.join_done = false;
    probe.failed = false;
    zlink_msg_t after_join;
    init_text_msg (&after_join, "disconnect-after");
    TEST_ASSERT_EQUAL (ZLINK_SUBMIT_OK,
                       (zlink_spot_node_actor_join_spot) (
                         session_node, &after_ref, &target_node_rid,
                         &target_spot_rid, &after_join, on_join_reply,
                         &probe, ZLINK_DONTWAIT, 1000));
    {
        std::unique_lock<std::mutex> lock (probe.mutex);
        TEST_ASSERT_TRUE (probe.cv.wait_for (
          lock, std::chrono::seconds (2),
          [&] { return probe.join_done || probe.failed; }));
        TEST_ASSERT_FALSE (probe.failed);
        TEST_ASSERT_EQUAL (ZLINK_REQUEST_OK, probe.join_result);
    }
    zlink_actor_ref_t after_target_ref;
    TEST_ASSERT_EQUAL (ZLINK_CONFIG_OK,
                       zlink_spot_node_actor_lookup (
                         target_node, "remote-disconnect-after",
                         &after_target_ref));
    TEST_ASSERT_EQUAL (ZLINK_CONFIG_NOT_FOUND,
                       zlink_spot_node_actor_lookup (
                         source_node, "remote-disconnect-after", &after_ref));
    TEST_ASSERT_EQUAL (ZLINK_CLOSE_OK, zlink_close (after_stream));
    void *target_entry = NULL;
    TEST_ASSERT_EQUAL (ZLINK_CONFIG_OK,
                       zlink_spot_node_entry_spot (target_node,
                                                   &target_entry));
    zlink_actor_ref_t target_entry_rows[1];
    size_t target_entry_count = 1;
    TEST_ASSERT_EQUAL (ZLINK_CONFIG_OK,
                       zlink_spot_actors_snapshot (target_entry,
                                                   target_entry_rows,
                                                   &target_entry_count));
    TEST_ASSERT_EQUAL_UINT (1, target_entry_count);
    TEST_ASSERT_EQUAL_UINT64 (after_target_ref.generation,
                              target_entry_rows[0].generation);
    TEST_ASSERT_EQUAL (ZLINK_REQUEST_OK,
                       zlink_spot_node_actor_destroy (target_node,
                                                      &after_target_ref,
                                                      1000));
    TEST_ASSERT_EQUAL (ZLINK_CLOSE_OK, zlink_spot_destroy (&target_entry));

    TEST_ASSERT_EQUAL (ZLINK_CLOSE_OK, zlink_spot_destroy (&silent_spot));
    if (stream)
        TEST_ASSERT_EQUAL (ZLINK_CLOSE_OK, zlink_close (stream));
    TEST_ASSERT_EQUAL (ZLINK_CLOSE_OK, zlink_spot_destroy (&target_spot));
    TEST_ASSERT_EQUAL (ZLINK_CLOSE_OK, zlink_spot_node_destroy (&target_node));
    TEST_ASSERT_EQUAL (ZLINK_CLOSE_OK, zlink_spot_node_destroy (&source_node));
    TEST_ASSERT_EQUAL (ZLINK_CLOSE_OK, zlink_spot_node_destroy (&session_node));
    TEST_ASSERT_EQUAL (ZLINK_CLOSE_OK, zlink_ctx_term (ctx));
}

void test_actor_close_bound_session_returns_entry_and_dispatches ()
{
    void *ctx = zlink_ctx_new ();
    TEST_ASSERT_NOT_NULL (ctx);

    zlink_spot_node_options_t options;
    options.mode = ZLINK_SPOT_NODE_MODE_ALL;
    void *node = zlink_spot_node_new (ctx, &options);
    TEST_ASSERT_NOT_NULL (node);
    void *entry = NULL;
    TEST_ASSERT_EQUAL (ZLINK_CONFIG_OK,
                       zlink_spot_node_entry_spot (node, &entry));
    TEST_ASSERT_NOT_NULL (entry);
    actor_probe_t entry_probe;
    TEST_ASSERT_EQUAL (ZLINK_HANDLER_OK,
                       zlink_spot_dispatch_event_handler (entry, on_dispatch,
                                                          &entry_probe));

    void *spot = zlink_spot_new (node);
    TEST_ASSERT_NOT_NULL (spot);
    zlink_routing_id_t spot_rid;
    TEST_ASSERT_EQUAL (ZLINK_CONFIG_OK, zlink_get_routing_id (spot, &spot_rid));
    actor_probe_t join_probe;
    TEST_ASSERT_EQUAL (ZLINK_HANDLER_OK,
                       zlink_spot_dispatch_event_handler (
                         spot, on_join_only_dispatch, &join_probe));

    void *actor = zlink_spot_node_actor_new (node, "close-bound");
    TEST_ASSERT_NOT_NULL (actor);
    zlink_actor_ref_t ref;
    TEST_ASSERT_EQUAL (ZLINK_CONFIG_OK, zlink_actor_get_ref (actor, &ref));
    void *stream = zlink_socket (ctx, ZLINK_SOCKET_STREAM);
    TEST_ASSERT_NOT_NULL (stream);
    zlink_routing_id_t session_rid;
    set_rid (&session_rid, "close-bound-session");
    TEST_ASSERT_EQUAL (ZLINK_REQUEST_OK,
                       zlink_stream_bind_actor (node, stream, &session_rid,
                                                &ref, 1000));

    zlink_msg_t join_msg;
    init_text_msg (&join_msg, "join");
    TEST_ASSERT_EQUAL (ZLINK_SUBMIT_OK,
                       zlink_actor_join_spot (actor, spot, &join_msg,
                                              on_join_reply, &join_probe,
                                              ZLINK_DONTWAIT, 1000));
    {
        std::unique_lock<std::mutex> lock (join_probe.mutex);
        TEST_ASSERT_TRUE (join_probe.cv.wait_for (
          lock, std::chrono::seconds (2),
          [&] { return join_probe.join_done; }));
        TEST_ASSERT_EQUAL (ZLINK_REQUEST_OK, join_probe.join_result);
    }
    TEST_ASSERT_EQUAL (ZLINK_REQUEST_INVALID_STATE,
                       zlink_stream_unbind_actor (node, stream, &session_rid,
                                                  "close-bound", 1000));

    zlink_msg_t unread;
    init_text_msg (&unread, "queued-before-close");
    TEST_ASSERT_EQUAL (ZLINK_SUBMIT_OK,
                       zlink_stream_send_bound_actor_part (
                         node, stream, &session_rid, "close-bound", &unread,
                         ZLINK_DONTWAIT, ZLINK_PART_FINAL));
    TEST_ASSERT_EQUAL (ZLINK_REQUEST_OK,
                       zlink_spot_node_actor_close_bound_session (node, &ref,
                                                                  1000));
    {
        std::unique_lock<std::mutex> lock (entry_probe.mutex);
        TEST_ASSERT_TRUE (entry_probe.cv.wait_for (
          lock, std::chrono::seconds (2),
          [&] { return entry_probe.actor_recv_count >= 1
                         || entry_probe.failed; }));
        TEST_ASSERT_FALSE (entry_probe.failed);
        TEST_ASSERT_EQUAL_STRING ("queued-before-close",
                                  entry_probe.payload.c_str ());
    }

    zlink_actor_ref_t entry_rows[1];
    size_t entry_count = 1;
    TEST_ASSERT_EQUAL (ZLINK_CONFIG_OK,
                       zlink_spot_actors_snapshot (entry, entry_rows,
                                                   &entry_count));
    TEST_ASSERT_EQUAL_UINT (1, entry_count);
    TEST_ASSERT_EQUAL_UINT64 (ref.generation, entry_rows[0].generation);

    TEST_ASSERT_EQUAL (ZLINK_REQUEST_NOT_FOUND,
                       zlink_spot_node_actor_close_bound_session (node, &ref,
                                                                  0));
    TEST_ASSERT_EQUAL (ZLINK_REQUEST_OK, zlink_actor_destroy (&actor, 0));
    TEST_ASSERT_EQUAL (ZLINK_CLOSE_OK, zlink_close (stream));
    TEST_ASSERT_EQUAL (ZLINK_CLOSE_OK, zlink_spot_destroy (&spot));
    TEST_ASSERT_EQUAL (ZLINK_CLOSE_OK, zlink_spot_destroy (&entry));
    TEST_ASSERT_EQUAL (ZLINK_CLOSE_OK, zlink_spot_node_destroy (&node));
    TEST_ASSERT_EQUAL (ZLINK_CLOSE_OK, zlink_ctx_term (ctx));
}

void test_actor_join_reject_ref_edges_and_rejoin_fifo ()
{
    void *ctx = zlink_ctx_new ();
    TEST_ASSERT_NOT_NULL (ctx);

    zlink_spot_node_options_t options;
    options.mode = ZLINK_SPOT_NODE_MODE_ALL;
    void *node = zlink_spot_node_new (ctx, &options);
    void *caller_node = zlink_spot_node_new (ctx, &options);
    TEST_ASSERT_NOT_NULL (node);
    TEST_ASSERT_NOT_NULL (caller_node);
    void *spot = zlink_spot_new (node);
    TEST_ASSERT_NOT_NULL (spot);
    zlink_routing_id_t spot_rid;
    TEST_ASSERT_EQUAL (ZLINK_CONFIG_OK, zlink_get_routing_id (spot, &spot_rid));
    void *second_spot = zlink_spot_new (node);
    TEST_ASSERT_NOT_NULL (second_spot);
    zlink_routing_id_t second_spot_rid;
    TEST_ASSERT_EQUAL (ZLINK_CONFIG_OK,
                       zlink_get_routing_id (second_spot, &second_spot_rid));
    void *actor = zlink_spot_node_actor_new (node, "join-edge");
    TEST_ASSERT_NOT_NULL (actor);
    zlink_actor_ref_t ref;
    TEST_ASSERT_EQUAL (ZLINK_CONFIG_OK, zlink_actor_get_ref (actor, &ref));
    void *stream = zlink_socket (ctx, ZLINK_SOCKET_STREAM);
    TEST_ASSERT_NOT_NULL (stream);
    zlink_routing_id_t session_rid;
    set_rid (&session_rid, "join-fifo-session");
    TEST_ASSERT_EQUAL (ZLINK_REQUEST_OK,
                       zlink_stream_bind_actor (node, stream, &session_rid,
                                                &ref, 1000));

    actor_probe_t reject_probe;
    reject_probe.accept_join = false;
    TEST_ASSERT_EQUAL (ZLINK_HANDLER_OK,
                       zlink_spot_dispatch_event_handler (spot, on_dispatch,
                                                          &reject_probe));
    zlink_msg_t reject_msg;
    init_text_msg (&reject_msg, "reject");
    TEST_ASSERT_EQUAL (ZLINK_SUBMIT_OK,
                       zlink_actor_join_spot (actor, spot, &reject_msg,
                                              on_join_reply, &reject_probe,
                                              ZLINK_DONTWAIT, 1000));
    TEST_ASSERT_EQUAL_UINT (0, zlink_msg_size (&reject_msg));
    {
        std::unique_lock<std::mutex> lock (reject_probe.mutex);
        TEST_ASSERT_TRUE (reject_probe.cv.wait_for (
          lock, std::chrono::seconds (2),
          [&] { return reject_probe.join_done; }));
        TEST_ASSERT_EQUAL (ZLINK_REQUEST_REJECTED, reject_probe.join_result);
    }

    zlink_routing_id_t node_rid;
    TEST_ASSERT_EQUAL (ZLINK_CONFIG_OK, zlink_get_routing_id (node, &node_rid));

    zlink_routing_id_t missing_node_rid;
    set_rid (&missing_node_rid, "missing-node");
    zlink_actor_ref_t missing_node_ref;
    TEST_ASSERT_EQUAL (ZLINK_CONFIG_OK,
                       zlink_remote_actor_get_ref (&missing_node_rid,
                                                   "join-edge",
                                                   &missing_node_ref));
    actor_probe_t missing_node_probe;
    zlink_msg_t missing_node_msg;
    init_text_msg (&missing_node_msg, "not-connected");
    TEST_ASSERT_EQUAL (ZLINK_SUBMIT_NOT_CONNECTED,
                       zlink_spot_node_actor_join_spot (
                         caller_node, &missing_node_ref, &spot_rid,
                         &missing_node_msg, on_join_reply,
                         &missing_node_probe, ZLINK_DONTWAIT, 1000));
    TEST_ASSERT_EQUAL_STRING ("not-connected",
                              msg_text (&missing_node_msg).c_str ());
    TEST_ASSERT_EQUAL (ZLINK_CONFIG_OK, zlink_msg_close (&missing_node_msg));
    TEST_ASSERT_EQUAL (ZLINK_REQUEST_NOT_CONNECTED,
                       zlink_spot_node_actor_leave_spot (
                         caller_node, &missing_node_ref, &spot_rid, 1000));

    zlink_actor_ref_t missing_actor_ref;
    TEST_ASSERT_EQUAL (ZLINK_CONFIG_OK,
                       zlink_remote_actor_get_ref (&node_rid, "missing-actor",
                                                   &missing_actor_ref));
    actor_probe_t missing_actor_probe;
    zlink_msg_t missing_actor_msg;
    init_text_msg (&missing_actor_msg, "missing-actor");
    TEST_ASSERT_EQUAL (ZLINK_SUBMIT_OK,
                       zlink_spot_node_actor_join_spot (
                         caller_node, &missing_actor_ref, &spot_rid,
                         &missing_actor_msg, on_join_reply,
                         &missing_actor_probe, ZLINK_DONTWAIT, 1000));
    TEST_ASSERT_EQUAL_UINT (0, zlink_msg_size (&missing_actor_msg));
    {
        std::unique_lock<std::mutex> lock (missing_actor_probe.mutex);
        TEST_ASSERT_TRUE (missing_actor_probe.cv.wait_for (
          lock, std::chrono::seconds (2),
          [&] { return missing_actor_probe.join_done; }));
        TEST_ASSERT_EQUAL (ZLINK_REQUEST_NOT_FOUND,
                           missing_actor_probe.join_result);
    }

    zlink_routing_id_t missing_spot_rid;
    set_rid (&missing_spot_rid, "missing-spot");
    actor_probe_t missing_spot_probe;
    zlink_msg_t missing_spot_msg;
    init_text_msg (&missing_spot_msg, "missing-spot");
    TEST_ASSERT_EQUAL (ZLINK_SUBMIT_OK,
                       zlink_spot_node_actor_join_spot (
                         caller_node, &ref, &missing_spot_rid,
                         &missing_spot_msg, on_join_reply,
                         &missing_spot_probe, ZLINK_DONTWAIT, 1000));
    {
        std::unique_lock<std::mutex> lock (missing_spot_probe.mutex);
        TEST_ASSERT_TRUE (missing_spot_probe.cv.wait_for (
          lock, std::chrono::seconds (2),
          [&] { return missing_spot_probe.join_done; }));
        TEST_ASSERT_EQUAL (ZLINK_REQUEST_NOT_FOUND,
                           missing_spot_probe.join_result);
    }

    zlink_actor_ref_t stale_ref = ref;
    ++stale_ref.generation;
    actor_probe_t stale_probe;
    zlink_msg_t stale_msg;
    init_text_msg (&stale_msg, "stale");
    TEST_ASSERT_EQUAL (ZLINK_SUBMIT_OK,
                       zlink_spot_node_actor_join_spot (
                         caller_node, &stale_ref, &spot_rid, &stale_msg,
                         on_join_reply, &stale_probe, ZLINK_DONTWAIT, 1000));
    {
        std::unique_lock<std::mutex> lock (stale_probe.mutex);
        TEST_ASSERT_TRUE (stale_probe.cv.wait_for (
          lock, std::chrono::seconds (2),
          [&] { return stale_probe.join_done; }));
        TEST_ASSERT_EQUAL (ZLINK_REQUEST_CONFLICT, stale_probe.join_result);
    }

    actor_probe_t join_only_probe;
    TEST_ASSERT_EQUAL (ZLINK_HANDLER_OK,
                       zlink_spot_dispatch_event_handler (
                         second_spot, on_join_only_dispatch,
                         &join_only_probe));
    zlink_actor_ref_t unchecked_ref = ref;
    unchecked_ref.generation = 0;
    zlink_msg_t unchecked_join;
    init_text_msg (&unchecked_join, "unchecked");
    TEST_ASSERT_EQUAL (ZLINK_SUBMIT_OK,
                       zlink_spot_node_actor_join_spot (
                         caller_node, &unchecked_ref, &second_spot_rid,
                         &unchecked_join, on_join_reply, &join_only_probe,
                         ZLINK_DONTWAIT, 1000));
    {
        std::unique_lock<std::mutex> lock (join_only_probe.mutex);
        TEST_ASSERT_TRUE (join_only_probe.cv.wait_for (
          lock, std::chrono::seconds (2),
          [&] { return join_only_probe.join_done; }));
        TEST_ASSERT_EQUAL (ZLINK_REQUEST_OK, join_only_probe.join_result);
    }

    zlink_msg_t before_leave;
    init_text_msg (&before_leave, "before-");
    TEST_ASSERT_EQUAL (ZLINK_SUBMIT_OK,
                       zlink_stream_send_bound_actor_part (
                         node, stream, &session_rid, "join-edge",
                         &before_leave, ZLINK_DONTWAIT, ZLINK_PART_FINAL));
    msleep (50);
    TEST_ASSERT_EQUAL (ZLINK_CONFIG_OK,
                       zlink_actor_leave_spot (actor, second_spot));
    zlink_msg_t between_join;
    init_text_msg (&between_join, "between-");
    TEST_ASSERT_EQUAL (ZLINK_SUBMIT_OK,
                       zlink_stream_send_bound_actor_part (
                         node, stream, &session_rid, "join-edge",
                         &between_join, ZLINK_DONTWAIT, ZLINK_PART_FINAL));

    actor_probe_t drain_probe;
    void *drain_spot = zlink_spot_new (node);
    TEST_ASSERT_NOT_NULL (drain_spot);
    zlink_routing_id_t drain_spot_rid;
    TEST_ASSERT_EQUAL (ZLINK_CONFIG_OK,
                       zlink_get_routing_id (drain_spot, &drain_spot_rid));
    TEST_ASSERT_EQUAL (ZLINK_HANDLER_OK,
                       zlink_spot_dispatch_event_handler (drain_spot,
                                                          on_dispatch,
                                                          &drain_probe));
    zlink_msg_t rejoin_msg;
    init_text_msg (&rejoin_msg, "rejoin");
    TEST_ASSERT_EQUAL (ZLINK_SUBMIT_OK,
                       zlink_spot_node_actor_join_spot (
                         caller_node, &unchecked_ref, &drain_spot_rid,
                         &rejoin_msg, on_join_reply, &drain_probe,
                         ZLINK_DONTWAIT, 1000));
    {
        std::unique_lock<std::mutex> lock (drain_probe.mutex);
        TEST_ASSERT_TRUE (drain_probe.cv.wait_for (
          lock, std::chrono::seconds (2),
          [&] { return drain_probe.join_done; }));
        TEST_ASSERT_EQUAL (ZLINK_REQUEST_OK, drain_probe.join_result);
        TEST_ASSERT_TRUE (drain_probe.cv.wait_for (
          lock, std::chrono::seconds (2),
          [&] { return drain_probe.actor_recv_count >= 2
                         || drain_probe.failed; }));
        TEST_ASSERT_FALSE (drain_probe.failed);
        TEST_ASSERT_EQUAL_STRING ("before-between-",
                                  drain_probe.payload.c_str ());
    }

    zlink_msg_t after_join;
    init_text_msg (&after_join, "after");
    TEST_ASSERT_EQUAL (ZLINK_SUBMIT_OK,
                       zlink_stream_send_bound_actor_part (
                         node, stream, &session_rid, "join-edge",
                         &after_join, ZLINK_DONTWAIT, ZLINK_PART_FINAL));
    {
        std::unique_lock<std::mutex> lock (drain_probe.mutex);
        TEST_ASSERT_TRUE (drain_probe.cv.wait_for (
          lock, std::chrono::seconds (2),
          [&] { return drain_probe.actor_recv_count >= 3
                         || drain_probe.failed; }));
        TEST_ASSERT_FALSE (drain_probe.failed);
        TEST_ASSERT_EQUAL_STRING ("before-between-after",
                                  drain_probe.payload.c_str ());
    }

    TEST_ASSERT_EQUAL (ZLINK_REQUEST_OK,
                       zlink_spot_node_actor_leave_spot (
                         caller_node, &unchecked_ref, &drain_spot_rid, 0));
    TEST_ASSERT_EQUAL (ZLINK_REQUEST_OK, zlink_actor_destroy (&actor, 0));
    TEST_ASSERT_EQUAL (ZLINK_CLOSE_OK, zlink_close (stream));
    TEST_ASSERT_EQUAL (ZLINK_CLOSE_OK, zlink_spot_destroy (&drain_spot));
    TEST_ASSERT_EQUAL (ZLINK_CLOSE_OK, zlink_spot_destroy (&second_spot));
    TEST_ASSERT_EQUAL (ZLINK_CLOSE_OK, zlink_spot_destroy (&spot));
    TEST_ASSERT_EQUAL (ZLINK_CLOSE_OK, zlink_spot_node_destroy (&caller_node));
    TEST_ASSERT_EQUAL (ZLINK_CLOSE_OK, zlink_spot_node_destroy (&node));
    TEST_ASSERT_EQUAL (ZLINK_CLOSE_OK, zlink_ctx_term (ctx));
}

void test_actor_route_sync_publish_requires_option_and_bind ()
{
    if (!zlink_has ("tcp")) {
        TEST_IGNORE_MESSAGE ("TCP not available");
        return;
    }

    void *ctx = zlink_ctx_new ();
    TEST_ASSERT_NOT_NULL (ctx);

    void *registry = zlink_registry_new (ctx);
    TEST_ASSERT_NOT_NULL (registry);
    TEST_ASSERT_EQUAL (ZLINK_CONFIG_OK,
                       zlink_registry_set_broadcast_interval (registry, 50));
    char registry_pub[MAX_SOCKET_STRING] = {0};
    char registry_router[MAX_SOCKET_STRING] = {0};
    TEST_ASSERT_TRUE (bind_registry_endpoints (
      registry, registry_pub, sizeof (registry_pub), registry_router,
      sizeof (registry_router)));

    void *discovery =
      zlink_discovery_new (ctx, ZLINK_AUTO_CONNECT_SPOT_MESH, "actors");
    TEST_ASSERT_NOT_NULL (discovery);
    int spot_owner_sync = 1;
    TEST_ASSERT_EQUAL (ZLINK_CONFIG_OK,
                       zlink_set_option (discovery,
                                         ZLINK_OPT_DISCOVERY_SPOT_OWNER_SYNC,
                                         &spot_owner_sync,
                                         sizeof (spot_owner_sync)));
    TEST_ASSERT_TRUE (
      connect_registry_with_retry (discovery, registry_router, 3000));

    zlink_spot_node_options_t options;
    options.mode = ZLINK_SPOT_NODE_MODE_ALL;
    void *node = zlink_spot_node_new (ctx, &options);
    TEST_ASSERT_NOT_NULL (node);
    void *spot = zlink_spot_new (node);
    TEST_ASSERT_NOT_NULL (spot);
    char node_endpoint[MAX_SOCKET_STRING] = {0};
    TEST_ASSERT_TRUE (
      bind_spot_endpoint (node, node_endpoint, sizeof (node_endpoint)));
    TEST_ASSERT_EQUAL (ZLINK_CONFIG_OK,
                       zlink_spot_node_attach_discovery (node, discovery));

    zlink_spot_node_spot_entry_t spot_rows[1];
    size_t spot_row_count = 1;
    TEST_ASSERT_EQUAL (ZLINK_CONFIG_OK,
                       zlink_spot_node_spots_snapshot (node, spot_rows,
                                                       &spot_row_count));
    TEST_ASSERT_EQUAL_UINT (1, spot_row_count);
    TEST_ASSERT_EQUAL_UINT32 (1, spot_rows[0].route_synced);

    void *actor = zlink_spot_node_actor_new (node, "route-a");
    TEST_ASSERT_NOT_NULL (actor);
    zlink_actor_ref_t ref;
    TEST_ASSERT_EQUAL (ZLINK_CONFIG_OK, zlink_actor_get_ref (actor, &ref));

    zlink_actor_route_t route;
    TEST_ASSERT_EQUAL (ZLINK_CONFIG_INVALID_ARGUMENT,
                       zlink_discovery_resolve_actor (discovery, "route-a",
                                                     &route));
    TEST_ASSERT_EQUAL (ENOENT, zlink_errno ());

    zlink_routing_id_t session_rid;
    set_rid (&session_rid, "route-session");
    void *stream_marker = zlink_socket (ctx, ZLINK_SOCKET_STREAM);
    TEST_ASSERT_NOT_NULL (stream_marker);
    TEST_ASSERT_EQUAL (ZLINK_REQUEST_OK,
                       zlink_stream_bind_actor (node, stream_marker,
                                                &session_rid, &ref, 1000));
    TEST_ASSERT_EQUAL (ZLINK_CONFIG_INVALID_ARGUMENT,
                       zlink_discovery_resolve_actor (discovery, "route-a",
                                                     &route));
    TEST_ASSERT_EQUAL (ENOENT, zlink_errno ());

    TEST_ASSERT_EQUAL (ZLINK_REQUEST_OK,
                       zlink_stream_unbind_actor (node, stream_marker,
                                                  &session_rid, "route-a",
                                                  1000));
    TEST_ASSERT_EQUAL (ZLINK_REQUEST_OK, zlink_actor_destroy (&actor, 0));

    int enabled = 1;
    TEST_ASSERT_EQUAL (ZLINK_CONFIG_OK,
                       zlink_set_option (discovery,
                                         ZLINK_OPT_DISCOVERY_ACTOR_ROUTE_SYNC,
                                         &enabled, sizeof (enabled)));
    actor = zlink_spot_node_actor_new (node, "route-a");
    TEST_ASSERT_NOT_NULL (actor);
    TEST_ASSERT_EQUAL (ZLINK_CONFIG_OK, zlink_actor_get_ref (actor, &ref));
    TEST_ASSERT_EQUAL (ZLINK_CONFIG_INVALID_ARGUMENT,
                       zlink_discovery_resolve_actor (discovery, "route-a",
                                                     &route));
    TEST_ASSERT_EQUAL (ENOENT, zlink_errno ());

    TEST_ASSERT_EQUAL (ZLINK_REQUEST_OK,
                       zlink_stream_bind_actor (node, stream_marker,
                                                &session_rid, &ref, 1000));
    TEST_ASSERT_EQUAL (ZLINK_CONFIG_OK,
                       zlink_discovery_resolve_actor (discovery, "route-a",
                                                     &route));
    TEST_ASSERT_EQUAL_STRING ("route-a", route.actor.actor_id);
    TEST_ASSERT_EQUAL_UINT64 (ref.generation, route.actor.generation);
    zlink_spot_node_actor_entry_t actor_rows[1];
    size_t actor_row_count = 1;
    TEST_ASSERT_EQUAL (ZLINK_CONFIG_OK,
                       zlink_spot_node_actors_snapshot (node, actor_rows,
                                                        &actor_row_count));
    TEST_ASSERT_EQUAL_UINT (1, actor_row_count);
    TEST_ASSERT_EQUAL_UINT32 (1, actor_rows[0].route_synced);

    TEST_ASSERT_EQUAL (ZLINK_REQUEST_OK,
                       zlink_stream_unbind_actor (node, stream_marker,
                                                  &session_rid, "route-a",
                                                  1000));
    TEST_ASSERT_EQUAL (ZLINK_REQUEST_OK, zlink_actor_destroy (&actor, 0));
    TEST_ASSERT_EQUAL (ZLINK_CONFIG_INVALID_ARGUMENT,
                       zlink_discovery_resolve_actor (discovery, "route-a",
                                                     &route));
    TEST_ASSERT_EQUAL (ENOENT, zlink_errno ());

    TEST_ASSERT_EQUAL (ZLINK_CLOSE_OK, zlink_spot_destroy (&spot));
    TEST_ASSERT_EQUAL (ZLINK_CLOSE_OK, zlink_spot_node_destroy (&node));
    TEST_ASSERT_EQUAL (ZLINK_CLOSE_OK, zlink_close (stream_marker));
    TEST_ASSERT_EQUAL (ZLINK_CLOSE_OK, zlink_discovery_destroy (&discovery));
    TEST_ASSERT_EQUAL (ZLINK_CLOSE_OK, zlink_registry_destroy (&registry));
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
    TEST_ASSERT_EQUAL (ZLINK_CONFIG_OK,
                       zlink_get_routing_id (joined_spot, &joined_spot_rid));
    actor_probe_t joined_probe;
    TEST_ASSERT_EQUAL (ZLINK_HANDLER_OK,
                       zlink_spot_dispatch_event_handler (
                         joined_spot, on_join_only_dispatch, &joined_probe));
    void *actor = zlink_spot_node_actor_new (node, "snapshot-joined");
    TEST_ASSERT_NOT_NULL (actor);
    zlink_actor_ref_t actor_ref;
    TEST_ASSERT_EQUAL (ZLINK_CONFIG_OK,
                       zlink_actor_get_ref (actor, &actor_ref));
    void *joined_stream = zlink_socket (ctx, ZLINK_SOCKET_STREAM);
    TEST_ASSERT_NOT_NULL (joined_stream);
    zlink_routing_id_t joined_session_rid;
    set_rid (&joined_session_rid, "snapshot-joined-session");
    TEST_ASSERT_EQUAL (ZLINK_REQUEST_OK,
                       zlink_stream_bind_actor (node, joined_stream,
                                                &joined_session_rid,
                                                &actor_ref, 1000));
    zlink_msg_t join_msg;
    init_text_msg (&join_msg, "join");
    TEST_ASSERT_EQUAL (ZLINK_SUBMIT_OK,
                       zlink_actor_join_spot (actor, joined_spot, &join_msg,
                                              on_join_reply, &joined_probe,
                                              ZLINK_DONTWAIT, 1000));
    {
        std::unique_lock<std::mutex> lock (joined_probe.mutex);
        TEST_ASSERT_TRUE (joined_probe.cv.wait_for (
          lock, std::chrono::seconds (2),
          [&] { return joined_probe.join_done; }));
        TEST_ASSERT_EQUAL (ZLINK_REQUEST_OK, joined_probe.join_result);
    }

    zlink_spot_node_spot_entry_t row;
    TEST_ASSERT_TRUE (find_spot_snapshot_row (node, joined_spot_rid, &row));
    TEST_ASSERT_EQUAL_UINT32 (1, row.joined_actor_count);
    TEST_ASSERT_EQUAL (ZLINK_CLOSE_BUSY, zlink_spot_destroy (&joined_spot));
    TEST_ASSERT_EQUAL (ZLINK_CONFIG_OK,
                       zlink_actor_leave_spot (actor, joined_spot));
    TEST_ASSERT_TRUE (find_spot_snapshot_row (node, joined_spot_rid, &row));
    TEST_ASSERT_EQUAL_UINT32 (0, row.joined_actor_count);

    void *pending_spot = zlink_spot_new (node);
    TEST_ASSERT_NOT_NULL (pending_spot);
    zlink_routing_id_t pending_spot_rid;
    TEST_ASSERT_EQUAL (ZLINK_CONFIG_OK,
                       zlink_get_routing_id (pending_spot,
                                             &pending_spot_rid));
    void *pending_actor = zlink_spot_node_actor_new (node, "snapshot-pending");
    TEST_ASSERT_NOT_NULL (pending_actor);
    zlink_actor_ref_t pending_actor_ref;
    TEST_ASSERT_EQUAL (ZLINK_CONFIG_OK,
                       zlink_actor_get_ref (pending_actor,
                                            &pending_actor_ref));
    void *pending_stream = zlink_socket (ctx, ZLINK_SOCKET_STREAM);
    TEST_ASSERT_NOT_NULL (pending_stream);
    zlink_routing_id_t pending_session_rid;
    set_rid (&pending_session_rid, "snapshot-pending-session");
    TEST_ASSERT_EQUAL (ZLINK_REQUEST_OK,
                       zlink_stream_bind_actor (node, pending_stream,
                                                &pending_session_rid,
                                                &pending_actor_ref, 1000));
    actor_probe_t pending_probe;
    zlink_msg_t pending_join;
    init_text_msg (&pending_join, "pending");
    TEST_ASSERT_EQUAL (ZLINK_SUBMIT_OK,
                       zlink_actor_join_spot (pending_actor, pending_spot,
                                              &pending_join, on_join_reply,
                                              &pending_probe, ZLINK_DONTWAIT,
                                              5000));
    TEST_ASSERT_TRUE (find_spot_snapshot_row (node, pending_spot_rid, &row));
    TEST_ASSERT_EQUAL_UINT32 (1, row.pending_actor_join_count);
    TEST_ASSERT_EQUAL (ZLINK_CLOSE_BUSY, zlink_spot_destroy (&pending_spot));
    zlink_actor_join_info_t pending_info;
    zlink_msg_t pending_message;
    TEST_ASSERT_EQUAL (ZLINK_RECV_OK,
                       zlink_spot_actor_join_recv (
                         pending_spot, &pending_info, &pending_message,
                         ZLINK_RECV_FLAGS_DONTWAIT));
    zlink_msg_close (&pending_message);
    zlink_msg_t pending_reply;
    zlink_msg_init (&pending_reply);
    TEST_ASSERT_EQUAL (ZLINK_SUBMIT_OK,
                       zlink_spot_actor_join_reply (pending_spot,
                                                    &pending_info, 0u,
                                                    &pending_reply));
    {
        std::unique_lock<std::mutex> lock (pending_probe.mutex);
        TEST_ASSERT_TRUE (pending_probe.cv.wait_for (
          lock, std::chrono::seconds (2),
          [&] { return pending_probe.join_done; }));
        TEST_ASSERT_EQUAL (ZLINK_REQUEST_REJECTED, pending_probe.join_result);
    }
    TEST_ASSERT_EQUAL (ZLINK_CLOSE_OK, zlink_spot_destroy (&pending_spot));
    TEST_ASSERT_FALSE (find_spot_snapshot_row (node, pending_spot_rid, &row));

    TEST_ASSERT_EQUAL (ZLINK_REQUEST_OK, zlink_actor_destroy (&actor, 0));
    TEST_ASSERT_EQUAL (ZLINK_REQUEST_OK,
                       zlink_actor_destroy (&pending_actor, 0));
    TEST_ASSERT_EQUAL (ZLINK_CLOSE_OK, zlink_close (pending_stream));
    TEST_ASSERT_EQUAL (ZLINK_CLOSE_OK, zlink_close (joined_stream));
    TEST_ASSERT_EQUAL (ZLINK_CLOSE_OK, zlink_spot_destroy (&joined_spot));
    TEST_ASSERT_EQUAL (ZLINK_CLOSE_OK, zlink_spot_node_destroy (&node));
    TEST_ASSERT_EQUAL (ZLINK_CLOSE_OK, zlink_ctx_term (ctx));
}

void test_stream_bind_replaces_actor_id_entry ()
{
    void *ctx = zlink_ctx_new ();
    TEST_ASSERT_NOT_NULL (ctx);

    zlink_spot_node_options_t options;
    options.mode = ZLINK_SPOT_NODE_MODE_ALL;
    void *node_a = zlink_spot_node_new (ctx, &options);
    TEST_ASSERT_NOT_NULL (node_a);
    void *node_b = zlink_spot_node_new (ctx, &options);
    TEST_ASSERT_NOT_NULL (node_b);

    void *actor_a = zlink_spot_node_actor_new (node_a, "shared");
    TEST_ASSERT_NOT_NULL (actor_a);
    void *actor_b = zlink_spot_node_actor_new (node_b, "shared");
    TEST_ASSERT_NOT_NULL (actor_b);

    void *stream = zlink_socket (ctx, ZLINK_SOCKET_STREAM);
    TEST_ASSERT_NOT_NULL (stream);
    void *not_stream = zlink_socket (ctx, ZLINK_SOCKET_PAIR);
    TEST_ASSERT_NOT_NULL (not_stream);

    zlink_routing_id_t session_rid;
    set_rid (&session_rid, "session-rebind");
    zlink_actor_ref_t ref_a;
    zlink_actor_ref_t ref_b;
    TEST_ASSERT_EQUAL (ZLINK_CONFIG_OK, zlink_actor_get_ref (actor_a, &ref_a));
    TEST_ASSERT_EQUAL (ZLINK_CONFIG_OK, zlink_actor_get_ref (actor_b, &ref_b));

    TEST_ASSERT_EQUAL (ZLINK_REQUEST_INVALID_ARGUMENT,
                       zlink_stream_bind_actor (node_a, not_stream,
                                                &session_rid, &ref_a, 1000));

    TEST_ASSERT_EQUAL (ZLINK_REQUEST_OK,
                       zlink_stream_bind_actor (node_a, stream, &session_rid,
                                                &ref_a, 1000));
    TEST_ASSERT_EQUAL (ZLINK_REQUEST_OK,
                       zlink_stream_bind_actor (node_a, stream, &session_rid,
                                                &ref_b, 1000));
    zlink_actor_ref_t stale_remote_ref = ref_b;
    ++stale_remote_ref.generation;
    zlink_msg_t stale_remote_send;
    init_text_msg (&stale_remote_send, "drop-stale-remote");
    TEST_ASSERT_EQUAL (ZLINK_SUBMIT_OK,
                       zlink_spot_node_actor_send_bound_session_msg (
                         node_a, &stale_remote_ref, &stale_remote_send,
                         ZLINK_DONTWAIT));
    TEST_ASSERT_EQUAL_UINT (0, zlink_msg_size (&stale_remote_send));
    TEST_ASSERT_EQUAL (ZLINK_CONFIG_OK, zlink_msg_close (&stale_remote_send));
    zlink_routing_id_t missing_node_rid;
    set_rid (&missing_node_rid, "missing-bind-node");
    zlink_actor_ref_t missing_node_ref;
    TEST_ASSERT_EQUAL (ZLINK_CONFIG_OK,
                       zlink_remote_actor_get_ref (&missing_node_rid, "shared",
                                                   &missing_node_ref));
    TEST_ASSERT_EQUAL (ZLINK_REQUEST_NOT_CONNECTED,
                       zlink_stream_bind_actor (node_a, stream, &session_rid,
                                                &missing_node_ref, 1000));

    zlink_msg_t stale_send;
    init_text_msg (&stale_send, "stale");
    TEST_ASSERT_EQUAL (ZLINK_SUBMIT_NOT_FOUND,
                       zlink_actor_send_bound_session_msg (actor_a,
                                                          &stale_send,
                                                          ZLINK_DONTWAIT));
    TEST_ASSERT_EQUAL (ZLINK_CONFIG_OK, zlink_msg_close (&stale_send));

    zlink_msg_t part;
    init_text_msg (&part, "to-new-a");
    TEST_ASSERT_EQUAL (ZLINK_SUBMIT_OK,
                       zlink_stream_send_bound_actor_part (
                         node_a, stream, &session_rid, "shared", &part,
                         ZLINK_DONTWAIT, ZLINK_PART_MORE));
    zlink_msg_t final_part;
    init_text_msg (&final_part, "to-new-b");
    TEST_ASSERT_EQUAL (ZLINK_SUBMIT_OK,
                       zlink_stream_send_bound_actor_part (
                         node_a, stream, &session_rid, "shared", &final_part,
                         ZLINK_DONTWAIT, ZLINK_PART_FINAL));

    zlink_spot_node_actor_entry_t rows[1];
    size_t count = 1;
    TEST_ASSERT_EQUAL (ZLINK_CONFIG_OK,
                       zlink_spot_node_actors_snapshot (node_b, rows, &count));
    TEST_ASSERT_EQUAL_UINT (1, count);
    TEST_ASSERT_EQUAL_UINT32 (2, rows[0].pending_message_count);

    TEST_ASSERT_EQUAL (ZLINK_REQUEST_OK, zlink_actor_destroy (&actor_a, 0));
    TEST_ASSERT_EQUAL (ZLINK_REQUEST_OK, zlink_actor_destroy (&actor_b, 0));
    TEST_ASSERT_EQUAL (ZLINK_CLOSE_OK, zlink_close (not_stream));
    TEST_ASSERT_EQUAL (ZLINK_CLOSE_OK, zlink_close (stream));
    TEST_ASSERT_EQUAL (ZLINK_CLOSE_OK, zlink_spot_node_destroy (&node_b));
    TEST_ASSERT_EQUAL (ZLINK_CLOSE_OK, zlink_spot_node_destroy (&node_a));
    TEST_ASSERT_EQUAL (ZLINK_CLOSE_OK, zlink_ctx_term (ctx));
}

void test_actor_send_bound_session_owner_not_connected ()
{
    void *ctx = zlink_ctx_new ();
    TEST_ASSERT_NOT_NULL (ctx);

    zlink_spot_node_options_t options;
    options.mode = ZLINK_SPOT_NODE_MODE_ALL;
    void *session_owner = zlink_spot_node_new (ctx, &options);
    TEST_ASSERT_NOT_NULL (session_owner);
    void *actor_owner = zlink_spot_node_new (ctx, &options);
    TEST_ASSERT_NOT_NULL (actor_owner);

    void *actor = zlink_spot_node_actor_new (actor_owner, "owner-gone");
    TEST_ASSERT_NOT_NULL (actor);
    zlink_actor_ref_t ref;
    TEST_ASSERT_EQUAL (ZLINK_CONFIG_OK, zlink_actor_get_ref (actor, &ref));

    void *stream = zlink_socket (ctx, ZLINK_SOCKET_STREAM);
    TEST_ASSERT_NOT_NULL (stream);
    zlink_routing_id_t session_rid;
    set_rid (&session_rid, "owner-gone-session");
    TEST_ASSERT_EQUAL (ZLINK_REQUEST_OK,
                       zlink_stream_bind_actor (session_owner, stream,
                                                &session_rid, &ref, 1000));

    TEST_ASSERT_EQUAL (ZLINK_CLOSE_OK,
                       zlink_spot_node_destroy (&session_owner));

    zlink_msg_t msg;
    init_text_msg (&msg, "after-owner-gone");
    TEST_ASSERT_EQUAL (ZLINK_SUBMIT_NOT_CONNECTED,
                       zlink_actor_send_bound_session_msg (actor, &msg,
                                                          ZLINK_DONTWAIT));
    TEST_ASSERT_EQUAL_STRING ("after-owner-gone", msg_text (&msg).c_str ());
    TEST_ASSERT_EQUAL (ZLINK_CONFIG_OK, zlink_msg_close (&msg));

    TEST_ASSERT_EQUAL (ZLINK_REQUEST_OK, zlink_actor_destroy (&actor, 0));
    TEST_ASSERT_EQUAL (ZLINK_CLOSE_OK, zlink_close (stream));
    TEST_ASSERT_EQUAL (ZLINK_CLOSE_OK, zlink_spot_node_destroy (&actor_owner));
    TEST_ASSERT_EQUAL (ZLINK_CLOSE_OK, zlink_ctx_term (ctx));
}

void test_stream_remote_actor_owner_not_connected_and_unbind_cleanup ()
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
                       zlink_stream_bind_actor (session_owner, stream,
                                                &session_rid, &ref, 1000));

    TEST_ASSERT_EQUAL (ZLINK_CLOSE_OK, zlink_spot_node_destroy (&actor_owner));

    zlink_msg_t part;
    init_text_msg (&part, "still-owned");
    TEST_ASSERT_EQUAL (ZLINK_SUBMIT_NOT_CONNECTED,
                       zlink_stream_send_bound_actor_part (
                         session_owner, stream, &session_rid, "remote-gone",
                         &part, ZLINK_DONTWAIT, ZLINK_PART_FINAL));
    TEST_ASSERT_EQUAL_STRING ("still-owned", msg_text (&part).c_str ());
    TEST_ASSERT_EQUAL (ZLINK_CONFIG_OK, zlink_msg_close (&part));

    TEST_ASSERT_EQUAL (ZLINK_REQUEST_OK,
                       zlink_stream_unbind_actor (session_owner, stream,
                                                  &session_rid, "remote-gone",
                                                  1000));
    init_text_msg (&part, "gone");
    TEST_ASSERT_EQUAL (ZLINK_SUBMIT_NOT_FOUND,
                       zlink_stream_send_bound_actor_part (
                         session_owner, stream, &session_rid, "remote-gone",
                         &part, ZLINK_DONTWAIT, ZLINK_PART_FINAL));
    TEST_ASSERT_EQUAL (ZLINK_CONFIG_OK, zlink_msg_close (&part));

    TEST_ASSERT_EQUAL (ZLINK_CLOSE_OK, zlink_close (stream));
    TEST_ASSERT_EQUAL (ZLINK_CLOSE_OK,
                       zlink_spot_node_destroy (&session_owner));
    TEST_ASSERT_EQUAL (ZLINK_CLOSE_OK, zlink_ctx_term (ctx));
}

void test_stream_unbind_not_connected_keeps_binding ()
{
    void *ctx = zlink_ctx_new ();
    TEST_ASSERT_NOT_NULL (ctx);

    zlink_spot_node_options_t options;
    options.mode = ZLINK_SPOT_NODE_MODE_ALL;
    void *session_owner = zlink_spot_node_new (ctx, &options);
    TEST_ASSERT_NOT_NULL (session_owner);
    void *actor_owner = zlink_spot_node_new (ctx, &options);
    TEST_ASSERT_NOT_NULL (actor_owner);

    zlink_routing_id_t actor_owner_rid;
    TEST_ASSERT_EQUAL (ZLINK_CONFIG_OK,
                       zlink_get_routing_id (actor_owner, &actor_owner_rid));
    void *actor = zlink_spot_node_actor_new (actor_owner, "unbind-lost");
    TEST_ASSERT_NOT_NULL (actor);
    zlink_actor_ref_t ref;
    TEST_ASSERT_EQUAL (ZLINK_CONFIG_OK, zlink_actor_get_ref (actor, &ref));

    void *stream = zlink_socket (ctx, ZLINK_SOCKET_STREAM);
    TEST_ASSERT_NOT_NULL (stream);
    zlink_routing_id_t session_rid;
    set_rid (&session_rid, "unbind-lost-session");
    TEST_ASSERT_EQUAL (ZLINK_REQUEST_OK,
                       zlink_stream_bind_actor (session_owner, stream,
                                                &session_rid, &ref, 1000));

    (void) zlink_spot_node_disconnect_peer_rid (session_owner,
                                                &actor_owner_rid);
    TEST_ASSERT_EQUAL (ZLINK_REQUEST_NOT_CONNECTED,
                       zlink_stream_unbind_actor (session_owner, stream,
                                                  &session_rid, "unbind-lost",
                                                  1000));
    zlink_msg_t part;
    init_text_msg (&part, "still-bound");
    TEST_ASSERT_EQUAL (ZLINK_SUBMIT_NOT_CONNECTED,
                       zlink_stream_send_bound_actor_part (
                         session_owner, stream, &session_rid, "unbind-lost",
                         &part, ZLINK_DONTWAIT, ZLINK_PART_FINAL));
    TEST_ASSERT_EQUAL_STRING ("still-bound", msg_text (&part).c_str ());
    TEST_ASSERT_EQUAL (ZLINK_CONFIG_OK, zlink_msg_close (&part));

    TEST_ASSERT_EQUAL (ZLINK_REQUEST_OK, zlink_actor_destroy (&actor, 1000));
    TEST_ASSERT_EQUAL (ZLINK_CLOSE_OK, zlink_close (stream));
    TEST_ASSERT_EQUAL (ZLINK_CLOSE_OK, zlink_spot_node_destroy (&actor_owner));
    TEST_ASSERT_EQUAL (ZLINK_CLOSE_OK,
                       zlink_spot_node_destroy (&session_owner));
    TEST_ASSERT_EQUAL (ZLINK_CLOSE_OK, zlink_ctx_term (ctx));
}

void test_stream_remote_target_actor_missing_drops_after_forward ()
{
    void *ctx = zlink_ctx_new ();
    TEST_ASSERT_NOT_NULL (ctx);

    zlink_spot_node_options_t options;
    options.mode = ZLINK_SPOT_NODE_MODE_ALL;
    void *session_owner = zlink_spot_node_new (ctx, &options);
    TEST_ASSERT_NOT_NULL (session_owner);
    void *actor_owner = zlink_spot_node_new (ctx, &options);
    TEST_ASSERT_NOT_NULL (actor_owner);

    zlink_routing_id_t actor_owner_rid;
    TEST_ASSERT_EQUAL (ZLINK_CONFIG_OK,
                       zlink_get_routing_id (actor_owner, &actor_owner_rid));
    void *actor = zlink_spot_node_actor_new (actor_owner, "drop-missing");
    TEST_ASSERT_NOT_NULL (actor);
    zlink_actor_ref_t ref;
    TEST_ASSERT_EQUAL (ZLINK_CONFIG_OK, zlink_actor_get_ref (actor, &ref));

    void *stream = zlink_socket (ctx, ZLINK_SOCKET_STREAM);
    TEST_ASSERT_NOT_NULL (stream);
    zlink_routing_id_t session_rid;
    set_rid (&session_rid, "drop-missing-session");
    TEST_ASSERT_EQUAL (ZLINK_REQUEST_OK,
                       zlink_stream_bind_actor (session_owner, stream,
                                                &session_rid, &ref, 1000));

    TEST_ASSERT_EQUAL (ZLINK_CLOSE_OK, zlink_spot_node_destroy (&actor_owner));

    void *replacement = zlink_spot_node_new (ctx, &options);
    TEST_ASSERT_NOT_NULL (replacement);
    TEST_ASSERT_EQUAL (ZLINK_CONFIG_OK,
                       zlink_set_routing_id (replacement,
                                             actor_owner_rid.data,
                                             actor_owner_rid.size));
    void *other_actor = zlink_spot_node_actor_new (replacement, "other-actor");
    TEST_ASSERT_NOT_NULL (other_actor);

    zlink_msg_t part;
    init_text_msg (&part, "drop-me");
    TEST_ASSERT_EQUAL (ZLINK_SUBMIT_OK,
                       zlink_stream_send_bound_actor_part (
                         session_owner, stream, &session_rid, "drop-missing",
                         &part, ZLINK_DONTWAIT, ZLINK_PART_FINAL));
    TEST_ASSERT_EQUAL_UINT (0, zlink_msg_size (&part));

    zlink_spot_node_actor_entry_t row;
    size_t count = 1;
    TEST_ASSERT_EQUAL (ZLINK_CONFIG_OK,
                       zlink_spot_node_actors_snapshot (replacement, &row,
                                                        &count));
    TEST_ASSERT_EQUAL_UINT (1, count);
    TEST_ASSERT_EQUAL_STRING ("other-actor", row.actor.actor_id);
    TEST_ASSERT_EQUAL_UINT32 (0, row.pending_message_count);

    TEST_ASSERT_EQUAL (ZLINK_REQUEST_OK,
                       zlink_actor_destroy (&other_actor, 1000));
    TEST_ASSERT_EQUAL (ZLINK_CLOSE_OK, zlink_spot_node_destroy (&replacement));
    TEST_ASSERT_EQUAL (ZLINK_CLOSE_OK, zlink_close (stream));
    TEST_ASSERT_EQUAL (ZLINK_CLOSE_OK,
                       zlink_spot_node_destroy (&session_owner));
    TEST_ASSERT_EQUAL (ZLINK_CLOSE_OK, zlink_ctx_term (ctx));
}

void test_actor_request_timeout_atomicity_under_control_lock ()
{
    void *ctx = zlink_ctx_new ();
    TEST_ASSERT_NOT_NULL (ctx);

    zlink_spot_node_options_t options;
    options.mode = ZLINK_SPOT_NODE_MODE_ALL;
    void *caller = zlink_spot_node_new (ctx, &options);
    TEST_ASSERT_NOT_NULL (caller);
    void *target = zlink_spot_node_new (ctx, &options);
    TEST_ASSERT_NOT_NULL (target);

    zlink_routing_id_t target_rid;
    TEST_ASSERT_EQUAL (ZLINK_CONFIG_OK,
                       zlink_get_routing_id (target, &target_rid));

    void *actor = zlink_spot_node_actor_new (target, "timeout-actor");
    TEST_ASSERT_NOT_NULL (actor);
    zlink_actor_ref_t ref;
    TEST_ASSERT_EQUAL (ZLINK_CONFIG_OK, zlink_actor_get_ref (actor, &ref));

    admission_probe_t admission;
    admission.sleep_ms = 200;
    TEST_ASSERT_EQUAL (ZLINK_HANDLER_OK,
                       zlink_spot_node_actor_admission_handler (
                         target, on_actor_admission, &admission));

    zlink_request_result_t create_rc = ZLINK_REQUEST_INTERNAL_ERROR;
    zlink_actor_create_result_t create_result;
    std::thread holder ([&] {
        zlink_msg_t create_msg;
        init_text_msg (&create_msg, "hold-lock");
        create_rc = zlink_spot_node_create_remote_actor (
          caller, &target_rid, "timeout-holder", &create_msg, &create_result,
          1000);
    });

    {
        std::unique_lock<std::mutex> lock (admission.mutex);
        TEST_ASSERT_TRUE (admission.cv.wait_for (
          lock, std::chrono::seconds (2), [&] { return admission.entered; }));
    }

    void *destroy_candidate = actor;
    TEST_ASSERT_EQUAL (ZLINK_REQUEST_TIMED_OUT,
                       zlink_actor_destroy (&destroy_candidate, 0));
    TEST_ASSERT_EQUAL_PTR (actor, destroy_candidate);
    TEST_ASSERT_EQUAL (ZLINK_REQUEST_TIMED_OUT,
                       zlink_actor_destroy (&destroy_candidate, 10));
    TEST_ASSERT_EQUAL_PTR (actor, destroy_candidate);

    void *stream = zlink_socket (ctx, ZLINK_SOCKET_STREAM);
    TEST_ASSERT_NOT_NULL (stream);
    zlink_routing_id_t session_rid;
    set_rid (&session_rid, "timeout-session");
    TEST_ASSERT_EQUAL (ZLINK_REQUEST_TIMED_OUT,
                       zlink_stream_bind_actor (caller, stream, &session_rid,
                                                &ref, 0));
    TEST_ASSERT_EQUAL (ZLINK_REQUEST_TIMED_OUT,
                       zlink_stream_bind_actor (caller, stream, &session_rid,
                                                &ref, 10));
    zlink_msg_t create_nowait_msg;
    init_text_msg (&create_nowait_msg, "nowait-create");
    zlink_actor_create_result_t create_nowait_result;
    TEST_ASSERT_EQUAL (ZLINK_REQUEST_TIMED_OUT,
                       zlink_spot_node_create_remote_actor (
                         caller, &target_rid, "timeout-nowait",
                         &create_nowait_msg, &create_nowait_result, 0));
    TEST_ASSERT_EQUAL_STRING ("nowait-create",
                              msg_text (&create_nowait_msg).c_str ());
    TEST_ASSERT_EQUAL (ZLINK_CONFIG_OK, zlink_msg_close (&create_nowait_msg));
    TEST_ASSERT_EQUAL (ZLINK_REQUEST_TIMED_OUT,
                       zlink_spot_node_destroy_remote_actor (caller, &ref, 0));
    TEST_ASSERT_EQUAL (ZLINK_REQUEST_TIMED_OUT,
                       zlink_spot_node_destroy_remote_actor (caller, &ref, 10));

    holder.join ();
    TEST_ASSERT_EQUAL (ZLINK_REQUEST_OK, create_rc);

    TEST_ASSERT_EQUAL (ZLINK_REQUEST_OK,
                       zlink_stream_bind_actor (caller, stream, &session_rid,
                                                &ref, 1000));

    admission.entered = false;
    std::thread holder2 ([&] {
        zlink_msg_t create_msg;
        init_text_msg (&create_msg, "hold-lock-2");
        zlink_actor_create_result_t ignored;
        (void) zlink_spot_node_create_remote_actor (
          caller, &target_rid, "timeout-holder-2", &create_msg, &ignored,
          1000);
    });
    {
        std::unique_lock<std::mutex> lock (admission.mutex);
        TEST_ASSERT_TRUE (admission.cv.wait_for (
          lock, std::chrono::seconds (2), [&] { return admission.entered; }));
    }
    TEST_ASSERT_EQUAL (ZLINK_REQUEST_TIMED_OUT,
                       zlink_stream_unbind_actor (caller, stream, &session_rid,
                                                  "timeout-actor", 10));
    holder2.join ();

    zlink_msg_t part;
    init_text_msg (&part, "still-bound");
    TEST_ASSERT_EQUAL (ZLINK_SUBMIT_OK,
                       zlink_stream_send_bound_actor_part (
                         caller, stream, &session_rid, "timeout-actor", &part,
                         ZLINK_DONTWAIT, ZLINK_PART_FINAL));
    size_t count = 0;
    TEST_ASSERT_EQUAL (ZLINK_CONFIG_OK,
                       zlink_spot_node_actors_snapshot (target, NULL, &count));
    std::vector<zlink_spot_node_actor_entry_t> rows (count);
    TEST_ASSERT_EQUAL (ZLINK_CONFIG_OK,
                       zlink_spot_node_actors_snapshot (target, rows.data (),
                                                        &count));
    uint32_t timeout_actor_pending = 0;
    for (size_t i = 0; i < count; ++i) {
        if (strncmp (rows[i].actor.actor_id, "timeout-actor",
                     ZLINK_ACTOR_ID_MAX)
            == 0)
            timeout_actor_pending = rows[i].pending_message_count;
    }
    TEST_ASSERT_EQUAL_UINT32 (1, timeout_actor_pending);

    TEST_ASSERT_EQUAL (ZLINK_REQUEST_OK,
                       zlink_stream_unbind_actor (caller, stream, &session_rid,
                                                  "timeout-actor", 1000));
    TEST_ASSERT_EQUAL (ZLINK_REQUEST_OK, zlink_actor_destroy (&actor, 1000));
    TEST_ASSERT_EQUAL (ZLINK_REQUEST_OK,
                       zlink_spot_node_destroy_remote_actor (
                         caller, &create_result.actor, 1000));
    TEST_ASSERT_EQUAL (ZLINK_CLOSE_OK, zlink_close (stream));
    TEST_ASSERT_EQUAL (ZLINK_CLOSE_OK, zlink_spot_node_destroy (&target));
    TEST_ASSERT_EQUAL (ZLINK_CLOSE_OK, zlink_spot_node_destroy (&caller));
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
                       zlink_stream_bind_actor (node, stream, &session_rid,
                                                &ref_a, 1000));
    TEST_ASSERT_EQUAL (ZLINK_REQUEST_OK,
                       zlink_stream_bind_actor (node, stream, &session_rid,
                                                &ref_b, 1000));

    zlink_msg_t missing;
    init_text_msg (&missing, "missing");
    TEST_ASSERT_EQUAL (ZLINK_SUBMIT_NOT_FOUND,
                       zlink_stream_send_bound_actor_part (
                         node, stream, &session_rid, "missing", &missing,
                         ZLINK_DONTWAIT, ZLINK_PART_FINAL));
    TEST_ASSERT_EQUAL (ZLINK_CONFIG_OK, zlink_msg_close (&missing));

    zlink_msg_t first;
    init_text_msg (&first, "first");
    TEST_ASSERT_EQUAL (ZLINK_SUBMIT_OK,
                       zlink_stream_send_bound_actor_part (
                         node, stream, &session_rid, "multi-a", &first,
                         ZLINK_DONTWAIT, ZLINK_PART_MORE));
    void *destroy_candidate = actor_a;
    TEST_ASSERT_EQUAL (ZLINK_REQUEST_BUSY,
                       zlink_actor_destroy (&destroy_candidate, 1000));
    TEST_ASSERT_EQUAL_PTR (actor_a, destroy_candidate);

    zlink_msg_t wrong_final;
    init_text_msg (&wrong_final, "wrong");
    TEST_ASSERT_EQUAL (ZLINK_SUBMIT_INVALID_STATE,
                       zlink_stream_send_bound_actor_part (
                         node, stream, &session_rid, "multi-b", &wrong_final,
                         ZLINK_DONTWAIT, ZLINK_PART_FINAL));
    TEST_ASSERT_EQUAL (ZLINK_CONFIG_OK, zlink_msg_close (&wrong_final));

    zlink_msg_t final;
    init_text_msg (&final, "final");
    TEST_ASSERT_EQUAL (ZLINK_SUBMIT_OK,
                       zlink_stream_send_bound_actor_part (
                         node, stream, &session_rid, "multi-a", &final,
                         ZLINK_DONTWAIT, ZLINK_PART_FINAL));

    zlink_spot_node_actor_entry_t rows[2];
    size_t count = 2;
    TEST_ASSERT_EQUAL (ZLINK_CONFIG_OK,
                       zlink_spot_node_actors_snapshot (node, rows, &count));
    TEST_ASSERT_EQUAL_UINT (2, count);
    uint32_t pending_a = 0;
    uint32_t pending_b = 0;
    for (size_t i = 0; i < 2; ++i) {
        if (strncmp (rows[i].actor.actor_id, "multi-a",
                     ZLINK_ACTOR_ID_MAX)
            == 0)
            pending_a = rows[i].pending_message_count;
        if (strncmp (rows[i].actor.actor_id, "multi-b",
                     ZLINK_ACTOR_ID_MAX)
            == 0)
            pending_b = rows[i].pending_message_count;
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
                       zlink_stream_bind_actor (node, stream, &session_rid,
                                                &ref, 1000));

    actor_probe_t probe;
    probe.try_destroy_in_actor_callback = true;
    TEST_ASSERT_EQUAL (ZLINK_HANDLER_OK,
                       zlink_spot_dispatch_event_handler (spot, on_dispatch,
                                                          &probe));

    zlink_msg_t join_msg;
    init_text_msg (&join_msg, "join");
    TEST_ASSERT_EQUAL (ZLINK_SUBMIT_OK,
                       zlink_actor_join_spot (actor, spot, &join_msg,
                                              on_join_reply, &probe,
                                              ZLINK_DONTWAIT, 1000));
    {
        std::unique_lock<std::mutex> lock (probe.mutex);
        TEST_ASSERT_TRUE (probe.cv.wait_for (
          lock, std::chrono::seconds (2), [&] { return probe.join_done; }));
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
                       zlink_actor_recv_part (actor, &outside_info,
                                              &outside_part, &outside_flag,
                                              ZLINK_RECV_FLAGS_DONTWAIT));

    zlink_msg_t first;
    init_text_msg (&first, "first");
    TEST_ASSERT_EQUAL (ZLINK_SUBMIT_OK,
                       zlink_stream_send_bound_actor_part (
                         node, stream, &session_rid, "queue-dispatch", &first,
                         ZLINK_DONTWAIT, ZLINK_PART_MORE));
    {
        std::unique_lock<std::mutex> lock (probe.mutex);
        TEST_ASSERT_TRUE (probe.cv.wait_for (
          lock, std::chrono::seconds (2),
          [&] { return probe.actor_recv_count >= 1 || probe.failed; }));
        TEST_ASSERT_FALSE (probe.failed);
        TEST_ASSERT_TRUE (probe.actor_subject_ok);
        TEST_ASSERT_TRUE (probe.actor_no_data_after_drain);
        TEST_ASSERT_TRUE (probe.destroy_in_actor_callback_blocked);
        TEST_ASSERT_EQUAL (ZLINK_PART_MORE, probe.first_part_flag);
        TEST_ASSERT_EQUAL_STRING ("first", probe.payload.c_str ());
    }

    zlink_msg_t final;
    init_text_msg (&final, "final");
    TEST_ASSERT_EQUAL (ZLINK_SUBMIT_OK,
                       zlink_stream_send_bound_actor_part (
                         node, stream, &session_rid, "queue-dispatch", &final,
                         ZLINK_DONTWAIT, ZLINK_PART_FINAL));
    {
        std::unique_lock<std::mutex> lock (probe.mutex);
        TEST_ASSERT_TRUE (probe.cv.wait_for (
          lock, std::chrono::seconds (2),
          [&] { return probe.actor_recv_count >= 2 || probe.failed; }));
        TEST_ASSERT_FALSE (probe.failed);
        TEST_ASSERT_EQUAL_STRING ("firstfinal", probe.payload.c_str ());
    }

    void *backpressure_actor =
      zlink_spot_node_actor_new (node, "queue-backpressure");
    TEST_ASSERT_NOT_NULL (backpressure_actor);
    zlink_actor_ref_t backpressure_ref;
    TEST_ASSERT_EQUAL (ZLINK_CONFIG_OK,
                       zlink_actor_get_ref (backpressure_actor,
                                            &backpressure_ref));
    TEST_ASSERT_EQUAL (ZLINK_REQUEST_OK,
                       zlink_stream_bind_actor (node, stream, &session_rid,
                                                &backpressure_ref, 1000));
    for (size_t i = 0; i < 1024; ++i) {
        zlink_msg_t msg;
        init_text_msg (&msg, "x");
        TEST_ASSERT_EQUAL (ZLINK_SUBMIT_OK,
                           zlink_stream_send_bound_actor_part (
                             node, stream, &session_rid, "queue-backpressure",
                             &msg, ZLINK_DONTWAIT, ZLINK_PART_FINAL));
    }
    zlink_msg_t over_limit;
    init_text_msg (&over_limit, "owned-by-caller");
    TEST_ASSERT_EQUAL (ZLINK_SUBMIT_OK,
                       zlink_stream_send_bound_actor_part (
                         node, stream, &session_rid, "queue-backpressure",
                         &over_limit, ZLINK_DONTWAIT, ZLINK_PART_FINAL));

    void *cleanup_actor = zlink_spot_node_actor_new (node, "queue-cleanup");
    TEST_ASSERT_NOT_NULL (cleanup_actor);
    zlink_actor_ref_t cleanup_ref;
    TEST_ASSERT_EQUAL (ZLINK_CONFIG_OK,
                       zlink_actor_get_ref (cleanup_actor, &cleanup_ref));
    TEST_ASSERT_EQUAL (ZLINK_REQUEST_OK,
                       zlink_stream_bind_actor (node, stream, &session_rid,
                                                &cleanup_ref, 1000));
    zlink_msg_t incomplete;
    init_text_msg (&incomplete, "incomplete");
    TEST_ASSERT_EQUAL (ZLINK_SUBMIT_OK,
                       zlink_stream_send_bound_actor_part (
                         node, stream, &session_rid, "queue-cleanup",
                         &incomplete, ZLINK_DONTWAIT, ZLINK_PART_MORE));
    zlink_msg_t complete_cleanup;
    init_text_msg (&complete_cleanup, "complete");
    TEST_ASSERT_EQUAL (ZLINK_SUBMIT_OK,
                       zlink_stream_send_bound_actor_part (
                         node, stream, &session_rid, "queue-cleanup",
                         &complete_cleanup, ZLINK_DONTWAIT, ZLINK_PART_FINAL));
    TEST_ASSERT_EQUAL (ZLINK_REQUEST_OK,
                       zlink_actor_destroy (&cleanup_actor, 0));

    TEST_ASSERT_EQUAL (ZLINK_CONFIG_OK, zlink_actor_leave_spot (actor, spot));
    TEST_ASSERT_EQUAL (ZLINK_REQUEST_OK, zlink_actor_destroy (&actor, 0));
    TEST_ASSERT_EQUAL (ZLINK_REQUEST_OK,
                       zlink_actor_destroy (&backpressure_actor, 0));
    TEST_ASSERT_EQUAL (ZLINK_CLOSE_OK, zlink_close (stream));
    TEST_ASSERT_EQUAL (ZLINK_CLOSE_OK, zlink_spot_destroy (&spot));
    TEST_ASSERT_EQUAL (ZLINK_CLOSE_OK, zlink_spot_node_destroy (&node));
    TEST_ASSERT_EQUAL (ZLINK_CLOSE_OK, zlink_ctx_term (ctx));
}

void test_actor_route_move_joined_publish_and_provider_cleanup ()
{
    void *ctx = zlink_ctx_new ();
    TEST_ASSERT_NOT_NULL (ctx);

    void *discovery_a =
      zlink_discovery_new (ctx, ZLINK_AUTO_CONNECT_SPOT_MESH, "route-move-a");
    void *discovery_b =
      zlink_discovery_new (ctx, ZLINK_AUTO_CONNECT_SPOT_MESH, "route-move-b");
    TEST_ASSERT_NOT_NULL (discovery_a);
    TEST_ASSERT_NOT_NULL (discovery_b);
    int enabled = 1;
    TEST_ASSERT_EQUAL (ZLINK_CONFIG_OK,
                       zlink_set_option (discovery_a,
                                         ZLINK_OPT_DISCOVERY_ACTOR_ROUTE_SYNC,
                                         &enabled, sizeof (enabled)));
    TEST_ASSERT_EQUAL (ZLINK_CONFIG_OK,
                       zlink_set_option (discovery_b,
                                         ZLINK_OPT_DISCOVERY_ACTOR_ROUTE_SYNC,
                                         &enabled, sizeof (enabled)));

    zlink_spot_node_options_t options;
    options.mode = ZLINK_SPOT_NODE_MODE_ALL;
    void *node_a = zlink_spot_node_new (ctx, &options);
    void *node_b = zlink_spot_node_new (ctx, &options);
    TEST_ASSERT_NOT_NULL (node_a);
    TEST_ASSERT_NOT_NULL (node_b);
    TEST_ASSERT_EQUAL (ZLINK_CONFIG_OK,
                       zlink_spot_node_attach_discovery (node_a, discovery_a));
    TEST_ASSERT_EQUAL (ZLINK_CONFIG_OK,
                       zlink_spot_node_attach_discovery (node_b, discovery_b));

    void *actor_a = zlink_spot_node_actor_new (node_a, "route-move");
    void *actor_b = zlink_spot_node_actor_new (node_b, "route-move");
    TEST_ASSERT_NOT_NULL (actor_a);
    TEST_ASSERT_NOT_NULL (actor_b);
    void *spot_b = zlink_spot_new (node_b);
    TEST_ASSERT_NOT_NULL (spot_b);

    actor_probe_t probe;
    TEST_ASSERT_EQUAL (ZLINK_HANDLER_OK,
                       zlink_spot_dispatch_event_handler (spot_b, on_dispatch,
                                                          &probe));

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
    TEST_ASSERT_EQUAL (ZLINK_CONFIG_INVALID_ARGUMENT,
                       zlink_discovery_resolve_actor (
                         discovery_a, "route-move", &route));
    TEST_ASSERT_EQUAL (ENOENT, zlink_errno ());

    TEST_ASSERT_EQUAL (ZLINK_REQUEST_OK,
                       zlink_stream_bind_actor (node_a, stream, &session_b,
                                                &ref_b, 1000));
    zlink_msg_t join_msg;
    init_text_msg (&join_msg, "join-after-bind");
    TEST_ASSERT_EQUAL (ZLINK_SUBMIT_OK,
                       zlink_actor_join_spot (actor_b, spot_b, &join_msg,
                                              on_join_reply, &probe,
                                              ZLINK_DONTWAIT, 1000));
    {
        std::unique_lock<std::mutex> lock (probe.mutex);
        TEST_ASSERT_TRUE (probe.cv.wait_for (
          lock, std::chrono::seconds (2), [&] { return probe.join_done; }));
        TEST_ASSERT_EQUAL (ZLINK_REQUEST_OK, probe.join_result);
    }

    TEST_ASSERT_EQUAL (ZLINK_REQUEST_OK,
                       zlink_stream_bind_actor (node_a, stream, &session_a,
                                                &ref_a, 1000));
    TEST_ASSERT_EQUAL (ZLINK_CONFIG_OK,
                       zlink_discovery_resolve_actor (
                         discovery_a, "route-move", &route));
    TEST_ASSERT_EQUAL_UINT64 (ref_a.generation, route.actor.generation);
    TEST_ASSERT_EQUAL_UINT32 (1, route.joined);

    TEST_ASSERT_EQUAL (ZLINK_REQUEST_OK,
                       zlink_stream_bind_actor (node_a, stream, &session_b,
                                                &ref_b, 1000));
    TEST_ASSERT_EQUAL (ZLINK_CONFIG_OK,
                       zlink_discovery_resolve_actor (
                         discovery_a, "route-move", &route));
    TEST_ASSERT_EQUAL_UINT64 (ref_b.generation, route.actor.generation);
    TEST_ASSERT_EQUAL_UINT32 (1, route.joined);

    TEST_ASSERT_EQUAL (ZLINK_REQUEST_OK, zlink_actor_destroy (&actor_a, 0));
    TEST_ASSERT_EQUAL (ZLINK_CONFIG_OK,
                       zlink_discovery_resolve_actor (
                         discovery_a, "route-move", &route));
    TEST_ASSERT_EQUAL_UINT64 (ref_b.generation, route.actor.generation);

    TEST_ASSERT_EQUAL (ZLINK_CONFIG_OK, zlink_actor_leave_spot (actor_b, spot_b));
    TEST_ASSERT_EQUAL (ZLINK_CONFIG_OK,
                       zlink_discovery_resolve_actor (
                         discovery_a, "route-move", &route));
    TEST_ASSERT_EQUAL_UINT32 (1, route.joined);

    TEST_ASSERT_EQUAL (ZLINK_CLOSE_OK, zlink_spot_destroy (&spot_b));
    TEST_ASSERT_EQUAL (ZLINK_CLOSE_OK, zlink_spot_node_destroy (&node_b));
    TEST_ASSERT_EQUAL (ZLINK_CONFIG_INVALID_ARGUMENT,
                       zlink_discovery_resolve_actor (
                         discovery_a, "route-move", &route));
    TEST_ASSERT_EQUAL (ENOENT, zlink_errno ());

    TEST_ASSERT_EQUAL (ZLINK_CLOSE_OK, zlink_close (stream));
    TEST_ASSERT_EQUAL (ZLINK_CLOSE_OK, zlink_spot_node_destroy (&node_a));
    TEST_ASSERT_EQUAL (ZLINK_CLOSE_OK, zlink_discovery_destroy (&discovery_b));
    TEST_ASSERT_EQUAL (ZLINK_CLOSE_OK, zlink_discovery_destroy (&discovery_a));
    TEST_ASSERT_EQUAL (ZLINK_CLOSE_OK, zlink_ctx_term (ctx));
}

void test_remote_create_existing_and_destroy_generation_rules ()
{
    void *ctx = zlink_ctx_new ();
    TEST_ASSERT_NOT_NULL (ctx);

    zlink_spot_node_options_t options;
    options.mode = ZLINK_SPOT_NODE_MODE_ALL;
    void *caller = zlink_spot_node_new (ctx, &options);
    TEST_ASSERT_NOT_NULL (caller);
    void *target = zlink_spot_node_new (ctx, &options);
    TEST_ASSERT_NOT_NULL (target);

    zlink_routing_id_t target_rid;
    TEST_ASSERT_EQUAL (ZLINK_CONFIG_OK,
                       zlink_get_routing_id (target, &target_rid));

    zlink_msg_t create_msg;
    zlink_actor_create_result_t create_result;
    init_text_msg (&create_msg, "invalid-create");
    TEST_ASSERT_EQUAL (ZLINK_REQUEST_INVALID_ARGUMENT,
                       zlink_spot_node_create_remote_actor (
                         caller, NULL, "invalid-create", &create_msg,
                         &create_result, 1000));
    TEST_ASSERT_EQUAL_STRING ("invalid-create", msg_text (&create_msg).c_str ());
    TEST_ASSERT_EQUAL (ZLINK_CONFIG_OK, zlink_msg_close (&create_msg));

    init_text_msg (&create_msg, "default-reject");
    TEST_ASSERT_EQUAL (ZLINK_REQUEST_REJECTED,
                       zlink_spot_node_create_remote_actor (
                         caller, &target_rid, "remote-default-reject",
                         &create_msg, &create_result, 1000));
    TEST_ASSERT_EQUAL_UINT (0, zlink_msg_size (&create_msg));

    admission_probe_t admission;
    TEST_ASSERT_EQUAL (ZLINK_HANDLER_OK,
                       zlink_spot_node_actor_admission_handler (
                         target, on_actor_admission, &admission));

    init_text_msg (&create_msg, "create");
    TEST_ASSERT_EQUAL (ZLINK_REQUEST_OK,
                       zlink_spot_node_create_remote_actor (
                         caller, &target_rid, "remote-a", &create_msg,
                         &create_result, 1000));
    TEST_ASSERT_EQUAL_UINT (0, zlink_msg_size (&create_msg));
    TEST_ASSERT_EQUAL (ZLINK_ACTOR_CREATE_CREATED, create_result.status);
    TEST_ASSERT_EQUAL (1, admission.calls);
    TEST_ASSERT_TRUE (create_result.actor.generation != 0);
    zlink_actor_ref_t remote_a_ref = create_result.actor;

    void *target_entry = NULL;
    TEST_ASSERT_EQUAL (ZLINK_CONFIG_OK,
                       zlink_spot_node_entry_spot (target, &target_entry));
    TEST_ASSERT_NOT_NULL (target_entry);
    zlink_actor_ref_t target_entry_rows[1];
    size_t target_entry_count = 1;
    TEST_ASSERT_EQUAL (ZLINK_CONFIG_OK,
                       zlink_spot_actors_snapshot (target_entry,
                                                   target_entry_rows,
                                                   &target_entry_count));
    TEST_ASSERT_EQUAL_UINT (1, target_entry_count);
    TEST_ASSERT_EQUAL_UINT64 (remote_a_ref.generation,
                              target_entry_rows[0].generation);

    void *caller_same_id =
      zlink_spot_node_actor_new (caller, "remote-routed");
    TEST_ASSERT_NOT_NULL (caller_same_id);
    admission.calls = 0;
    init_text_msg (&create_msg, "route-to-target");
    TEST_ASSERT_EQUAL (ZLINK_REQUEST_OK,
                       zlink_spot_node_create_remote_actor (
                         caller, &target_rid, "remote-routed", &create_msg,
                         &create_result, 1000));
    TEST_ASSERT_EQUAL (ZLINK_ACTOR_CREATE_CREATED, create_result.status);
    TEST_ASSERT_TRUE (rid_equals (create_result.actor.node_rid, target_rid));
    TEST_ASSERT_EQUAL (1, admission.calls);
    zlink_actor_ref_t caller_same_ref;
    TEST_ASSERT_EQUAL (ZLINK_CONFIG_OK,
                       zlink_actor_get_ref (caller_same_id,
                                            &caller_same_ref));
    TEST_ASSERT_FALSE (rid_equals (caller_same_ref.node_rid,
                                   create_result.actor.node_rid));
    TEST_ASSERT_EQUAL (ZLINK_REQUEST_OK,
                       zlink_spot_node_destroy_remote_actor (
                         caller, &create_result.actor, 1000));
    TEST_ASSERT_EQUAL (ZLINK_REQUEST_OK,
                       zlink_actor_destroy (&caller_same_id, 1000));

    init_text_msg (&create_msg, "existing");
    TEST_ASSERT_EQUAL (ZLINK_REQUEST_OK,
                       zlink_spot_node_create_remote_actor (
                         caller, &target_rid, "remote-a", &create_msg,
                         &create_result, 1000));
    TEST_ASSERT_EQUAL (ZLINK_ACTOR_CREATE_EXISTING, create_result.status);
    TEST_ASSERT_EQUAL (1, admission.calls);

    void *remote_a_stream = zlink_socket (ctx, ZLINK_SOCKET_STREAM);
    TEST_ASSERT_NOT_NULL (remote_a_stream);
    zlink_routing_id_t remote_a_session_rid;
    set_rid (&remote_a_session_rid, "remote-a-session");
    TEST_ASSERT_EQUAL (ZLINK_REQUEST_OK,
                       zlink_stream_bind_actor (caller, remote_a_stream,
                                                &remote_a_session_rid,
                                                &remote_a_ref, 1000));

    void *remote_existing_spot = zlink_spot_new (target);
    TEST_ASSERT_NOT_NULL (remote_existing_spot);
    zlink_routing_id_t remote_existing_spot_rid;
    TEST_ASSERT_EQUAL (ZLINK_CONFIG_OK,
                       zlink_get_routing_id (remote_existing_spot,
                                             &remote_existing_spot_rid));
    actor_probe_t remote_existing_probe;
    TEST_ASSERT_EQUAL (ZLINK_HANDLER_OK,
                       zlink_spot_dispatch_event_handler (
                         remote_existing_spot, on_join_only_dispatch,
                         &remote_existing_probe));
    zlink_msg_t remote_existing_join;
    init_text_msg (&remote_existing_join, "remote-existing-join");
    TEST_ASSERT_EQUAL (ZLINK_SUBMIT_OK,
                       zlink_spot_node_actor_join_spot (
                         caller, &remote_a_ref, &remote_existing_spot_rid,
                         &remote_existing_join, on_join_reply,
                         &remote_existing_probe, ZLINK_DONTWAIT, 1000));
    {
        std::unique_lock<std::mutex> lock (remote_existing_probe.mutex);
        TEST_ASSERT_TRUE (remote_existing_probe.cv.wait_for (
          lock, std::chrono::seconds (2),
          [&] { return remote_existing_probe.join_done; }));
        TEST_ASSERT_EQUAL (ZLINK_REQUEST_OK,
                           remote_existing_probe.join_result);
    }
    admission.calls = 0;
    init_text_msg (&create_msg, "existing-keeps-spot");
    TEST_ASSERT_EQUAL (ZLINK_REQUEST_OK,
                       zlink_spot_node_create_remote_actor (
                         caller, &target_rid, "remote-a", &create_msg,
                         &create_result, 1000));
    TEST_ASSERT_EQUAL (ZLINK_ACTOR_CREATE_EXISTING, create_result.status);
    TEST_ASSERT_EQUAL (0, admission.calls);
    zlink_actor_ref_t remote_existing_rows[1];
    size_t remote_existing_count = 1;
    TEST_ASSERT_EQUAL (ZLINK_CONFIG_OK,
                       zlink_spot_actors_snapshot (remote_existing_spot,
                                                   remote_existing_rows,
                                                   &remote_existing_count));
    TEST_ASSERT_EQUAL_UINT (1, remote_existing_count);
    TEST_ASSERT_EQUAL_UINT64 (remote_a_ref.generation,
                              remote_existing_rows[0].generation);
    TEST_ASSERT_EQUAL (ZLINK_REQUEST_INVALID_STATE,
                       zlink_spot_node_actor_destroy (caller, &remote_a_ref,
                                                      1000));
    TEST_ASSERT_EQUAL (ZLINK_REQUEST_OK,
                       zlink_spot_node_actor_leave_spot (
                         caller, &remote_a_ref, &remote_existing_spot_rid,
                         1000));

    zlink_msg_t retry_msg;
    zlink_actor_create_result_t retry_result;
    init_text_msg (&retry_msg, "retry-created");
    TEST_ASSERT_EQUAL (ZLINK_REQUEST_OK,
                       zlink_spot_node_create_remote_actor (
                         caller, &target_rid, "remote-retry", &retry_msg,
                         &retry_result, 0));
    TEST_ASSERT_EQUAL (ZLINK_ACTOR_CREATE_CREATED, retry_result.status);
    init_text_msg (&retry_msg, "retry-existing");
    TEST_ASSERT_EQUAL (ZLINK_REQUEST_OK,
                       zlink_spot_node_create_remote_actor (
                         caller, &target_rid, "remote-retry", &retry_msg,
                         &retry_result, 0));
    TEST_ASSERT_EQUAL (ZLINK_ACTOR_CREATE_EXISTING, retry_result.status);

    zlink_actor_create_result_t concurrent_results[2];
    zlink_request_result_t concurrent_rc[2] = {
      ZLINK_REQUEST_INTERNAL_ERROR, ZLINK_REQUEST_INTERNAL_ERROR};
    std::thread threads[2];
    for (int i = 0; i < 2; ++i) {
        threads[i] = std::thread ([&, i] {
            zlink_msg_t concurrent_msg;
            init_text_msg (&concurrent_msg, i == 0 ? "c0" : "c1");
            concurrent_rc[i] = zlink_spot_node_create_remote_actor (
              caller, &target_rid, "remote-concurrent", &concurrent_msg,
              &concurrent_results[i], 1000);
        });
    }
    threads[0].join ();
    threads[1].join ();
    TEST_ASSERT_EQUAL (ZLINK_REQUEST_OK, concurrent_rc[0]);
    TEST_ASSERT_EQUAL (ZLINK_REQUEST_OK, concurrent_rc[1]);
    TEST_ASSERT_TRUE (
      (concurrent_results[0].status == ZLINK_ACTOR_CREATE_CREATED
       && concurrent_results[1].status == ZLINK_ACTOR_CREATE_EXISTING)
      || (concurrent_results[0].status == ZLINK_ACTOR_CREATE_EXISTING
          && concurrent_results[1].status == ZLINK_ACTOR_CREATE_CREATED));

    admission.accept = false;
    init_text_msg (&create_msg, "reject");
    TEST_ASSERT_EQUAL (ZLINK_REQUEST_REJECTED,
                       zlink_spot_node_create_remote_actor (
                         caller, &target_rid, "remote-rejected", &create_msg,
                         &create_result, 1000));
    TEST_ASSERT_EQUAL_UINT (0, zlink_msg_size (&create_msg));
    TEST_ASSERT_EQUAL (3, admission.calls);

    admission.accept = true;
    admission.try_reentrant_create = true;
    init_text_msg (&create_msg, "reentrant");
    TEST_ASSERT_EQUAL (ZLINK_REQUEST_OK,
                       zlink_spot_node_create_remote_actor (
                         caller, &target_rid, "remote-reentrant", &create_msg,
                         &create_result, 1000));
    TEST_ASSERT_TRUE (admission.reentrant_create_blocked);
    TEST_ASSERT_EQUAL (ZLINK_ACTOR_CREATE_CREATED, create_result.status);
    TEST_ASSERT_EQUAL (4, admission.calls);

    zlink_actor_ref_t stale = create_result.actor;
    ++stale.generation;
    TEST_ASSERT_EQUAL (ZLINK_REQUEST_CONFLICT,
                       zlink_spot_node_destroy_remote_actor (caller, &stale,
                                                            1000));
    zlink_routing_id_t missing_destroy_rid;
    set_rid (&missing_destroy_rid, "missing-destroy-node");
    zlink_actor_ref_t missing_destroy_ref;
    TEST_ASSERT_EQUAL (ZLINK_CONFIG_OK,
                       zlink_remote_actor_get_ref (&missing_destroy_rid,
                                                   "missing-destroy",
                                                   &missing_destroy_ref));
    TEST_ASSERT_EQUAL (ZLINK_REQUEST_NOT_CONNECTED,
                       zlink_spot_node_destroy_remote_actor (
                         caller, &missing_destroy_ref, 1000));
    TEST_ASSERT_EQUAL (ZLINK_REQUEST_OK,
                       zlink_spot_node_destroy_remote_actor (
                         caller, &create_result.actor, 1000));
    zlink_actor_ref_t lookup_after_destroy;
    TEST_ASSERT_EQUAL (ZLINK_CONFIG_NOT_FOUND,
                       zlink_spot_node_actor_lookup (
                         target, create_result.actor.actor_id,
                         &lookup_after_destroy));
    TEST_ASSERT_EQUAL (ENOENT, zlink_errno ());

    zlink_actor_ref_t unchecked;
    TEST_ASSERT_EQUAL (ZLINK_CONFIG_OK,
                       zlink_remote_actor_get_ref (&target_rid, "remote-a",
                                                   &unchecked));
    TEST_ASSERT_EQUAL_UINT64 (0, unchecked.generation);
    TEST_ASSERT_EQUAL (ZLINK_REQUEST_OK,
                       zlink_spot_node_destroy_remote_actor (caller,
                                                            &unchecked,
                                                            1000));
    TEST_ASSERT_EQUAL (ZLINK_REQUEST_NOT_FOUND,
                       zlink_spot_node_destroy_remote_actor (caller,
                                                            &unchecked,
                                                            1000));

    void *remote_joined = zlink_spot_node_actor_new (target, "remote-joined");
    TEST_ASSERT_NOT_NULL (remote_joined);
    zlink_actor_ref_t remote_joined_ref;
    TEST_ASSERT_EQUAL (ZLINK_CONFIG_OK,
                       zlink_actor_get_ref (remote_joined,
                                            &remote_joined_ref));
    void *remote_joined_stream = zlink_socket (ctx, ZLINK_SOCKET_STREAM);
    TEST_ASSERT_NOT_NULL (remote_joined_stream);
    zlink_routing_id_t remote_joined_session_rid;
    set_rid (&remote_joined_session_rid, "remote-joined-session");
    TEST_ASSERT_EQUAL (ZLINK_REQUEST_OK,
                       zlink_stream_bind_actor (caller, remote_joined_stream,
                                                &remote_joined_session_rid,
                                                &remote_joined_ref, 1000));
    void *remote_spot = zlink_spot_new (target);
    TEST_ASSERT_NOT_NULL (remote_spot);
    actor_probe_t remote_join_probe;
    TEST_ASSERT_EQUAL (ZLINK_HANDLER_OK,
                       zlink_spot_dispatch_event_handler (
                         remote_spot, on_join_only_dispatch,
                         &remote_join_probe));
    zlink_msg_t remote_join_msg;
    init_text_msg (&remote_join_msg, "remote-join");
    TEST_ASSERT_EQUAL (ZLINK_SUBMIT_OK,
                       zlink_actor_join_spot (remote_joined, remote_spot,
                                              &remote_join_msg, on_join_reply,
                                              &remote_join_probe,
                                              ZLINK_DONTWAIT, 1000));
    {
        std::unique_lock<std::mutex> lock (remote_join_probe.mutex);
        TEST_ASSERT_TRUE (remote_join_probe.cv.wait_for (
          lock, std::chrono::seconds (2),
          [&] { return remote_join_probe.join_done; }));
        TEST_ASSERT_EQUAL (ZLINK_REQUEST_OK, remote_join_probe.join_result);
    }
    TEST_ASSERT_EQUAL (ZLINK_REQUEST_INVALID_STATE,
                       zlink_spot_node_destroy_remote_actor (
                         caller, &remote_joined_ref, 1000));
    TEST_ASSERT_EQUAL (ZLINK_CONFIG_OK,
                       zlink_actor_leave_spot (remote_joined, remote_spot));
    TEST_ASSERT_EQUAL (ZLINK_REQUEST_OK,
                       zlink_spot_node_destroy_remote_actor (
                         caller, &remote_joined_ref, 1000));
    TEST_ASSERT_EQUAL (ZLINK_CLOSE_OK, zlink_close (remote_joined_stream));

    void *remote_bound = zlink_spot_node_actor_new (target, "remote-bound");
    TEST_ASSERT_NOT_NULL (remote_bound);
    zlink_actor_ref_t remote_bound_ref;
    TEST_ASSERT_EQUAL (ZLINK_CONFIG_OK,
                       zlink_actor_get_ref (remote_bound, &remote_bound_ref));
    void *stream = zlink_socket (ctx, ZLINK_SOCKET_STREAM);
    TEST_ASSERT_NOT_NULL (stream);
    zlink_routing_id_t remote_session_rid;
    set_rid (&remote_session_rid, "remote-bound-session");
    TEST_ASSERT_EQUAL (ZLINK_REQUEST_OK,
                       zlink_stream_bind_actor (caller, stream,
                                                &remote_session_rid,
                                                &remote_bound_ref, 1000));
    zlink_msg_t remote_more;
    init_text_msg (&remote_more, "remote-more");
    TEST_ASSERT_EQUAL (ZLINK_SUBMIT_OK,
                       zlink_stream_send_bound_actor_part (
                         caller, stream, &remote_session_rid, "remote-bound",
                         &remote_more, ZLINK_DONTWAIT, ZLINK_PART_MORE));
    TEST_ASSERT_EQUAL (ZLINK_REQUEST_BUSY,
                       zlink_spot_node_destroy_remote_actor (
                         caller, &remote_bound_ref, 1000));
    zlink_actor_ref_t still_bound;
    TEST_ASSERT_EQUAL (ZLINK_CONFIG_OK,
                       zlink_spot_node_actor_lookup (target, "remote-bound",
                                                     &still_bound));
    TEST_ASSERT_EQUAL_UINT64 (remote_bound_ref.generation,
                              still_bound.generation);
    zlink_msg_t remote_final;
    init_text_msg (&remote_final, "remote-final");
    TEST_ASSERT_EQUAL (ZLINK_SUBMIT_OK,
                       zlink_stream_send_bound_actor_part (
                         caller, stream, &remote_session_rid, "remote-bound",
                         &remote_final, ZLINK_DONTWAIT, ZLINK_PART_FINAL));
    TEST_ASSERT_EQUAL (ZLINK_REQUEST_OK,
                       zlink_stream_unbind_actor (caller, stream,
                                                  &remote_session_rid,
                                                  "remote-bound", 1000));
    TEST_ASSERT_EQUAL (ZLINK_REQUEST_OK,
                       zlink_spot_node_destroy_remote_actor (
                         caller, &remote_bound_ref, 1000));
    TEST_ASSERT_EQUAL (ZLINK_CLOSE_OK, zlink_close (stream));

    TEST_ASSERT_EQUAL (ZLINK_REQUEST_OK,
                       zlink_spot_node_destroy_remote_actor (
                         caller, &retry_result.actor, 1000));
    TEST_ASSERT_EQUAL (ZLINK_REQUEST_OK,
                       zlink_spot_node_destroy_remote_actor (
                         caller,
                         concurrent_results[0].status
                           == ZLINK_ACTOR_CREATE_CREATED
                           ? &concurrent_results[0].actor
                           : &concurrent_results[1].actor,
                         1000));

    TEST_ASSERT_EQUAL (ZLINK_CLOSE_OK, zlink_spot_destroy (&remote_spot));
    TEST_ASSERT_EQUAL (ZLINK_CLOSE_OK,
                       zlink_spot_destroy (&remote_existing_spot));
    TEST_ASSERT_EQUAL (ZLINK_CLOSE_OK, zlink_close (remote_a_stream));
    TEST_ASSERT_EQUAL (ZLINK_CLOSE_OK, zlink_spot_destroy (&target_entry));
    TEST_ASSERT_EQUAL (ZLINK_CLOSE_OK, zlink_spot_node_destroy (&target));
    TEST_ASSERT_EQUAL (ZLINK_CLOSE_OK, zlink_spot_node_destroy (&caller));
    TEST_ASSERT_EQUAL (ZLINK_CLOSE_OK, zlink_ctx_term (ctx));
}

int main ()
{
    setup_test_environment ();
    UNITY_BEGIN ();
    RUN_TEST (test_entry_spot_facade_lookup_and_rid);
    RUN_TEST (test_spot_lookup_refcount_and_rid_index);
    RUN_TEST (test_actor_lifecycle_ref_and_lookup);
    RUN_TEST (test_entry_spot_dispatch_receives_bound_actor_message);
    RUN_TEST (test_actor_join_bind_relay_and_dispatch_recv);
    RUN_TEST (test_actor_join_timeout_without_dispatch_handler);
    RUN_TEST (test_actor_join_requires_session_and_leave_stale);
    RUN_TEST (test_actor_remote_join_handoff_accept_reject_timeout);
    RUN_TEST (test_actor_close_bound_session_returns_entry_and_dispatches);
    RUN_TEST (test_actor_join_reject_ref_edges_and_rejoin_fifo);
    RUN_TEST (test_actor_send_bound_session_raw_and_packet);
    RUN_TEST (test_actor_route_sync_publish_requires_option_and_bind);
    RUN_TEST (test_spot_snapshot_destroy_joined_and_pending_counts);
    RUN_TEST (test_stream_bind_replaces_actor_id_entry);
    RUN_TEST (test_actor_send_bound_session_owner_not_connected);
    RUN_TEST (test_stream_remote_actor_owner_not_connected_and_unbind_cleanup);
    RUN_TEST (test_stream_unbind_not_connected_keeps_binding);
    RUN_TEST (test_stream_remote_target_actor_missing_drops_after_forward);
    RUN_TEST (test_actor_request_timeout_atomicity_under_control_lock);
    RUN_TEST (test_stream_multipart_selector_and_unbound_relay);
    RUN_TEST (test_actor_queue_dispatch_receive_and_backpressure);
    RUN_TEST (test_actor_route_move_joined_publish_and_provider_cleanup);
    RUN_TEST (test_remote_create_existing_and_destroy_generation_rules);
    return UNITY_END ();
}
