/* SPDX-License-Identifier: MPL-2.0 */

#include "precompiled.hpp"

#include "services/spot/spot_data_plane.hpp"
#include "services/spot/spot_runtime_internal.hpp"
#include "services/spot/spot_node.hpp"
#include "services/spot/spot_runtime.hpp"
#include "services/control/service_control_runtime.hpp"

#include "core/recv_internal.hpp"
#include "sockets/socket_base.hpp"
#include "utils/sleep.hpp"
#include "utils/random.hpp"

namespace zlink
{
namespace
{
static const int spot_runtime_default_close_timeout_ms = 2000;

static uint32_t resolve_spot_data_runtime_interval_ms ()
{
    const char *raw = std::getenv ("ZLINK_SPOT_DATA_RUNTIME_INTERVAL_MS");
    if (!raw || !*raw)
        return 1;

    char *end = NULL;
    const unsigned long parsed = std::strtoul (raw, &end, 10);
    if (!end || *end != '\0' || parsed == 0)
        return 1;
    return static_cast<uint32_t> (parsed);
}
}

void preserve_first_error_local (int rc_, int *first_error_)
{
    if (rc_ == 0 || !first_error_ || *first_error_ != 0)
        return;
    *first_error_ = errno != 0 ? errno : EIO;
}

int send_ascii_frame_local (socket_base_t *socket_,
                            const std::string &value_,
                            int flags_)
{
    msg_t msg;
    if (msg.init_size (value_.size ()) != 0)
        return -1;
    if (!value_.empty ())
        memcpy (msg.data (), value_.data (), value_.size ());
    const int rc = socket_->send (&msg, flags_);
    msg.close ();
    return rc;
}

void close_socket_ptr_local (socket_base_t **socket_p_)
{
    if (socket_p_ && *socket_p_) {
        socket_base_t *socket = *socket_p_;
        socket->stop ();
        socket->close ();
        *socket_p_ = NULL;
    }
}

bool spot_shutdown_debug_enabled_local ()
{
    return std::getenv ("ZLINK_DEBUG_SPOT_SHUTDOWN") != NULL;
}

size_t fill_runtime_socket_slot_refs (spot_runtime_t *runtime_,
                                      runtime_socket_slot_ref_t *out_)
{
    if (!runtime_ || !out_)
        return 0;

    size_t count = 0;
    out_[count++] =
      runtime_socket_slot_ref_t{&runtime_->data_ctrl_front,
                                &runtime_->data_ctrl_endpoint, false};
    out_[count++] =
      runtime_socket_slot_ref_t{&runtime_->data_ctrl_back,
                                &runtime_->data_ctrl_endpoint, false};
    out_[count++] =
      runtime_socket_slot_ref_t{&runtime_->mesh_pub, NULL, false};
    out_[count++] =
      runtime_socket_slot_ref_t{&runtime_->mesh_xsub, NULL, false};
    out_[count++] =
      runtime_socket_slot_ref_t{&runtime_->peer_ctrl_pub,
                                &runtime_->peer_ctrl_endpoint, false};
    out_[count++] =
      runtime_socket_slot_ref_t{&runtime_->peer_ctrl_sub, NULL, false};
    out_[count++] =
      runtime_socket_slot_ref_t{&runtime_->route_ingress,
                                &runtime_->route_ingress_endpoint, false};
    out_[count++] =
      runtime_socket_slot_ref_t{&runtime_->peer_route_ingress, NULL, false};
    out_[count++] =
      runtime_socket_slot_ref_t{&runtime_->node_router,
                                &runtime_->node_router_endpoint, false};
    out_[count++] =
      runtime_socket_slot_ref_t{&runtime_->route_ingress_tx,
                                &runtime_->route_ingress_sender_endpoint, true};
    out_[count++] =
      runtime_socket_slot_ref_t{&runtime_->node_router_tx,
                                &runtime_->node_router_sender_endpoint, true};
    out_[count++] =
      runtime_socket_slot_ref_t{&runtime_->peer_route_tx,
                                &runtime_->peer_route_sender_endpoint, true};
    out_[count++] =
      runtime_socket_slot_ref_t{&runtime_->local_pub_ingress_sub,
                                &runtime_->pub_ingress_endpoint, false};
    out_[count++] =
      runtime_socket_slot_ref_t{&runtime_->local_fanout_xpub,
                                &runtime_->sub_fanout_endpoint, false};
    return count;
}

namespace
{

static void spot_runtime_diag_logf_local (const char *fmt_, ...)
{
    if (!std::getenv ("ZLINK_DEBUG_SPOT_RUNTIME_ATTACH"))
        return;

    va_list args;
    va_start (args, fmt_);
    std::fprintf (stderr, "[spot-runtime] ");
    std::vfprintf (stderr, fmt_, args);
    std::fprintf (stderr, "\n");
    std::fflush (stderr);
    va_end (args);
}

static bool socket_route_sender_ready_local (socket_base_t *socket_,
                                             const std::string &endpoint_)
{
    if (!socket_ || endpoint_.empty ())
        return false;

    if (!socket_->socket_has_attached_pipes ())
        return false;

    std::vector<std::string> remote_endpoints;
    socket_->socket_peer_remote_endpoints (&remote_endpoints);
    for (size_t i = 0; i < remote_endpoints.size (); ++i) {
        if (remote_endpoints[i] == endpoint_)
            return true;
    }
    return remote_endpoints.empty ();
}

static socket_base_t *&sender_socket_slot_local (spot_runtime_t *runtime_,
                                                 spot_runtime_sender_kind_t kind_)
{
    return kind_ == spot_runtime_sender_node_router ? runtime_->node_router_tx
                                                    : runtime_->route_ingress_tx;
}

static std::string &sender_endpoint_cache_local (spot_runtime_t *runtime_,
                                                 spot_runtime_sender_kind_t kind_)
{
    return kind_ == spot_runtime_sender_node_router
             ? runtime_->node_router_sender_endpoint
             : runtime_->route_ingress_sender_endpoint;
}

static const std::string &sender_target_endpoint_local (
  const spot_runtime_t *runtime_,
  spot_runtime_sender_kind_t kind_)
{
    return kind_ == spot_runtime_sender_node_router
             ? runtime_->node_router_endpoint
             : runtime_->route_ingress_endpoint;
}

}

spot_runtime_t::spot_runtime_t (spot_node_t *owner_) :
    owner (owner_),
    data_ctrl_front (NULL),
    data_ctrl_back (NULL),
    mesh_pub (NULL),
    mesh_xsub (NULL),
    peer_ctrl_pub (NULL),
    peer_ctrl_sub (NULL),
    route_ingress (NULL),
    peer_route_ingress (NULL),
    node_router (NULL),
    route_ingress_tx (NULL),
    node_router_tx (NULL),
    peer_route_tx (NULL),
    peer_route_sender_ready_after_ms (0),
    local_pub_ingress_sub (NULL),
    local_fanout_xpub (NULL),
    data_plane_runtime (NULL),
    stop (0),
    node_id (generate_random ()),
    faulted (false),
    fault_errno (0),
    abortive_shutdown (false),
    shutdown_phase_value (spot_shutdown_phase_running),
    next_attachment_id (0)
{
    if (node_id == 0)
        node_id = 1;

    char buf[128];
    snprintf (buf, sizeof (buf), "inproc://zlink.spot.%u.pub-in", node_id);
    pub_ingress_endpoint = buf;
    snprintf (buf, sizeof (buf), "inproc://zlink.spot.%u.sub-out", node_id);
    sub_fanout_endpoint = buf;
    snprintf (buf, sizeof (buf), "inproc://zlink.spot.%u.route-in", node_id);
    route_ingress_endpoint = buf;
    snprintf (buf, sizeof (buf), "inproc://zlink.spot.%u.node-router", node_id);
    node_router_endpoint = buf;
    snprintf (buf, sizeof (buf), "inproc://zlink.spot.%u.ctrl", node_id);
    data_ctrl_endpoint = buf;
}

spot_node_hwm_config_t spot_runtime_t::hwm_config_snapshot () const
{
    scoped_lock_t lock (const_cast<mutex_t &> (hwm_config_sync));
    return hwm_config;
}

void spot_runtime_t::set_hwm_config (const spot_node_hwm_config_t &config_)
{
    scoped_lock_t lock (hwm_config_sync);
    hwm_config = config_;
}

int spot_runtime_t::create_attachment (int kind_,
                                       const char *endpoint_,
                                       uint64_t *out_id_)
{
    if (!owner || !owner->_ctx || !endpoint_ || !out_id_) {
        errno = EFAULT;
        return -1;
    }
    if (kind_ != spot_attachment_pub && kind_ != spot_attachment_sub) {
        errno = EINVAL;
        return -1;
    }

    const int socket_type =
      kind_ == spot_attachment_pub ? ZLINK_CORE_SOCKET_PUB
                                   : ZLINK_CORE_SOCKET_SUB;
    socket_base_t *socket = owner->_ctx->create_socket (socket_type);
    if (!socket)
        return -1;

    owner->track_owned_socket (socket);
    if (socket->connect (endpoint_) != 0) {
        (void) close_runtime_socket (socket, 1000);
        return -1;
    }

    spot_attachment_t attachment;
    {
        scoped_lock_t lock (attachment_sync);
        attachment.id = ++next_attachment_id;
        if (attachment.id == 0)
            attachment.id = ++next_attachment_id;
        attachment.kind = kind_;
        attachment.socket = socket;
        attachment.endpoint = endpoint_;
        attachments[attachment.id] = attachment;
    }
    *out_id_ = attachment.id;
    return 0;
}

int spot_runtime_t::close_runtime_socket (socket_base_t *&socket_,
                                          int timeout_ms_)
{
    if (!socket_)
        return 0;
    if (owner && owner->is_shutting_down ()) {
        socket_->stop ();
        socket_->close ();
        owner->untrack_owned_socket (socket_);
        socket_ = NULL;
        return 0;
    }
    if (owner && owner->_ctx)
        return owner->_lifecycle.close_socket_and_wait (socket_, timeout_ms_);

    socket_->stop ();
    socket_->close ();
    socket_ = NULL;
    return 0;
}

int spot_runtime_t::close_runtime_socket_async (socket_base_t *&socket_,
                                                int timeout_ms_)
{
    if (!socket_)
        return 0;
    if (owner && owner->is_shutting_down ()) {
        socket_->stop ();
        socket_->close ();
        owner->untrack_owned_socket (socket_);
        socket_ = NULL;
        return 0;
    }
    if (owner && owner->_ctx)
        return owner->_lifecycle.close_socket (socket_, timeout_ms_);

    socket_->stop ();
    socket_->close ();
    socket_ = NULL;
    return 0;
}

socket_base_t *spot_runtime_t::attachment_socket (uint64_t id_) const
{
    if (id_ == 0)
        return NULL;
    scoped_lock_t lock (const_cast<mutex_t &> (attachment_sync));
    std::map<uint64_t, spot_attachment_t>::const_iterator it =
      attachments.find (id_);
    return it != attachments.end () ? it->second.socket : NULL;
}

int spot_runtime_t::destroy_attachment (uint64_t id_)
{
    if (id_ == 0)
        return 0;

    socket_base_t *socket = NULL;
    std::string endpoint;
    {
        scoped_lock_t lock (attachment_sync);
        std::map<uint64_t, spot_attachment_t>::iterator it =
          attachments.find (id_);
        if (it == attachments.end ())
            return 0;
        socket = it->second.socket;
        endpoint = it->second.endpoint;
        attachments.erase (it);
    }

    if (!socket)
        return 0;
    spot_runtime_diag_logf_local ("destroy_attachment id=%llu socket=%d endpoint=%s",
                                  static_cast<unsigned long long> (id_),
                                  socket->socket_id (), endpoint.c_str ());
    if (!endpoint.empty ())
        (void) socket->term_endpoint (endpoint.c_str ());
    socket->set_all_pipes_nodelay ();
    return close_runtime_socket (socket, 10000);
}

int spot_runtime_t::destroy_attachment_async (uint64_t id_)
{
    if (id_ == 0)
        return 0;

    socket_base_t *socket = NULL;
    std::string endpoint;
    {
        scoped_lock_t lock (attachment_sync);
        std::map<uint64_t, spot_attachment_t>::iterator it =
          attachments.find (id_);
        if (it == attachments.end ())
            return 0;
        socket = it->second.socket;
        endpoint = it->second.endpoint;
        attachments.erase (it);
    }

    if (!socket)
        return 0;
    spot_runtime_diag_logf_local (
      "destroy_attachment_async id=%llu socket=%d endpoint=%s",
      static_cast<unsigned long long> (id_), socket->socket_id (),
      endpoint.c_str ());
    if (!endpoint.empty ())
        (void) socket->term_endpoint (endpoint.c_str ());
    socket->set_all_pipes_nodelay ();
    return close_runtime_socket_async (socket, 10000);
}

int spot_runtime_t::start ()
{
    if (!owner || !owner->_ctx) {
        errno = EFAULT;
        return -1;
    }

    data_ctrl_front = owner->_ctx->create_socket (ZLINK_CORE_SOCKET_PAIR);
    if (!data_ctrl_front
        || data_ctrl_front->bind (data_ctrl_endpoint.c_str ()) != 0) {
        close_socket_ptr_local (&data_ctrl_front);
        return -1;
    }
    owner->track_owned_socket (data_ctrl_front);

    const int linger = 0;
    const int timeout = 2000;
    data_ctrl_front->setsockopt (ZLINK_INTERNAL_OPT_LINGER, &linger,
                                 sizeof (linger));
    data_ctrl_front->setsockopt (ZLINK_INTERNAL_OPT_SNDTIMEO, &timeout,
                                 sizeof (timeout));
    data_ctrl_front->setsockopt (ZLINK_INTERNAL_OPT_RCVTIMEO, &timeout,
                                 sizeof (timeout));

    int worker_errno = 0;
    if (spot_data_plane_t::initialize_runtime (owner, this,
                                               &execution.data_plane_state)
        != 0
        || !spot_node_t::recv_ctrl_reply (data_ctrl_front, &worker_errno)) {
        errno = worker_errno != 0 ? worker_errno : ETIMEDOUT;
        stop.set (1);
        stop_sockets ();
        spot_data_plane_t::teardown_runtime (
          owner, this, &execution.data_plane_state,
          &execution.data_plane_protocol_state);
        if (owner)
            owner->untrack_owned_socket (data_ctrl_front);
        close_socket_ptr_local (&data_ctrl_front);
        return -1;
    }

    data_plane_runtime = owner->_ctx->service_data_runtime_for_key (node_id);
    if (!data_plane_runtime) {
        errno = ETERM;
        stop.set (1);
        stop_sockets ();
        spot_data_plane_t::teardown_runtime (
          owner, this, &execution.data_plane_state,
          &execution.data_plane_protocol_state);
        return -1;
    }

    const uint64_t task_id =
      data_plane_runtime->add_periodic_task (&spot_data_plane_t::task_entry,
                                             owner,
                                             resolve_spot_data_runtime_interval_ms (),
                                             true);
    if (task_id == 0) {
        stop.set (1);
        stop_sockets ();
        spot_data_plane_t::teardown_runtime (
          owner, this, &execution.data_plane_state,
          &execution.data_plane_protocol_state);
        return -1;
    }
    execution.data_plane_task_id_value = task_id;
    execution.data_plane_running = true;
    return 0;
}

int spot_runtime_t::ensure_healthy () const
{
    if (stop.get () != 0 || faulted) {
        errno = EFSM;
        return -1;
    }
    return 0;
}


int spot_runtime_t::send_command (const char *verb_, const char *arg_) const
{
    if (!data_ctrl_front) {
        errno = EFAULT;
        return -1;
    }

    scoped_lock_t lock (const_cast<mutex_t &> (ctrl_sync));
    if (send_ascii_frame_local (data_ctrl_front, verb_,
                                arg_ ? ZLINK_SNDMORE : 0)
        != 0)
        return -1;
    if (arg_ && send_ascii_frame_local (data_ctrl_front, arg_, 0) != 0)
        return -1;

    if (data_plane_runtime && execution.data_plane_task_id_value != 0)
        data_plane_runtime->wakeup_task (execution.data_plane_task_id_value);

    int reply_errno = 0;
    if (!spot_node_t::recv_ctrl_reply (data_ctrl_front, &reply_errno)) {
        errno = reply_errno != 0 ? reply_errno : ETIMEDOUT;
        return -1;
    }
    return 0;
}

void spot_runtime_t::mark_fault (int err_)
{
    faulted = true;
    fault_errno = err_ != 0 ? err_ : EIO;
}

int spot_runtime_t::ensure_sender_socket (spot_runtime_sender_kind_t kind_,
                                          socket_base_t **out_)
{
    if (!owner || !owner->_ctx || !out_) {
        errno = EFAULT;
        return -1;
    }

    scoped_lock_t lock (owner->_sync);
    if (stop.get () != 0 || faulted || shutdown_phase_value != spot_shutdown_phase_running) {
        errno = EFSM;
        return -1;
    }

    socket_base_t *&slot = sender_socket_slot_local (this, kind_);
    std::string &connected_endpoint = sender_endpoint_cache_local (this, kind_);
    const std::string &target_endpoint =
      sender_target_endpoint_local (this, kind_);
    if (target_endpoint.empty ()) {
        errno = EFAULT;
        return -1;
    }

    if (slot && connected_endpoint == target_endpoint) {
        *out_ = slot;
        return 0;
    }

    if (slot) {
        close_socket_ptr_local (&slot);
        connected_endpoint.clear ();
    }

    socket_base_t *socket = owner->_ctx->create_socket (ZLINK_CORE_SOCKET_DEALER);
    if (!socket)
        return -1;

    const int linger = 0;
    socket->setsockopt (ZLINK_INTERNAL_OPT_LINGER, &linger, sizeof (linger));
    if (socket->connect (target_endpoint.c_str ()) != 0) {
        const int saved_errno = errno != 0 ? errno : EIO;
        close_socket_ptr_local (&socket);
        errno = saved_errno;
        return -1;
    }

    slot = socket;
    connected_endpoint = target_endpoint;
    *out_ = slot;
    return 0;
}

int spot_runtime_t::ensure_peer_route_sender_socket (
  const std::string &target_endpoint_,
  socket_base_t **out_)
{
    if (!owner || !owner->_ctx || !out_ || target_endpoint_.empty ()) {
        errno = EFAULT;
        return -1;
    }

    scoped_lock_t lock (owner->_sync);
    if (stop.get () != 0 || faulted
        || shutdown_phase_value != spot_shutdown_phase_running) {
        errno = EFSM;
        return -1;
    }

    if (peer_route_tx && peer_route_sender_endpoint == target_endpoint_) {
        *out_ = peer_route_tx;
        return 0;
    }

    if (peer_route_tx) {
        close_socket_ptr_local (&peer_route_tx);
        peer_route_sender_endpoint.clear ();
        peer_route_sender_ready_after_ms = 0;
    }

    socket_base_t *socket =
      owner->_ctx->create_socket (ZLINK_CORE_SOCKET_DEALER);
    if (!socket)
        return -1;

    const int linger = 0;
    const int immediate = 1;
    socket->setsockopt (ZLINK_INTERNAL_OPT_LINGER, &linger, sizeof (linger));
    socket->setsockopt (ZLINK_INTERNAL_OPT_IMMEDIATE, &immediate,
                        sizeof (immediate));
    if (spot_node_t::apply_tls_client (socket, owner->_tls_ca,
                                       owner->_tls_hostname,
                                       owner->_tls_trust_system)
          != 0
        || socket->connect (target_endpoint_.c_str ()) != 0) {
        const int saved_errno = errno != 0 ? errno : EIO;
        close_socket_ptr_local (&socket);
        errno = saved_errno;
        return -1;
    }

    const uint64_t deadline_ms = zlink::clock_t ().now_ms () + 1000;
    while (zlink::clock_t ().now_ms () < deadline_ms) {
        spot_data_plane_forwarder_t::pump_socket_commands (socket);
        socket->set_all_pipes_nodelay ();
        if (socket_route_sender_ready_local (socket, target_endpoint_))
            break;
        zlink::sleep_ms (1);
    }
    if (!socket_route_sender_ready_local (socket, target_endpoint_)) {
        close_socket_ptr_local (&socket);
        errno = ETIMEDOUT;
        return -1;
    }

    peer_route_tx = socket;
    peer_route_sender_endpoint = target_endpoint_;
    peer_route_sender_ready_after_ms = 0;
    *out_ = peer_route_tx;
    return 0;
}

int spot_runtime_t::close_sender_cache (spot_runtime_sender_kind_t kind_,
                                        int timeout_ms_)
{
    socket_base_t *socket = NULL;
    std::string endpoint;
    {
        scoped_lock_t lock (owner->_sync);
        socket = sender_socket_slot_local (this, kind_);
        endpoint = sender_endpoint_cache_local (this, kind_);
        sender_socket_slot_local (this, kind_) = NULL;
        sender_endpoint_cache_local (this, kind_).clear ();
    }

    if (!socket)
        return 0;
    LIBZLINK_UNUSED (endpoint);
    LIBZLINK_UNUSED (timeout_ms_);
    close_socket_ptr_local (&socket);
    return 0;
}

int spot_runtime_t::close_sender_caches (int timeout_ms_)
{
    int first_error = 0;
    preserve_first_error_local (
      close_sender_cache (spot_runtime_sender_route_ingress, timeout_ms_),
      &first_error);
    preserve_first_error_local (
      close_sender_cache (spot_runtime_sender_node_router, timeout_ms_),
      &first_error);
    {
        socket_base_t *socket = NULL;
        {
            scoped_lock_t lock (owner->_sync);
            socket = peer_route_tx;
            peer_route_tx = NULL;
            peer_route_sender_endpoint.clear ();
            peer_route_sender_ready_after_ms = 0;
        }
        if (socket)
            close_socket_ptr_local (&socket);
    }
    if (first_error != 0) {
        errno = first_error;
        return -1;
    }
    return 0;
}

void spot_runtime_t::begin_shutdown ()
{
    stop.set (1);
    advance_shutdown_phase (spot_shutdown_phase_stop_accepting);
}

void spot_runtime_t::advance_shutdown_phase (spot_shutdown_phase_t phase_)
{
    scoped_lock_t lock (shutdown_sync);
    if (phase_ > shutdown_phase_value)
        shutdown_phase_value = phase_;
}

spot_shutdown_phase_t spot_runtime_t::shutdown_phase () const
{
    scoped_lock_t lock (const_cast<mutex_t &> (shutdown_sync));
    return shutdown_phase_value;
}


bool spot_runtime_t::try_set_data_plane_task_id (uint64_t task_id_)
{
    if (task_id_ == 0)
        return false;

    scoped_lock_t lock (execution_sync);
    if (execution.data_plane_task_id_value != 0)
        return false;
    execution.data_plane_task_id_value = task_id_;
    return true;
}

uint64_t spot_runtime_t::data_plane_task_id () const
{
    scoped_lock_t lock (const_cast<mutex_t &> (execution_sync));
    return execution.data_plane_task_id_value;
}

uint64_t spot_runtime_t::clear_data_plane_task_id ()
{
    scoped_lock_t lock (execution_sync);
    const uint64_t task_id = execution.data_plane_task_id_value;
    execution.data_plane_task_id_value = 0;
    return task_id;
}

bool spot_runtime_t::try_set_control_task_id (uint64_t task_id_)
{
    if (task_id_ == 0)
        return false;

    scoped_lock_t lock (execution_sync);
    if (execution.control_state.task_id != 0)
        return false;
    execution.control_state.task_id = task_id_;
    return true;
}

uint64_t spot_runtime_t::control_task_id () const
{
    scoped_lock_t lock (const_cast<mutex_t &> (execution_sync));
    return execution.control_state.task_id;
}

uint64_t spot_runtime_t::clear_control_task_id ()
{
    scoped_lock_t lock (execution_sync);
    const uint64_t task_id = execution.control_state.task_id;
    execution.control_state.task_id = 0;
    return task_id;
}

bool spot_runtime_t::note_connected_peer_version (
  uint64_t connected_peer_version_)
{
    scoped_lock_t lock (execution_sync);
    if (execution.control_state.connected_peer_version_seen
        == connected_peer_version_)
        return false;
    execution.control_state.connected_peer_version_seen = connected_peer_version_;
    return true;
}

uint64_t spot_runtime_t::connected_peer_version_seen () const
{
    scoped_lock_t lock (const_cast<mutex_t &> (execution_sync));
    return execution.control_state.connected_peer_version_seen;
}

}
