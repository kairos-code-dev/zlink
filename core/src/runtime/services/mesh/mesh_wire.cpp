/* SPDX-License-Identifier: MPL-2.0 */

#include "utils/precompiled.hpp"

#include <string.h>

#include <memory>
#include <limits>
#include <atomic>
#include <string>
#include <thread>
#include <vector>

#include "services/mesh/mesh_wire_internal.hpp"
#include "api/mesh/mesh_c_internal.hpp"
#include "api/monitoring/monitor_api_internal.hpp"
#include "api/socket/socket_api_internal.hpp"
#include "sockets/common/socket_base.hpp"
#include "utils/macros.hpp"

#ifdef ZLINK_BUILD_TESTS
namespace
{
std::atomic<int> g_instance_wire_pause_before_send (0);
std::atomic<int> g_instance_wire_before_send_paused (0);

void test_pause_instance_wire_before_send ()
{
    if (g_instance_wire_pause_before_send.load (std::memory_order_acquire)
        == 0)
        return;
    g_instance_wire_before_send_paused.store (1, std::memory_order_release);
    while (g_instance_wire_pause_before_send.load (
             std::memory_order_acquire)
           != 0)
        std::this_thread::yield ();
    g_instance_wire_before_send_paused.store (0, std::memory_order_release);
}
}

extern "C" void zlink_test_mesh_pause_instance_wire_before_send (int enabled_)
{
    g_instance_wire_pause_before_send.store (
      enabled_ != 0 ? 1 : 0, std::memory_order_release);
    if (enabled_ == 0)
        g_instance_wire_before_send_paused.store (
          0, std::memory_order_release);
}

extern "C" int zlink_test_mesh_instance_wire_before_send_paused ()
{
    return g_instance_wire_before_send_paused.load (
      std::memory_order_acquire);
}
#endif

//  Transport lifecycle and outbound submits: the node-owned ROUTER, its
//  monitor, endpoint resolution and every wire_submit_* egress path.
namespace zlink
{
namespace mesh
{
//  --- frame transmission -------------------------------------------------------

namespace
{
void on_router_send_ready (void *, void *userdata_)
{
    mesh_node_t *node = static_cast<mesh_node_t *> (userdata_);
    if (node)
        notify_remote_send_ready (node);
}

//  A successful MORE frame opens a part-helper send scope. If any later
//  allocation or frame send fails, roll that scope back before another
//  message uses the node-owned ROUTER.
class wire_send_sequence_guard_t
{
  public:
    wire_send_sequence_guard_t (zlink::socket_base_t *socket_,
                                zlink::socket_public_send_scope_t *scope_) :
        _socket (socket_),
        _scope (scope_),
        _open (false)
    {
    }
    ~wire_send_sequence_guard_t ()
    {
        if (!_open || !_socket || !_scope)
            return;
        const int saved_errno = errno;
        (void) _socket->rollback_scoped (*_scope);
        errno = saved_errno;
    }

    void update (zlink_part_flag_t part_flag_) { _open = part_flag_ == ZLINK_PART_MORE; }

  private:
    zlink::socket_base_t *_socket;
    zlink::socket_public_send_scope_t *_scope;
    bool _open;
    wire_send_sequence_guard_t (const wire_send_sequence_guard_t &);
    wire_send_sequence_guard_t &operator= (const wire_send_sequence_guard_t &);
};
}

int send_frame (mesh_node_t *node_,
                const zlink_routing_id_t &target_,
                const std::vector<unsigned char> &bytes_,
                zlink_part_flag_t part_flag_,
                zlink_send_flags_t flags_)
{
    zlink_msg_t frame;
    if (zlink_msg_init_size (&frame, bytes_.size ()) != 0)
        return -1;
    memcpy (zlink_msg_data (&frame), bytes_.data (), bytes_.size ());
    const zlink_submit_result_t rc =
      zlink_send_part_rid (node_->router_socket, &target_, &frame, flags_, part_flag_);
    if (rc != ZLINK_SUBMIT_OK) {
        const int saved_errno = errno;
        zlink_msg_close (&frame);
        errno = saved_errno;
        return -1;
    }
    return 0;
}

//  Sends one control message (single frame) to target_ without blocking.
int send_control (mesh_node_t *node_,
                  const zlink_routing_id_t &target_,
                  const std::vector<unsigned char> &frame_)
{
    std::unique_lock<std::mutex> lock (node_->wire_send_mutex);
    if (!node_->router_socket) {
        errno = ENOTCONN;
        return -1;
    }
    const int rc = send_frame (
      node_, target_, frame_, ZLINK_PART_FINAL, ZLINK_SEND_FLAGS_DONTWAIT);
    const int reason = errno;
    lock.unlock ();
    if (rc == 0)
        notify_remote_send_ready (node_);
    errno = reason;
    return rc;
}

//  send_data_message body for callers that already hold wire_send_mutex.
zlink_submit_result_t send_data_message_unlocked (mesh_node_t *node_,
                                                  const zlink_routing_id_t &target_,
                                                  const std::vector<unsigned char> &envelope_,
                                                  const std::vector<unsigned char> *metadata_,
                                                  const zlink_msg_t *parts_,
                                                  size_t part_count_,
                                                  zlink_send_flags_t flags_,
                                                  uint64_t *connection_id_out_,
                                                  uint64_t expected_connection_id_);

int send_control_exact (mesh_node_t *node_,
                        const zlink_routing_id_t &target_,
                        const std::vector<unsigned char> &frame_,
                        uint64_t expected_connection_id_)
{
    std::unique_lock<std::mutex> lock (node_->wire_send_mutex);
    if (!node_->router_socket) {
        errno = ENOTCONN;
        return -1;
    }
    uint64_t actual_connection_id = 0;
    const zlink_submit_result_t result = send_data_message_unlocked (
      node_, target_, frame_, NULL, NULL, 0, ZLINK_SEND_FLAGS_DONTWAIT,
      &actual_connection_id, expected_connection_id_);
    const int reason = errno;
    lock.unlock ();
    if (result == ZLINK_SUBMIT_OK)
        notify_remote_send_ready (node_);
    errno = reason;
    return result == ZLINK_SUBMIT_OK ? 0 : -1;
}

//  Sends envelope [+ metadata] + payload parts. Payload parts are borrowed
//  from the caller and copied (reference counted) per part.
zlink_submit_result_t send_data_message (mesh_node_t *node_,
                                         const zlink_routing_id_t &target_,
                                         const std::vector<unsigned char> &envelope_,
                                         const std::vector<unsigned char> *metadata_,
                                         const zlink_msg_t *parts_,
                                         size_t part_count_,
                                         zlink_send_flags_t flags_,
                                         uint64_t *connection_id_out_,
                                         uint64_t expected_connection_id_)
{
    std::unique_lock<std::mutex> lock (node_->wire_send_mutex);
    if (!node_->router_socket) {
        errno = ENOTCONN;
        return ZLINK_SUBMIT_NOT_CONNECTED;
    }
    const zlink_submit_result_t result =
      send_data_message_unlocked (node_, target_, envelope_, metadata_, parts_,
                                  part_count_, flags_, connection_id_out_,
                                  expected_connection_id_);
    const int reason = errno;
    lock.unlock ();
    if (result == ZLINK_SUBMIT_OK)
        notify_remote_send_ready (node_);
    errno = reason;
    return result;
}

//  Linearizes a new application wire admission against DRAINING. Acquire both
//  scopes without retaining either one while waiting for the other, so ROUTER
//  backpressure cannot make a sender stall status/peer queries through the node
//  state mutex.
zlink_submit_result_t send_application_data_message (
  mesh_node_t *node_,
  const zlink_routing_id_t &target_,
  const std::vector<unsigned char> &envelope_,
  const std::vector<unsigned char> *metadata_,
  const zlink_msg_t *parts_,
  size_t part_count_,
  zlink_send_flags_t flags_,
  const send_ready_interest_t *interest_ = NULL,
  uint64_t *connection_id_out_ = NULL,
  uint64_t expected_connection_id_ = 0)
{
    std::unique_lock<std::mutex> node_lock (node_->mutex, std::defer_lock);
    std::unique_lock<std::mutex> wire_lock (
      node_->wire_send_mutex, std::defer_lock);
    std::lock (node_lock, wire_lock);
    if (node_->state == ZLINK_MESH_NODE_DRAINING
        || node_->state == ZLINK_MESH_NODE_STOPPED) {
        errno = ESHUTDOWN;
        return ZLINK_SUBMIT_INVALID_STATE;
    }
    if (!node_->router_socket) {
        errno = ENOTCONN;
        return ZLINK_SUBMIT_NOT_CONNECTED;
    }
    node_lock.unlock ();
    const zlink_submit_result_t result =
      send_data_message_unlocked (node_, target_, envelope_, metadata_, parts_,
                                  part_count_, flags_, connection_id_out_,
                                  expected_connection_id_);
    const int reason = errno;
    bool notify_after_unlock = result == ZLINK_SUBMIT_OK;
    wire_lock.unlock ();
    if (interest_ && result == ZLINK_SUBMIT_BACKPRESSURED
        && reason == EAGAIN) {
        //  Register without wire_send_mutex. The socket callback may run
        //  inline through the MeshNode ready handler, and that handler may
        //  retry a send. Rechecking the socket level after registration covers
        //  a recovery callback that raced this handoff.
        register_remote_send_ready_interest (node_, *interest_);
        {
            std::lock_guard<std::mutex> lock (node_->wire_send_mutex);
            socket_handle_t handle = as_socket_handle (node_->router_socket);
            if (handle.socket && handle.socket->has_out ())
                notify_after_unlock = true;
        }
    }
    if (notify_after_unlock)
        notify_remote_send_ready (node_);
    errno = reason;
    return result;
}

zlink_submit_result_t send_data_message_unlocked (mesh_node_t *node_,
                                                  const zlink_routing_id_t &target_,
                                                  const std::vector<unsigned char> &envelope_,
                                                  const std::vector<unsigned char> *metadata_,
                                                  const zlink_msg_t *parts_,
                                                  size_t part_count_,
                                                  zlink_send_flags_t flags_,
                                                  uint64_t *connection_id_out_,
                                                  uint64_t expected_connection_id_)
{
    if (connection_id_out_)
        *connection_id_out_ = 0;
    socket_handle_t handle = as_socket_handle (node_->router_socket);
    if (!handle.socket)
        return submit_errno_result ();

    //  Mesh owns the whole wire message, so it does not need the public
    //  part-at-a-time helper to remember a multipart sequence between API
    //  calls. Keep one socket send scope for the complete transaction. This
    //  removes the per-frame shared_ptr lookup, state mutex and routing ID/spec
    //  copies while preserving the ROUTER rollback boundary.
    std::unique_ptr<zlink::socket_public_send_scope_t> send_scope =
      handle.socket->begin_public_send_scope (true);
    if (!send_scope)
        return submit_errno_result ();
    wire_send_sequence_guard_t sequence (handle.socket, send_scope.get ());

    const bool has_payload = part_count_ > 0;
    const zlink_part_flag_t envelope_flag =
      (metadata_ || has_payload) ? ZLINK_PART_MORE : ZLINK_PART_FINAL;

    zlink_msg_t envelope;
    if (zlink_msg_init_size (&envelope, envelope_.size ()) != 0)
        return ZLINK_SUBMIT_OUT_OF_MEMORY;
    memcpy (zlink_msg_data (&envelope), envelope_.data (), envelope_.size ());
    const int envelope_rc = handle.socket->send_routed_scoped (
      &target_, reinterpret_cast<zlink::msg_t *> (&envelope),
      static_cast<int> (flags_ & ZLINK_DONTWAIT)
        | (envelope_flag == ZLINK_PART_MORE ? ZLINK_SNDMORE : 0),
      *send_scope, connection_id_out_, expected_connection_id_);
    if (envelope_rc != 0) {
        const int saved_errno = errno;
        zlink_msg_close (&envelope);
        errno = saved_errno;
        return submit_errno_result ();
    }
    sequence.update (envelope_flag);
#ifdef ZLINK_BUILD_TESTS
    if (envelope_flag == ZLINK_PART_MORE) {
        try {
            test_maybe_throw_alloc ();
        }
        catch (const std::bad_alloc &) {
            errno = ENOMEM;
            return ZLINK_SUBMIT_OUT_OF_MEMORY;
        }
    }
#endif
    if (metadata_) {
        const zlink_part_flag_t metadata_flag =
          has_payload ? ZLINK_PART_MORE : ZLINK_PART_FINAL;
        zlink_msg_t metadata;
        if (zlink_msg_init_size (&metadata, metadata_->size ()) != 0)
            return ZLINK_SUBMIT_OUT_OF_MEMORY;
        memcpy (zlink_msg_data (&metadata), metadata_->data (), metadata_->size ());
        const int metadata_rc = handle.socket->send_scoped (
          reinterpret_cast<zlink::msg_t *> (&metadata),
          static_cast<int> (flags_ & ZLINK_DONTWAIT)
            | (metadata_flag == ZLINK_PART_MORE ? ZLINK_SNDMORE : 0),
          *send_scope);
        if (metadata_rc != 0) {
            const int saved_errno = errno;
            zlink_msg_close (&metadata);
            errno = saved_errno;
            return submit_errno_result ();
        }
        sequence.update (metadata_flag);
    }
    for (size_t i = 0; i < part_count_; ++i) {
        zlink_msg_t copy;
        if (zlink_msg_init (&copy) != 0)
            return ZLINK_SUBMIT_OUT_OF_MEMORY;
        if (zlink_msg_copy (&copy, const_cast<zlink_msg_t *> (&parts_[i])) != 0) {
            zlink_msg_close (&copy);
            errno = EFAULT;
            return ZLINK_SUBMIT_INTERNAL_ERROR;
        }
        const zlink_part_flag_t part_flag =
          (i + 1 < part_count_) ? ZLINK_PART_MORE : ZLINK_PART_FINAL;
        const int rc = handle.socket->send_scoped (
          reinterpret_cast<zlink::msg_t *> (&copy),
          static_cast<int> (flags_ & ZLINK_DONTWAIT)
            | (part_flag == ZLINK_PART_MORE ? ZLINK_SNDMORE : 0),
          *send_scope);
        if (rc != 0) {
            const int saved_errno = errno;
            zlink_msg_close (&copy);
            errno = saved_errno;
            return submit_errno_result ();
        }
        sequence.update (part_flag);
    }
    return ZLINK_SUBMIT_OK;
}

//  --- lifecycle ---------------------------------------------------------------------

int wire_start (mesh_node_t *node_)
{
    void *router = zlink_socket (node_->ctx, ZLINK_SOCKET_ROUTER);
    if (!router)
        return -1;

    zlink_routing_id_t rid = rid_value (node_->routing_id);
    if (zlink_set_routing_id (router, reinterpret_cast<const char *> (rid.data), rid.size)
        != ZLINK_CONFIG_OK) {
        zlink_close (router);
        return -1;
    }
    //  Mesh lifecycle allows the same peer RID to reconnect and a higher
    //  generation to supersede its predecessor. Use ROUTER's standard
    //  duplicate-RID handover policy so an asynchronously retiring pipe
    //  cannot reject the successor transport.
    const int duplicate_policy = ZLINK_RID_DUPLICATE_HANDOVER;
    if (zlink_set_option (router, ZLINK_OPT_RID_DUPLICATE_POLICY,
                          &duplicate_policy, sizeof (duplicate_policy))
        != ZLINK_CONFIG_OK) {
        const int saved_errno = errno;
        zlink_close (router);
        errno = saved_errno;
        return -1;
    }
    if (node_->router_hwm_override > 0) {
        const int hwm = node_->router_hwm_override;
        if (zlink_set_option (router, ZLINK_OPT_SNDHWM, &hwm, sizeof (hwm))
              != ZLINK_CONFIG_OK
            || zlink_set_option (router, ZLINK_OPT_RCVHWM, &hwm, sizeof (hwm))
                 != ZLINK_CONFIG_OK) {
            const int saved_errno = errno;
            zlink_close (router);
            errno = saved_errno;
            return -1;
        }
    }
    if (zlink_set_option (router, ZLINK_OPT_SNDTIMEO, &node_->sndtimeo_ms,
                          sizeof (node_->sndtimeo_ms))
        != ZLINK_CONFIG_OK) {
        const int saved_errno = errno;
        zlink_close (router);
        errno = saved_errno;
        return -1;
    }
    if ((!node_->tls_server_cert.empty ()
         && zlink_set_tls_server (
              router, node_->tls_server_cert.c_str (),
              node_->tls_server_key.c_str (),
              node_->tls_require_client_cert)
              != ZLINK_CONFIG_OK)
        || (!node_->tls_client_ca.empty ()
            && zlink_set_tls_client (
                 router, node_->tls_client_ca.c_str (),
                 node_->tls_client_hostname.c_str (),
                 node_->tls_trust_system)
                 != ZLINK_CONFIG_OK)) {
        const int saved_errno = errno;
        zlink_close (router);
        errno = saved_errno;
        return -1;
    }
    if (zlink_bind (router, node_->bind_endpoint.c_str ()) != ZLINK_BIND_OK) {
        const int saved_errno = errno;
        zlink_close (router);
        errno = saved_errno;
        return -1;
    }
    if (zlink_send_ready_handler (router, &on_router_send_ready, node_)
        != ZLINK_HANDLER_OK) {
        const int saved_errno = errno;
        zlink_close (router);
        errno = saved_errno;
        return -1;
    }
    char resolved[512] = "";
    size_t resolved_size = sizeof (resolved);
    if (zlink_get_option (router, ZLINK_OPT_LAST_ENDPOINT, resolved, &resolved_size)
          == ZLINK_CONFIG_OK
        && resolved[0] != '\0')
        node_->bind_endpoint = resolved;

    zlink_socket_monitor_open_options_t monitor_options;
    memset (&monitor_options, 0, sizeof (monitor_options));
    monitor_options.events =
      ZLINK_EVENT_CONNECTION_READY | ZLINK_EVENT_DISCONNECTED;
    //  Mesh membership must not lose a physical ready/disconnect edge while
    //  the private inproc PAIR finishes attaching. Version 4 uses the existing
    //  monitor worker retry path; it does not change ROUTER delivery policy.
    node_->router_monitor = open_socket_monitor_internal (
      router, monitor_options.events, 4);

    node_->router_socket = router;
    node_->io_stop.store (false, std::memory_order_release);
    try {
        node_->io_thread = std::thread (run_ingress_loop, node_);
    } catch (...) {
        if (node_->router_monitor)
            zlink_monitor_close (&node_->router_monitor);
        zlink_close (router);
        node_->router_socket = NULL;
        errno = ENOMEM;
        return -1;
    }
    return 0;
}

void wire_stop (mesh_node_t *node_)
{
    {
        std::lock_guard<std::mutex> state_lock (node_->mutex);
        clear_send_ready_interests_locked (node_);
    }
    node_->io_stop.store (true, std::memory_order_release);
    //  A blocking send may use SNDTIMEO=-1 while holding wire_send_mutex.
    //  The socket's stop command is the existing thread-safe cancellation
    //  path: it wakes that send with ETERM so shutdown can acquire the mutex
    //  and close without changing the configured timeout.
    if (node_->router_socket) {
        socket_handle_t handle = as_socket_handle (node_->router_socket);
        if (handle.socket)
            handle.socket->stop ();
    }
    if (node_->io_thread.joinable ()) {
        node_->io_thread.join ();
    }
    //  All outbound data and control messages use this mutex. Closing under
    //  it lets an already-admitted send finish and makes later senders
    //  observe the null socket instead of racing zlink_close().
    std::lock_guard<std::mutex> lock (node_->wire_send_mutex);
    if (node_->router_monitor) {
        zlink_monitor_close (&node_->router_monitor);
    }
    if (node_->router_socket) {
        zlink_close (node_->router_socket);
        node_->router_socket = NULL;
    }
}

int wire_connect_endpoint (mesh_node_t *node_, const std::string &endpoint_)
{
    std::lock_guard<std::mutex> lock (node_->wire_send_mutex);
    if (!node_->router_socket) {
        errno = ENOTCONN;
        return -1;
    }
    if (zlink_connect (node_->router_socket, endpoint_.c_str ()) != ZLINK_CONNECT_OK)
        return -1;
    return 0;
}

int wire_disconnect_endpoint (mesh_node_t *node_, const std::string &endpoint_)
{
    std::lock_guard<std::mutex> lock (node_->wire_send_mutex);
    if (!node_->router_socket) {
        errno = ENOTCONN;
        return -1;
    }
    return zlink_disconnect (node_->router_socket, endpoint_.c_str ())
             == ZLINK_CONNECT_OK
           ? 0
           : -1;
}

int wire_disconnect_peer (mesh_node_t *node_, const rid_bytes_t &peer_rid_)
{
    std::lock_guard<std::mutex> lock (node_->wire_send_mutex);
    if (!node_->router_socket) {
        errno = ENOTCONN;
        return -1;
    }
    const zlink_routing_id_t rid = rid_value (peer_rid_);
    return zlink_disconnect_rid (node_->router_socket, &rid)
             == ZLINK_CONNECT_OK
           ? 0
           : -1;
}

void wire_broadcast_update (mesh_node_t *node_)
{
    std::vector<unsigned char> frame = make_envelope (wire_update, 0);
    std::vector<rid_bytes_t> targets;
    {
        std::lock_guard<std::mutex> lock (node_->mutex);
        encode_descriptor_locked (node_, frame);
        targets.reserve (node_->peers.size ());
        for (size_t i = 0; i < node_->peers.size (); ++i) {
            if (node_->peers[i].state == ZLINK_MESH_PEER_ADMITTED)
                targets.push_back (node_->peers[i].rid);
        }
    }
    for (size_t i = 0; i < targets.size (); ++i)
        send_control (node_, rid_value (targets[i]), frame);
}

zlink_submit_result_t wire_submit_data (mesh_node_t *node_,
                                        const rid_bytes_t &peer_rid_,
                                        wire_type_t type_,
                                        uint64_t correlation_,
                                        const std::string &channel_,
                                        const zlink_mesh_metadata_view_t *metadata_,
                                        const zlink_msg_t *parts_,
                                        size_t part_count_,
                                        zlink_send_flags_t flags_,
                                        const send_ready_interest_t *interest_)
{
    std::vector<unsigned char> envelope =
      make_envelope (static_cast<unsigned char> (type_), metadata_ ? wire_flag_metadata : 0);
    if (type_ == wire_node_request || type_ == wire_channel_request)
        put_u64 (envelope, correlation_);
    if (type_ == wire_channel_send || type_ == wire_channel_request) {
        put_u8 (envelope, static_cast<unsigned char> (channel_.size ()));
        put_bytes (envelope, channel_.data (), channel_.size ());
    }
    std::vector<unsigned char> metadata;
    if (metadata_)
        metadata.assign (metadata_->data, metadata_->data + metadata_->size);

    const zlink_routing_id_t target = rid_value (peer_rid_);
    return send_application_data_message (
      node_, target, envelope, metadata_ ? &metadata : NULL, parts_, part_count_,
      flags_, interest_);
}

zlink_submit_result_t wire_submit_spot (mesh_node_t *node_,
                                        const rid_bytes_t &peer_rid_,
                                        bool is_request_,
                                        uint64_t correlation_,
                                        const rid_bytes_t &source_spot_rid_,
                                        const rid_bytes_t &target_spot_rid_,
                                        uint64_t target_spot_generation_,
                                        const zlink_mesh_metadata_view_t *metadata_,
                                        const zlink_msg_t *parts_,
                                        size_t part_count_,
                                        zlink_send_flags_t flags_,
                                        const send_ready_interest_t *interest_,
                                        uint64_t expected_connection_id_)
{
    std::vector<unsigned char> envelope =
      make_envelope (static_cast<unsigned char> (is_request_ ? wire_spot_request : wire_spot_send),
                     metadata_ ? wire_flag_metadata : 0);
    if (is_request_)
        put_u64 (envelope, correlation_);
    put_rid (envelope, source_spot_rid_);
    put_rid (envelope, target_spot_rid_);
    put_u64 (envelope, target_spot_generation_);
    std::vector<unsigned char> metadata;
    if (metadata_)
        metadata.assign (metadata_->data, metadata_->data + metadata_->size);

    const zlink_routing_id_t target = rid_value (peer_rid_);
#ifdef ZLINK_BUILD_TESTS
    test_pause_instance_wire_before_send ();
#endif
    return send_application_data_message (
      node_, target, envelope, metadata_ ? &metadata : NULL, parts_, part_count_,
      flags_, interest_, NULL, expected_connection_id_);
}

namespace
{
zlink_submit_result_t make_instance_envelope (
  mesh_node_t *node_,
  const instance_placement_value_t &target_,
  bool is_request_,
  const zlink_mesh_operation_id_t &operation_id_,
  uint32_t timeout_ms_,
  const rid_bytes_t &source_node_rid_,
  const rid_bytes_t &source_spot_rid_,
  bool redirected_,
  uint64_t redirected_spot_generation_,
  uint64_t relay_serial_,
  bool has_metadata_,
  std::vector<unsigned char> *envelope_out_)
{
    *envelope_out_ = make_envelope (
      wire_instance_spot, has_metadata_ ? wire_flag_metadata : 0);
    if (!encode_instance_placement (*envelope_out_, target_))
        return errno == ENOMEM ? ZLINK_SUBMIT_OUT_OF_MEMORY
                               : ZLINK_SUBMIT_INVALID_ARGUMENT;

    //  Sender generation fences the physical peer lifetime. Logical source
    //  identity and the full operation id remain unchanged across the one
    //  allowed redirect hop.
    put_u64 (*envelope_out_, node_->lifecycle_generation);
    put_rid (*envelope_out_, source_node_rid_);
    put_rid (*envelope_out_, source_spot_rid_);
    put_u8 (*envelope_out_, static_cast<unsigned char> (
                              is_request_
                                ? ZLINK_INSTANCE_SPOT_OPERATION_REQUEST
                                : ZLINK_INSTANCE_SPOT_OPERATION_SEND));
    put_u64 (*envelope_out_, operation_id_.high);
    put_u64 (*envelope_out_, operation_id_.low);
    put_u32 (*envelope_out_, is_request_ ? timeout_ms_ : 0);
    put_u8 (*envelope_out_, redirected_ ? 1 : 0);
    put_u64 (*envelope_out_, redirected_spot_generation_);
    put_u64 (*envelope_out_, relay_serial_);
    return ZLINK_SUBMIT_OK;
}
}

zlink_submit_result_t wire_submit_instance (
  mesh_node_t *node_,
  const instance_placement_value_t &target_,
  bool is_request_,
  uint64_t correlation_,
  uint32_t timeout_ms_,
  const rid_bytes_t &source_spot_rid_,
  const zlink_mesh_metadata_view_t *metadata_,
  const zlink_msg_t *parts_,
  size_t part_count_,
  zlink_send_flags_t flags_,
  const send_ready_interest_t *interest_,
  uint64_t *connection_id_out_,
  uint64_t expected_connection_id_)
{
    if ((is_request_ && correlation_ == 0) || source_spot_rid_.empty ()) {
        errno = EINVAL;
        return ZLINK_SUBMIT_INVALID_ARGUMENT;
    }
    zlink_mesh_operation_id_t operation_id;
    operation_id.high = is_request_ ? node_->lifecycle_generation : 0;
    operation_id.low = is_request_ ? correlation_ : 0;
    std::vector<unsigned char> envelope;
    const zlink_submit_result_t encode_result = make_instance_envelope (
      node_, target_, is_request_, operation_id, timeout_ms_, node_->routing_id,
      source_spot_rid_, false, 0, 0, metadata_ != NULL, &envelope);
    if (encode_result != ZLINK_SUBMIT_OK)
        return encode_result;

    std::vector<unsigned char> metadata;
    if (metadata_)
        metadata.assign (metadata_->data, metadata_->data + metadata_->size);
#ifdef ZLINK_BUILD_TESTS
    test_pause_instance_wire_before_send ();
#endif
    return send_application_data_message (
      node_, rid_value (target_.node_rid), envelope,
      metadata_ ? &metadata : NULL, parts_, part_count_, flags_, interest_,
      connection_id_out_, expected_connection_id_);
}

zlink_submit_result_t wire_redirect_instance (
  mesh_node_t *node_,
  const instance_placement_value_t &target_,
  uint64_t target_spot_generation_,
  std::unique_ptr<queued_record_t> &record_)
{
    if (!record_.get () || target_spot_generation_ == 0
        || (record_->kind != ZLINK_MESH_RECORD_SPOT_SEND
            && record_->kind != ZLINK_MESH_RECORD_SPOT_REQUEST)) {
        errno = EINVAL;
        return ZLINK_SUBMIT_INVALID_ARGUMENT;
    }

    const bool is_request =
      record_->kind == ZLINK_MESH_RECORD_SPOT_REQUEST;
    if (record_->source_node_rid.empty () || record_->source_spot_rid.empty ()
        || (is_request
            && (record_->operation_id.high == 0
                || record_->operation_id.low == 0))
        || (!is_request
            && (record_->operation_id.high != 0
                || record_->operation_id.low != 0))) {
        errno = EINVAL;
        return ZLINK_SUBMIT_INVALID_ARGUMENT;
    }
    uint64_t relay_serial = 0;
    uint32_t remaining_timeout_ms = 0;
    if (is_request) {
        mesh_node_t *token_node = NULL;
        zlink_mesh_reply_token_t token = record_->reply_token;
        if (!record_->has_reply_token
            || unseal_reply_token (&token, &token_node, &relay_serial) != 0
            || token_node != node_) {
            errno = ESTALE;
            return ZLINK_SUBMIT_INVALID_STATE;
        }
        if (record_->deadline_ns != 0) {
            const uint64_t now_ns =
              zlink::request_timeout::monotonic_now_ns ();
            if (now_ns >= record_->deadline_ns) {
                errno = ETIMEDOUT;
                return ZLINK_SUBMIT_INVALID_STATE;
            }
            const uint64_t remaining_ns = record_->deadline_ns - now_ns;
            const uint64_t remaining_ms =
              (remaining_ns + 999999ULL) / 1000000ULL;
            remaining_timeout_ms = static_cast<uint32_t> (
              std::min<uint64_t> (
                remaining_ms,
                std::numeric_limits<uint32_t>::max ()));
        }
    }

    std::vector<unsigned char> envelope;
    const zlink_submit_result_t encode_result = make_instance_envelope (
      node_, target_, is_request, record_->operation_id,
      remaining_timeout_ms,
      record_->source_node_rid, record_->source_spot_rid, true,
      target_spot_generation_, relay_serial,
      record_->has_metadata, &envelope);
    if (encode_result != ZLINK_SUBMIT_OK)
        return encode_result;

    const zlink_routing_id_t target_rid = rid_value (target_.node_rid);
    if (is_request
        && !prepare_remote_reply_route (
          node_, relay_serial, target_.node_rid))
        return errno == ENOMEM ? ZLINK_SUBMIT_OUT_OF_MEMORY
                               : ZLINK_SUBMIT_INVALID_STATE;
    remote_route_flight_guard_t route_flight (node_, is_request);
    if (!route_flight.valid ())
        return ZLINK_SUBMIT_INTERNAL_ERROR;
    uint64_t expected_connection_id = 0;
    if (!validate_remote_route_flight (
          node_, target_.node_rid, target_.node_generation,
          &expected_connection_id))
        return ZLINK_SUBMIT_NOT_CONNECTED;
#ifdef ZLINK_BUILD_TESTS
    test_pause_instance_wire_before_send ();
#endif
    uint64_t connection_id = 0;
    //  Redirect completes a placement accepted before drain began. It uses
    //  the infrastructure send path so DRAINING does not strand that token;
    //  a stopped or disconnected ROUTER still fails normally.
    const zlink_submit_result_t result = send_data_message (
      node_, target_rid, envelope,
      record_->has_metadata ? &record_->application_metadata : NULL,
      record_->parts.empty () ? NULL : &record_->parts[0],
      record_->parts.size (), ZLINK_SEND_FLAGS_NONE,
      is_request ? &connection_id : NULL, expected_connection_id);
    if (is_request) {
        if (result == ZLINK_SUBMIT_OK)
            (void) commit_remote_reply_route (
              node_, relay_serial, connection_id);
        if (result == ZLINK_SUBMIT_OK)
            route_flight.release ();
    }
    if (result == ZLINK_SUBMIT_OK) {
        if (record_->instance_deadline_task) {
            zlink::request_timeout::cancel (
              record_->instance_deadline_task);
            record_->instance_deadline_task.reset ();
        }
        record_.reset ();
    }
    return result;
}

zlink_submit_result_t wire_publish_remote (mesh_node_t *node_,
                                           const std::vector<rid_bytes_t> &targets_,
                                           const std::string &channel_,
                                           const std::string &topic_,
                                           const rid_bytes_t &source_spot_rid_,
                                           const zlink_mesh_metadata_view_t *metadata_,
                                           const zlink_msg_t *parts_,
                                           size_t part_count_,
                                           zlink_send_flags_t flags_,
                                           uint32_t *admitted_out_,
                                           uint32_t *dropped_out_,
                                           uint32_t *unreachable_out_)
{
    *admitted_out_ = 0;
    *dropped_out_ = 0;
    *unreachable_out_ = 0;
    if (targets_.empty ())
        return ZLINK_SUBMIT_OK;

    std::vector<unsigned char> envelope =
      make_envelope (wire_multicast, metadata_ ? wire_flag_metadata : 0);
    put_u8 (envelope, static_cast<unsigned char> (channel_.size ()));
    put_bytes (envelope, channel_.data (), channel_.size ());
    put_u8 (envelope, static_cast<unsigned char> (topic_.size ()));
    put_bytes (envelope, topic_.data (), topic_.size ());
    put_rid (envelope, source_spot_rid_);
    std::vector<unsigned char> metadata;
    if (metadata_)
        metadata.assign (metadata_->data, metadata_->data + metadata_->size);

    //  Each target takes the normal per-message ROUTER send lock. Releasing
    //  it between targets prevents one fan-out from reserving the whole
    //  outbound path while preserving multipart framing per target.
    zlink_submit_result_t aggregate_rc = ZLINK_SUBMIT_OK;
    int aggregate_errno = 0;
    for (size_t i = 0; i < targets_.size (); ++i) {
        const zlink_routing_id_t target = rid_value (targets_[i]);
        const zlink_submit_result_t rc =
          send_application_data_message (
            node_, target, envelope, metadata_ ? &metadata : NULL, parts_, part_count_, flags_);
        if (rc == ZLINK_SUBMIT_OK)
            *admitted_out_ += 1;
        else if (rc == ZLINK_SUBMIT_NOT_CONNECTED)
            *unreachable_out_ += 1;
        else {
            *dropped_out_ += 1;
            if (rc == ZLINK_SUBMIT_BACKPRESSURED) {
                if (aggregate_rc == ZLINK_SUBMIT_OK) {
                    aggregate_rc = rc;
                    aggregate_errno = errno;
                }
            } else {
                //  A hard failure is more actionable than capacity pressure
                //  observed at an earlier target.
                aggregate_rc = rc;
                aggregate_errno = errno;
                *dropped_out_ += static_cast<uint32_t> (targets_.size () - i - 1);
                break;
            }
        }
    }
    if (aggregate_rc != ZLINK_SUBMIT_OK)
        errno = aggregate_errno;
    return aggregate_rc;
}

zlink_submit_result_t wire_submit_actor_data (mesh_node_t *node_,
                                              const rid_bytes_t &peer_rid_,
                                              bool is_request_,
                                              uint64_t correlation_,
                                              const zlink_actor_ref_t *source_actor_,
                                              const zlink_actor_ref_t &target_actor_,
                                              const zlink_mesh_metadata_view_t *metadata_,
                                              const zlink_msg_t *parts_,
                                              size_t part_count_,
                                              zlink_send_flags_t flags_,
                                              const send_ready_interest_t *interest_)
{
    std::vector<unsigned char> envelope = make_envelope (
      static_cast<unsigned char> (is_request_ ? wire_actor_request : wire_actor_send),
      metadata_ ? wire_flag_metadata : 0);
    if (is_request_)
        put_u64 (envelope, correlation_);
    if (source_actor_) {
        put_actor_ref (envelope, *source_actor_);
    } else {
        put_u8 (envelope, 0);
    }
    put_actor_ref (envelope, target_actor_);
    std::vector<unsigned char> metadata;
    if (metadata_)
        metadata.assign (metadata_->data, metadata_->data + metadata_->size);

    const zlink_routing_id_t target = rid_value (peer_rid_);
    return send_application_data_message (
      node_, target, envelope, metadata_ ? &metadata : NULL, parts_, part_count_,
      flags_, interest_);
}

zlink_submit_result_t wire_submit_bound_actor_data (
  mesh_node_t *node_,
  const rid_bytes_t &peer_rid_,
  bool is_request_,
  uint64_t correlation_,
  const rid_bytes_t &source_session_rid_,
  uint64_t source_binding_generation_,
  const zlink_actor_ref_t &target_actor_,
  const zlink_mesh_metadata_view_t *metadata_,
  const zlink_msg_t *parts_,
  size_t part_count_,
  zlink_send_flags_t flags_,
  const send_ready_interest_t *interest_)
{
    std::vector<unsigned char> envelope = make_envelope (
      static_cast<unsigned char> (is_request_ ? wire_actor_request
                                              : wire_actor_send),
      static_cast<unsigned char> (
        wire_flag_bound_session | wire_flag_source_spot_rid
        | (metadata_ ? wire_flag_metadata : 0)));
    if (is_request_)
        put_u64 (envelope, correlation_);
    put_u8 (envelope, 0);
    put_actor_ref (envelope, target_actor_);
    put_rid (envelope, source_session_rid_);
    put_u64 (envelope, source_binding_generation_);
    std::vector<unsigned char> metadata;
    if (metadata_)
        metadata.assign (metadata_->data, metadata_->data + metadata_->size);
    const zlink_routing_id_t target = rid_value (peer_rid_);
    return send_application_data_message (
      node_, target, envelope, metadata_ ? &metadata : NULL, parts_, part_count_,
      flags_, interest_);
}

zlink_submit_result_t wire_submit_bound_session (
  mesh_node_t *node_,
  const rid_bytes_t &peer_rid_,
  const zlink_actor_ref_t &actor_,
  uint64_t expected_binding_generation_,
  const zlink_msg_t *parts_,
  size_t part_count_,
  zlink_send_flags_t flags_)
{
    std::vector<unsigned char> envelope =
      make_envelope (wire_bound_session_send, 0);
    put_actor_ref (envelope, actor_);
    put_u64 (envelope, expected_binding_generation_);
    const zlink_routing_id_t target = rid_value (peer_rid_);
    return send_application_data_message (
      node_, target, envelope, NULL, parts_, part_count_, flags_);
}

zlink_submit_result_t wire_submit_bound_session_bind (
  mesh_node_t *node_,
  const rid_bytes_t &peer_rid_,
  uint64_t correlation_,
  const zlink_actor_ref_t &actor_,
  uint64_t binding_generation_,
  uint64_t retired_binding_generation_)
{
    std::vector<unsigned char> envelope =
      make_envelope (wire_bound_session_bind, 0);
    put_u64 (envelope, correlation_);
    put_actor_ref (envelope, actor_);
    put_u64 (envelope, binding_generation_);
    put_u64 (envelope, retired_binding_generation_);
    const zlink_routing_id_t target = rid_value (peer_rid_);
    return send_application_data_message (
      node_, target, envelope, NULL, NULL, 0, ZLINK_SEND_FLAGS_NONE);
}

void wire_submit_bound_session_bind_reply (
  mesh_node_t *node_,
  const rid_bytes_t &peer_rid_,
  uint64_t correlation_,
  int32_t terminal_result_,
  int32_t failure_errno_,
  uint64_t binding_generation_,
  uint64_t membership_epoch_)
{
    std::vector<unsigned char> envelope = make_envelope (wire_reply, 0);
    put_u64 (envelope, correlation_);
    put_u32 (envelope, static_cast<uint32_t> (terminal_result_));
    put_u32 (envelope, static_cast<uint32_t> (failure_errno_));
    put_u64 (envelope, binding_generation_);
    put_u64 (envelope, membership_epoch_);
    const zlink_routing_id_t target = rid_value (peer_rid_);
    (void) send_application_data_message (
      node_, target, envelope, NULL, NULL, 0, ZLINK_SEND_FLAGS_NONE);
}

void wire_broadcast_bound_session_bind (mesh_node_t *node_,
                                        const zlink_actor_ref_t &actor_,
                                        uint64_t binding_generation_,
                                        uint64_t retired_binding_generation_)
try
{
    std::vector<rid_bytes_t> targets;
    {
        std::lock_guard<std::mutex> lock (node_->mutex);
        targets.reserve (node_->peers.size ());
        for (size_t i = 0; i < node_->peers.size (); ++i) {
            if (node_->peers[i].state == ZLINK_MESH_PEER_ADMITTED)
                targets.push_back (node_->peers[i].rid);
        }
    }
    for (size_t i = 0; i < targets.size (); ++i)
        (void) wire_submit_bound_session_bind (
          node_, targets[i], 0, actor_, binding_generation_,
          retired_binding_generation_);
}
catch (const std::bad_alloc &)
{
    //  The physical session owner remains the authority. Missing a cache
    //  announcement can make a peer report NOT_FOUND, but cannot permit a
    //  stale generation to reach a replacement binding.
}

zlink_submit_result_t wire_submit_actor_lookup (mesh_node_t *node_,
                                                const rid_bytes_t &peer_rid_,
                                                uint64_t correlation_,
                                                const std::string &actor_id_)
{
    std::vector<unsigned char> envelope = make_envelope (wire_actor_lookup, 0);
    put_u64 (envelope, correlation_);
    put_u8 (envelope, static_cast<unsigned char> (actor_id_.size ()));
    put_bytes (envelope, actor_id_.data (), actor_id_.size ());
    const zlink_routing_id_t target = rid_value (peer_rid_);
    return send_application_data_message (
      node_, target, envelope, NULL, NULL, 0, ZLINK_SEND_FLAGS_NONE);
}

zlink_submit_result_t wire_submit_actor_destroy (mesh_node_t *node_,
                                                 const rid_bytes_t &peer_rid_,
                                                 uint64_t correlation_,
                                                 const zlink_actor_ref_t &actor_)
{
    std::vector<unsigned char> envelope = make_envelope (wire_actor_destroy, 0);
    put_u64 (envelope, correlation_);
    put_actor_ref (envelope, actor_);
    const zlink_routing_id_t target = rid_value (peer_rid_);
    return send_application_data_message (
      node_, target, envelope, NULL, NULL, 0, ZLINK_SEND_FLAGS_NONE);
}

zlink_submit_result_t wire_submit_actor_join (mesh_node_t *node_,
                                              const rid_bytes_t &peer_rid_,
                                              uint64_t correlation_,
                                              const zlink_actor_ref_t &actor_,
                                              bool entry_,
                                              const rid_bytes_t &target_spot_rid_,
                                              uint64_t target_spot_generation_,
                                              const zlink_msg_t *creation_parts_,
                                              size_t creation_part_count_)
{
    std::vector<unsigned char> envelope = make_envelope (wire_actor_join, 0);
    put_u64 (envelope, correlation_);
    put_actor_ref (envelope, actor_);
    put_u8 (envelope, entry_ ? 1 : 0);
    put_rid (envelope, target_spot_rid_);
    put_u64 (envelope, target_spot_generation_);
    const zlink_routing_id_t target = rid_value (peer_rid_);
    return send_application_data_message (
      node_, target, envelope, NULL, creation_parts_, creation_part_count_,
      ZLINK_SEND_FLAGS_NONE);
}

void wire_notify_actor_left (mesh_node_t *node_,
                             const rid_bytes_t &peer_rid_,
                             const zlink_actor_ref_t &actor_,
                             const rid_bytes_t &previous_spot_rid_,
                             uint64_t previous_spot_generation_,
                             uint64_t previous_membership_epoch_,
                             uint64_t current_membership_epoch_)
{
    std::vector<unsigned char> envelope = make_envelope (wire_actor_left, 0);
    put_actor_ref (envelope, actor_);
    put_rid (envelope, previous_spot_rid_);
    put_u64 (envelope, previous_spot_generation_);
    put_u64 (envelope, previous_membership_epoch_);
    put_u64 (envelope, current_membership_epoch_);
    const zlink_routing_id_t target = rid_value (peer_rid_);
    send_control (node_, target, envelope);
}

void wire_notify_actor_joined (mesh_node_t *node_,
                               const rid_bytes_t &peer_rid_,
                               const zlink_actor_ref_t &actor_,
                               const rid_bytes_t &previous_spot_rid_,
                               uint64_t previous_spot_generation_,
                               uint64_t previous_membership_epoch_,
                               const rid_bytes_t &current_spot_rid_,
                               uint64_t current_spot_generation_,
                               uint64_t current_membership_epoch_)
{
    std::vector<unsigned char> envelope = make_envelope (wire_actor_joined, 0);
    put_actor_ref (envelope, actor_);
    put_rid (envelope, previous_spot_rid_);
    put_u64 (envelope, previous_spot_generation_);
    put_u64 (envelope, previous_membership_epoch_);
    put_rid (envelope, current_spot_rid_);
    put_u64 (envelope, current_spot_generation_);
    put_u64 (envelope, current_membership_epoch_);
    const zlink_routing_id_t target = rid_value (peer_rid_);
    (void) send_application_data_message (
      node_, target, envelope, NULL, NULL, 0, ZLINK_SEND_FLAGS_NONE);
}

zlink_submit_result_t wire_submit_join_reply (mesh_node_t *node_,
                                              const rid_bytes_t &peer_rid_,
                                              uint64_t correlation_,
                                              uint32_t join_result_,
                                              const rid_bytes_t &spot_rid_,
                                              uint64_t spot_generation_,
                                              const zlink_msg_t *parts_,
                                              size_t part_count_,
                                              zlink_send_flags_t flags_)
{
    std::vector<unsigned char> envelope = make_envelope (wire_reply, 0);
    put_u64 (envelope, correlation_);
    put_u32 (envelope, static_cast<uint32_t> (join_result_ == ZLINK_ACTOR_JOIN_ACCEPTED
                                                ? ZLINK_REQUEST_OK
                                                : ZLINK_REQUEST_REJECTED));
    put_u32 (envelope, join_result_ == ZLINK_ACTOR_JOIN_ACCEPTED ? 0
                                                                 : static_cast<uint32_t> (EACCES));
    put_u32 (envelope, join_result_);
    put_rid (envelope, spot_rid_);
    put_u64 (envelope, spot_generation_);
    const zlink_routing_id_t target = rid_value (peer_rid_);
    //  The public join-reply contract owns the flags: DONTWAIT surfaces
    //  EAGAIN and a blocking call waits for SNDTIMEO before ETIMEDOUT, both
    //  leaving the reply token retryable (04-actor §3).
    return send_data_message (node_, target, envelope, NULL, parts_, part_count_, flags_);
}

zlink_submit_result_t wire_submit_lookup_reply (mesh_node_t *node_,
                                                const rid_bytes_t &peer_rid_,
                                                uint64_t correlation_,
                                                const zlink_actor_ref_t &ref_,
                                                const rid_bytes_t &spot_rid_,
                                                uint64_t spot_generation_,
                                                uint64_t membership_epoch_)
{
    std::vector<unsigned char> envelope = make_envelope (wire_reply, 0);
    put_u64 (envelope, correlation_);
    put_u32 (envelope, static_cast<uint32_t> (ZLINK_REQUEST_OK));
    put_u32 (envelope, 0);
    put_actor_ref (envelope, ref_);
    put_rid (envelope, spot_rid_);
    put_u64 (envelope, spot_generation_);
    put_u64 (envelope, membership_epoch_);
    const zlink_routing_id_t target = rid_value (peer_rid_);
    return send_data_message (node_, target, envelope, NULL, NULL, 0, ZLINK_SEND_FLAGS_NONE);
}

zlink_submit_result_t wire_submit_transfer_ready (mesh_node_t *node_,
                                                  const rid_bytes_t &peer_rid_,
                                                  const zlink_actor_transfer_id_t &transfer_id_,
                                                  const zlink_actor_ref_t &actor_,
                                                  uint64_t expected_epoch_,
                                                  uint64_t final_sequence_,
                                                  uint8_t role_,
                                                  uint64_t offered_messages_,
                                                  uint64_t offered_bytes_,
                                                  const std::vector<transfer_participant_descriptor_t>
                                                    &participants_)
{
    std::vector<unsigned char> envelope = make_envelope (wire_transfer_ready, 0);
    put_transfer_id (envelope, transfer_id_);
    put_actor_ref (envelope, actor_);
    put_u64 (envelope, expected_epoch_);
    put_u64 (envelope, final_sequence_);
    put_u8 (envelope, role_);
    put_u64 (envelope, offered_messages_);
    put_u64 (envelope, offered_bytes_);
    put_u32 (envelope, static_cast<uint32_t> (participants_.size ()));
    for (size_t i = 0; i < participants_.size (); ++i) {
        put_u64 (envelope, participants_[i].participant_id);
        put_u64 (envelope, participants_[i].binding_generation);
        put_u64 (envelope, participants_[i].allowance_messages);
        put_u64 (envelope, participants_[i].allowance_bytes);
    }
    const zlink_routing_id_t target = rid_value (peer_rid_);
    return send_data_message (node_, target, envelope, NULL, NULL, 0, ZLINK_SEND_FLAGS_NONE);
}

zlink_submit_result_t wire_submit_transfer_data (mesh_node_t *node_,
                                                 const rid_bytes_t &peer_rid_,
                                                 const zlink_actor_transfer_id_t &transfer_id_,
                                                 uint64_t participant_id_,
                                                 uint64_t sequence_,
                                                 const queued_record_t &record_,
                                                 uint64_t relay_serial_)
{
    std::vector<unsigned char> envelope = make_envelope (wire_transfer_data, 0);
    put_transfer_id (envelope, transfer_id_);
    put_u64 (envelope, participant_id_);
    put_u64 (envelope, sequence_);
    put_record_header (envelope, record_, relay_serial_);
    const zlink_routing_id_t target = rid_value (peer_rid_);
    return send_data_message (node_, target, envelope, NULL,
                              record_.parts.empty () ? NULL : &record_.parts[0],
                              record_.parts.size (), ZLINK_SEND_FLAGS_NONE);
}

zlink_submit_result_t wire_submit_transfer_ack (mesh_node_t *node_,
                                                const rid_bytes_t &peer_rid_,
                                                const zlink_actor_transfer_id_t &transfer_id_,
                                                uint64_t participant_id_,
                                                uint64_t high_water_)
{
    std::vector<unsigned char> envelope = make_envelope (wire_transfer_ack, 0);
    put_transfer_id (envelope, transfer_id_);
    put_u64 (envelope, participant_id_);
    put_u64 (envelope, high_water_);
    const zlink_routing_id_t target = rid_value (peer_rid_);
    return send_data_message (node_, target, envelope, NULL, NULL, 0, ZLINK_SEND_FLAGS_NONE);
}

zlink_submit_result_t wire_submit_transfer_seal (
  mesh_node_t *node_,
  const rid_bytes_t &peer_rid_,
  const zlink_actor_transfer_id_t &transfer_id_,
  bool response_,
  const std::vector<transfer_participant_terminal_t> &terminals_)
{
    std::vector<unsigned char> envelope = make_envelope (wire_transfer_seal, 0);
    put_transfer_id (envelope, transfer_id_);
    put_u8 (envelope, response_ ? 1 : 0);
    put_u32 (envelope, static_cast<uint32_t> (terminals_.size ()));
    for (size_t i = 0; i < terminals_.size (); ++i) {
        put_u64 (envelope, terminals_[i].participant_id);
        put_u64 (envelope, terminals_[i].high_water);
    }
    const zlink_routing_id_t target = rid_value (peer_rid_);
    return send_data_message (node_, target, envelope, NULL, NULL, 0, ZLINK_SEND_FLAGS_NONE);
}

zlink_submit_result_t wire_submit_transfer_complete (
  mesh_node_t *node_,
  const rid_bytes_t &peer_rid_,
  const zlink_actor_transfer_id_t &transfer_id_)
{
    std::vector<unsigned char> envelope = make_envelope (wire_transfer_complete, 0);
    put_transfer_id (envelope, transfer_id_);
    const zlink_routing_id_t target = rid_value (peer_rid_);
    return send_data_message (node_, target, envelope, NULL, NULL, 0, ZLINK_SEND_FLAGS_NONE);
}

zlink_submit_result_t wire_submit_reply_relay (mesh_node_t *node_,
                                               const rid_bytes_t &peer_rid_,
                                               uint64_t relay_serial_,
                                               int32_t terminal_result_,
                                               int32_t failure_errno_,
                                               const zlink_msg_t *parts_,
                                               size_t part_count_,
                                               uint64_t expected_connection_id_)
{
    std::vector<unsigned char> envelope = make_envelope (wire_reply_relay, 0);
    put_u64 (envelope, relay_serial_);
    put_u32 (envelope, static_cast<uint32_t> (terminal_result_));
    put_u32 (envelope, static_cast<uint32_t> (failure_errno_));
    const zlink_routing_id_t target = rid_value (peer_rid_);
    return send_data_message (node_, target, envelope, NULL, parts_, part_count_,
                              ZLINK_SEND_FLAGS_NONE, NULL,
                              expected_connection_id_);
}

zlink_submit_result_t wire_submit_reply (mesh_node_t *node_,
                                         const rid_bytes_t &peer_rid_,
                                         uint64_t correlation_,
                                         int32_t terminal_result_,
                                         int32_t failure_errno_,
                                         const zlink_msg_t *parts_,
                                         size_t part_count_,
                                         uint64_t expected_connection_id_)
{
    std::vector<unsigned char> envelope = make_envelope (wire_reply, 0);
    put_u64 (envelope, correlation_);
    put_u32 (envelope, static_cast<uint32_t> (terminal_result_));
    put_u32 (envelope, static_cast<uint32_t> (failure_errno_));

    const zlink_routing_id_t target = rid_value (peer_rid_);
    return send_data_message (node_, target, envelope, NULL, parts_, part_count_,
                              ZLINK_SEND_FLAGS_NONE, NULL,
                              expected_connection_id_);
}
}
}
