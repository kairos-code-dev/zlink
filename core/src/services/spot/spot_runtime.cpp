/* SPDX-License-Identifier: MPL-2.0 */

#include "precompiled.hpp"

#include "services/spot/spot_data_plane.hpp"
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

static void preserve_first_error_local (int rc_, int *first_error_)
{
    if (rc_ == 0 || !first_error_ || *first_error_ != 0)
        return;
    *first_error_ = errno != 0 ? errno : EIO;
}

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

static int send_ascii_frame_local (socket_base_t *socket_,
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

static void close_socket_ptr_local (socket_base_t **socket_p_)
{
    if (socket_p_ && *socket_p_) {
        socket_base_t *socket = *socket_p_;
        socket->stop ();
        socket->close ();
        *socket_p_ = NULL;
    }
}

static bool spot_shutdown_debug_enabled_local ()
{
    return std::getenv ("ZLINK_DEBUG_SPOT_SHUTDOWN") != NULL;
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

void spot_runtime_t::stop_sockets ()
{
    socket_base_t *ctrl_front = NULL;
    socket_base_t *ctrl_back = NULL;
    socket_base_t *mesh_pub_local = NULL;
    socket_base_t *mesh_xsub_local = NULL;
    socket_base_t *peer_ctrl_pub_local = NULL;
    socket_base_t *peer_ctrl_sub_local = NULL;
    socket_base_t *route_ingress_local = NULL;
    socket_base_t *peer_route_ingress_local = NULL;
    socket_base_t *node_router_local = NULL;
    socket_base_t *route_ingress_tx_local = NULL;
    socket_base_t *node_router_tx_local = NULL;
    socket_base_t *peer_route_tx_local = NULL;
    socket_base_t *ingress = NULL;
    socket_base_t *fanout = NULL;

    {
        scoped_lock_t lock (owner->_sync);
        ctrl_front = data_ctrl_front;
        ctrl_back = data_ctrl_back;
        mesh_pub_local = mesh_pub;
        mesh_xsub_local = mesh_xsub;
        peer_ctrl_pub_local = peer_ctrl_pub;
        peer_ctrl_sub_local = peer_ctrl_sub;
        route_ingress_local = route_ingress;
        peer_route_ingress_local = peer_route_ingress;
        node_router_local = node_router;
        route_ingress_tx_local = route_ingress_tx;
        node_router_tx_local = node_router_tx;
        peer_route_tx_local = peer_route_tx;
        ingress = local_pub_ingress_sub;
        fanout = local_fanout_xpub;
    }

    if (ctrl_front)
        ctrl_front->stop ();
    if (ctrl_back)
        ctrl_back->stop ();
    if (mesh_pub_local)
        mesh_pub_local->stop ();
    if (mesh_xsub_local)
        mesh_xsub_local->stop ();
    if (peer_ctrl_pub_local)
        peer_ctrl_pub_local->stop ();
    if (peer_ctrl_sub_local)
        peer_ctrl_sub_local->stop ();
    if (route_ingress_local)
        route_ingress_local->stop ();
    if (peer_route_ingress_local)
        peer_route_ingress_local->stop ();
    if (node_router_local)
        node_router_local->stop ();
    if (route_ingress_tx_local)
        route_ingress_tx_local->stop ();
    if (node_router_tx_local)
        node_router_tx_local->stop ();
    if (peer_route_tx_local)
        peer_route_tx_local->stop ();
    if (ingress)
        ingress->stop ();
    if (fanout)
        fanout->stop ();
}

int spot_runtime_t::close_control_sockets ()
{
    int first_error = 0;
    socket_base_t *ctrl_front = NULL;
    socket_base_t *ctrl_back = NULL;
    socket_base_t *mesh_pub_local = NULL;
    socket_base_t *mesh_xsub_local = NULL;
    socket_base_t *peer_ctrl_pub_local = NULL;
    socket_base_t *peer_ctrl_sub_local = NULL;
    socket_base_t *route_ingress_local = NULL;
    socket_base_t *peer_route_ingress_local = NULL;
    socket_base_t *node_router_local = NULL;
    socket_base_t *route_ingress_tx_local = NULL;
    socket_base_t *node_router_tx_local = NULL;
    socket_base_t *peer_route_tx_local = NULL;
    socket_base_t *ingress = NULL;
    socket_base_t *fanout = NULL;
    {
        scoped_lock_t lock (owner->_sync);
        ctrl_front = data_ctrl_front;
        ctrl_back = data_ctrl_back;
        mesh_pub_local = mesh_pub;
        mesh_xsub_local = mesh_xsub;
        peer_ctrl_pub_local = peer_ctrl_pub;
        peer_ctrl_sub_local = peer_ctrl_sub;
        route_ingress_local = route_ingress;
        peer_route_ingress_local = peer_route_ingress;
        node_router_local = node_router;
        route_ingress_tx_local = route_ingress_tx;
        node_router_tx_local = node_router_tx;
        peer_route_tx_local = peer_route_tx;
        ingress = local_pub_ingress_sub;
        fanout = local_fanout_xpub;
        data_ctrl_front = NULL;
        data_ctrl_back = NULL;
        mesh_pub = NULL;
        mesh_xsub = NULL;
        peer_ctrl_pub = NULL;
        peer_ctrl_sub = NULL;
        route_ingress = NULL;
        peer_route_ingress = NULL;
        node_router = NULL;
        route_ingress_tx = NULL;
        node_router_tx = NULL;
        peer_route_tx = NULL;
        local_pub_ingress_sub = NULL;
        local_fanout_xpub = NULL;
    }

    if (spot_shutdown_debug_enabled_local ()) {
        std::fprintf (
          stderr,
          "[spot-close] ctrl_front=%d ctrl_back=%d mesh_pub=%d mesh_xsub=%d peer_ctrl_pub=%d peer_ctrl_sub=%d route_ingress=%d peer_route_ingress=%d node_router=%d route_ingress_tx=%d node_router_tx=%d peer_route_tx=%d ingress=%d fanout=%d\n",
          ctrl_front ? ctrl_front->socket_id () : -1,
          ctrl_back ? ctrl_back->socket_id () : -1,
          mesh_pub_local ? mesh_pub_local->socket_id () : -1,
          mesh_xsub_local ? mesh_xsub_local->socket_id () : -1,
          peer_ctrl_pub_local ? peer_ctrl_pub_local->socket_id () : -1,
          peer_ctrl_sub_local ? peer_ctrl_sub_local->socket_id () : -1,
          route_ingress_local ? route_ingress_local->socket_id () : -1,
          peer_route_ingress_local ? peer_route_ingress_local->socket_id () : -1,
          node_router_local ? node_router_local->socket_id () : -1,
          route_ingress_tx_local ? route_ingress_tx_local->socket_id () : -1,
          node_router_tx_local ? node_router_tx_local->socket_id () : -1,
          peer_route_tx_local ? peer_route_tx_local->socket_id () : -1,
          ingress ? ingress->socket_id () : -1,
          fanout ? fanout->socket_id () : -1);
        std::fflush (stderr);
    }

    if (owner && owner->_ctx) {
        if (ctrl_front && !data_ctrl_endpoint.empty ())
            (void) ctrl_front->term_endpoint (data_ctrl_endpoint.c_str ());
        if (ctrl_back && !data_ctrl_endpoint.empty ())
            (void) ctrl_back->term_endpoint (data_ctrl_endpoint.c_str ());
        if (route_ingress_local && !route_ingress_endpoint.empty ())
            (void) route_ingress_local->term_endpoint (
              route_ingress_endpoint.c_str ());
        if (node_router_local && !node_router_endpoint.empty ())
            (void) node_router_local->term_endpoint (
              node_router_endpoint.c_str ());
        if (node_router_local && !peer_route_bind_endpoint.empty ())
            (void) node_router_local->term_endpoint (
              peer_route_bind_endpoint.c_str ());
        if (ingress && !pub_ingress_endpoint.empty ())
            (void) ingress->term_endpoint (pub_ingress_endpoint.c_str ());
        if (fanout && !sub_fanout_endpoint.empty ())
            (void) fanout->term_endpoint (sub_fanout_endpoint.c_str ());
        if (ctrl_front)
            ctrl_front->set_all_pipes_nodelay ();
        if (ctrl_back)
            ctrl_back->set_all_pipes_nodelay ();
        if (mesh_pub_local)
            mesh_pub_local->set_all_pipes_nodelay ();
        if (mesh_xsub_local)
            mesh_xsub_local->set_all_pipes_nodelay ();
        if (peer_ctrl_pub_local)
            peer_ctrl_pub_local->set_all_pipes_nodelay ();
        if (peer_ctrl_sub_local)
            peer_ctrl_sub_local->set_all_pipes_nodelay ();
        if (route_ingress_local)
            route_ingress_local->set_all_pipes_nodelay ();
        if (peer_route_ingress_local)
            peer_route_ingress_local->set_all_pipes_nodelay ();
        if (node_router_local)
            node_router_local->set_all_pipes_nodelay ();
        if (ingress)
            ingress->set_all_pipes_nodelay ();
        if (fanout)
            fanout->set_all_pipes_nodelay ();
        if (route_ingress_tx_local)
            close_socket_ptr_local (&route_ingress_tx_local);
        if (node_router_tx_local)
            close_socket_ptr_local (&node_router_tx_local);
        if (peer_route_tx_local)
            close_socket_ptr_local (&peer_route_tx_local);
        preserve_first_error_local (close_runtime_socket (ctrl_front, 2000),
                                    &first_error);
        preserve_first_error_local (close_runtime_socket (ctrl_back, 2000),
                                    &first_error);
        preserve_first_error_local (close_runtime_socket (mesh_pub_local, 2000),
                                    &first_error);
        preserve_first_error_local (close_runtime_socket (mesh_xsub_local, 2000),
                                    &first_error);
        preserve_first_error_local (
          close_runtime_socket (peer_ctrl_pub_local, 2000), &first_error);
        preserve_first_error_local (
          close_runtime_socket (peer_ctrl_sub_local, 2000), &first_error);
        preserve_first_error_local (
          close_runtime_socket (route_ingress_local, 2000), &first_error);
        preserve_first_error_local (
          close_runtime_socket (peer_route_ingress_local, 2000), &first_error);
        preserve_first_error_local (
          close_runtime_socket (node_router_local, 2000), &first_error);
        preserve_first_error_local (close_runtime_socket (ingress, 2000),
                                    &first_error);
        preserve_first_error_local (close_runtime_socket (fanout, 2000),
                                    &first_error);
    } else {
        close_socket_ptr_local (&ctrl_front);
        close_socket_ptr_local (&ctrl_back);
        close_socket_ptr_local (&mesh_pub_local);
        close_socket_ptr_local (&mesh_xsub_local);
        close_socket_ptr_local (&peer_ctrl_pub_local);
        close_socket_ptr_local (&peer_ctrl_sub_local);
        close_socket_ptr_local (&route_ingress_local);
        close_socket_ptr_local (&peer_route_ingress_local);
        close_socket_ptr_local (&node_router_local);
        close_socket_ptr_local (&route_ingress_tx_local);
        close_socket_ptr_local (&node_router_tx_local);
        close_socket_ptr_local (&peer_route_tx_local);
        close_socket_ptr_local (&ingress);
        close_socket_ptr_local (&fanout);
    }

    if (first_error != 0) {
        errno = first_error;
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

int spot_runtime_t::detach_runtime_endpoints ()
{
    socket_base_t *ctrl_front = NULL;
    socket_base_t *ctrl_back = NULL;
    socket_base_t *route_ingress_local = NULL;
    socket_base_t *peer_route_ingress_local = NULL;
    socket_base_t *node_router_local = NULL;
    socket_base_t *ingress = NULL;
    socket_base_t *fanout = NULL;
    socket_base_t *route_ingress_tx_local = NULL;
    socket_base_t *node_router_tx_local = NULL;
    socket_base_t *peer_route_tx_local = NULL;
    std::string route_ingress_sender_endpoint_local;
    std::string node_router_sender_endpoint_local;
    std::string peer_route_sender_endpoint_local;

    {
        scoped_lock_t lock (owner->_sync);
        ctrl_front = data_ctrl_front;
        ctrl_back = data_ctrl_back;
        route_ingress_local = route_ingress;
        peer_route_ingress_local = peer_route_ingress;
        node_router_local = node_router;
        route_ingress_tx_local = route_ingress_tx;
        node_router_tx_local = node_router_tx;
        peer_route_tx_local = peer_route_tx;
        ingress = local_pub_ingress_sub;
        fanout = local_fanout_xpub;
        route_ingress_sender_endpoint_local = route_ingress_sender_endpoint;
        node_router_sender_endpoint_local = node_router_sender_endpoint;
        peer_route_sender_endpoint_local = peer_route_sender_endpoint;
    }

    if (ctrl_front && !data_ctrl_endpoint.empty ())
        (void) ctrl_front->term_endpoint (data_ctrl_endpoint.c_str ());
    if (ctrl_back && !data_ctrl_endpoint.empty ())
        (void) ctrl_back->term_endpoint (data_ctrl_endpoint.c_str ());
    if (route_ingress_local && !route_ingress_endpoint.empty ())
        (void) route_ingress_local->term_endpoint (route_ingress_endpoint.c_str ());
    if (node_router_local && !node_router_endpoint.empty ())
        (void) node_router_local->term_endpoint (node_router_endpoint.c_str ());
    if (node_router_local && !peer_route_bind_endpoint.empty ())
        (void) node_router_local->term_endpoint (
          peer_route_bind_endpoint.c_str ());
    if (ingress && !pub_ingress_endpoint.empty ())
        (void) ingress->term_endpoint (pub_ingress_endpoint.c_str ());
    if (fanout && !sub_fanout_endpoint.empty ())
        (void) fanout->term_endpoint (sub_fanout_endpoint.c_str ());
    if (route_ingress_tx_local && !route_ingress_sender_endpoint_local.empty ())
        (void) route_ingress_tx_local->term_endpoint (
          route_ingress_sender_endpoint_local.c_str ());
    if (node_router_tx_local && !node_router_sender_endpoint_local.empty ())
        (void) node_router_tx_local->term_endpoint (
          node_router_sender_endpoint_local.c_str ());
    if (peer_route_tx_local && !peer_route_sender_endpoint_local.empty ())
        (void) peer_route_tx_local->term_endpoint (
          peer_route_sender_endpoint_local.c_str ());
    return 0;
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

int spot_runtime_t::stop_and_join ()
{
    begin_shutdown ();
    if (data_ctrl_front) {
        scoped_lock_t lock (ctrl_sync);
        if (send_ascii_frame_local (data_ctrl_front, "terminate", 0) != 0) {
            const int err = errno != 0 ? errno : EIO;
            if (err != EAGAIN && err != ETIMEDOUT && err != EFSM && err != ETERM
                && err != EPIPE && err != ENOTSOCK) {
                errno = err;
                return -1;
            }
        }
    }
    advance_shutdown_phase (spot_shutdown_phase_stop_producers);
    (void) close_sender_caches (1000);
    (void) detach_runtime_endpoints ();
    advance_shutdown_phase (spot_shutdown_phase_detach_endpoints);
    stop_sockets ();
    const uint64_t task_id = clear_data_plane_task_id ();
    if (data_plane_runtime && task_id != 0)
        (void) data_plane_runtime->remove_task (task_id);
    data_plane_runtime = NULL;
    if (execution.data_plane_running) {
        spot_data_plane_t::teardown_runtime (
          owner, this, &execution.data_plane_state,
          &execution.data_plane_protocol_state);
        execution.data_plane_running = false;
    }
    advance_shutdown_phase (spot_shutdown_phase_close_transports);
    advance_shutdown_phase (spot_shutdown_phase_drain_state);
    advance_shutdown_phase (spot_shutdown_phase_cleanup);
    return 0;
}

size_t spot_runtime_t::live_socket_slot_count () const
{
    size_t count = 0;
    scoped_lock_t lock (const_cast<mutex_t &> (owner->_sync));
    count += data_ctrl_front != NULL ? 1 : 0;
    count += data_ctrl_back != NULL ? 1 : 0;
    count += mesh_pub != NULL ? 1 : 0;
    count += mesh_xsub != NULL ? 1 : 0;
    count += peer_ctrl_pub != NULL ? 1 : 0;
    count += peer_ctrl_sub != NULL ? 1 : 0;
    count += route_ingress != NULL ? 1 : 0;
    count += peer_route_ingress != NULL ? 1 : 0;
    count += node_router != NULL ? 1 : 0;
    count += route_ingress_tx != NULL ? 1 : 0;
    count += node_router_tx != NULL ? 1 : 0;
    count += peer_route_tx != NULL ? 1 : 0;
    count += local_pub_ingress_sub != NULL ? 1 : 0;
    count += local_fanout_xpub != NULL ? 1 : 0;
    return count;
}

size_t spot_runtime_t::attachment_count () const
{
    scoped_lock_t lock (const_cast<mutex_t &> (attachment_sync));
    return attachments.size ();
}

int spot_runtime_t::abortive_stop ()
{
    abortive_shutdown = true;
    begin_shutdown ();
    const uint64_t task_id = clear_data_plane_task_id ();
    if (data_plane_runtime && task_id != 0)
        (void) data_plane_runtime->remove_task (task_id);
    data_plane_runtime = NULL;
    if (execution.data_plane_running) {
        spot_data_plane_t::teardown_runtime (
          owner, this, &execution.data_plane_state,
          &execution.data_plane_protocol_state);
        execution.data_plane_running = false;
    }
    advance_shutdown_phase (spot_shutdown_phase_stop_producers);
    (void) close_sender_caches (1000);
    (void) detach_runtime_endpoints ();
    advance_shutdown_phase (spot_shutdown_phase_detach_endpoints);

    socket_base_t *ctrl_front = NULL;
    socket_base_t *ctrl_back = NULL;
    socket_base_t *mesh_pub_local = NULL;
    socket_base_t *mesh_xsub_local = NULL;
    socket_base_t *peer_ctrl_pub_local = NULL;
    socket_base_t *peer_ctrl_sub_local = NULL;
    socket_base_t *route_ingress_local = NULL;
    socket_base_t *peer_route_ingress_local = NULL;
    socket_base_t *node_router_local = NULL;
    socket_base_t *route_ingress_tx_local = NULL;
    socket_base_t *node_router_tx_local = NULL;
    socket_base_t *ingress = NULL;
    socket_base_t *fanout = NULL;
    {
        scoped_lock_t lock (owner->_sync);
        ctrl_front = data_ctrl_front;
        ctrl_back = data_ctrl_back;
        mesh_pub_local = mesh_pub;
        mesh_xsub_local = mesh_xsub;
        peer_ctrl_pub_local = peer_ctrl_pub;
        peer_ctrl_sub_local = peer_ctrl_sub;
        route_ingress_local = route_ingress;
        peer_route_ingress_local = peer_route_ingress;
        node_router_local = node_router;
        route_ingress_tx_local = route_ingress_tx;
        node_router_tx_local = node_router_tx;
        ingress = local_pub_ingress_sub;
        fanout = local_fanout_xpub;
        data_ctrl_front = NULL;
        data_ctrl_back = NULL;
        mesh_pub = NULL;
        mesh_xsub = NULL;
        peer_ctrl_pub = NULL;
        peer_ctrl_sub = NULL;
        route_ingress = NULL;
        peer_route_ingress = NULL;
        node_router = NULL;
        route_ingress_tx = NULL;
        node_router_tx = NULL;
        local_pub_ingress_sub = NULL;
        local_fanout_xpub = NULL;
    }

    std::vector<socket_base_t *> attachment_sockets;
    {
        scoped_lock_t lock (attachment_sync);
        for (std::map<uint64_t, spot_attachment_t>::iterator it =
               attachments.begin ();
             it != attachments.end (); ++it) {
            if (it->second.socket)
                attachment_sockets.push_back (it->second.socket);
        }
        attachments.clear ();
    }

    if (owner && owner->_ctx) {
        if (ctrl_front)
            ctrl_front->set_all_pipes_nodelay ();
        if (ctrl_back)
            ctrl_back->set_all_pipes_nodelay ();
        if (mesh_pub_local)
            mesh_pub_local->set_all_pipes_nodelay ();
        if (mesh_xsub_local)
            mesh_xsub_local->set_all_pipes_nodelay ();
        if (peer_ctrl_pub_local)
            peer_ctrl_pub_local->set_all_pipes_nodelay ();
        if (peer_ctrl_sub_local)
            peer_ctrl_sub_local->set_all_pipes_nodelay ();
        if (route_ingress_local)
            route_ingress_local->set_all_pipes_nodelay ();
        if (peer_route_ingress_local)
            peer_route_ingress_local->set_all_pipes_nodelay ();
        if (node_router_local)
            node_router_local->set_all_pipes_nodelay ();
        if (ingress)
            ingress->set_all_pipes_nodelay ();
        if (fanout)
            fanout->set_all_pipes_nodelay ();
        (void) close_runtime_socket (ctrl_front, 1000);
        (void) close_runtime_socket (ctrl_back, 1000);
        (void) close_runtime_socket (mesh_pub_local, 1000);
        (void) close_runtime_socket (mesh_xsub_local, 1000);
        (void) close_runtime_socket (peer_ctrl_pub_local, 1000);
        (void) close_runtime_socket (peer_ctrl_sub_local, 1000);
        (void) close_runtime_socket (route_ingress_local, 1000);
        (void) close_runtime_socket (peer_route_ingress_local, 1000);
        (void) close_runtime_socket (node_router_local, 1000);
        (void) close_runtime_socket (ingress, 1000);
        (void) close_runtime_socket (fanout, 1000);
        for (size_t i = 0; i < attachment_sockets.size (); ++i) {
            socket_base_t *socket = attachment_sockets[i];
            (void) close_runtime_socket (socket, 1000);
        }
    } else {
        close_socket_ptr_local (&ctrl_front);
        close_socket_ptr_local (&ctrl_back);
        close_socket_ptr_local (&mesh_pub_local);
        close_socket_ptr_local (&mesh_xsub_local);
        close_socket_ptr_local (&peer_ctrl_pub_local);
        close_socket_ptr_local (&peer_ctrl_sub_local);
        close_socket_ptr_local (&route_ingress_local);
        close_socket_ptr_local (&peer_route_ingress_local);
        close_socket_ptr_local (&node_router_local);
        close_socket_ptr_local (&ingress);
        close_socket_ptr_local (&fanout);
        for (size_t i = 0; i < attachment_sockets.size (); ++i) {
            socket_base_t *socket = attachment_sockets[i];
            close_socket_ptr_local (&socket);
        }
    }

    advance_shutdown_phase (spot_shutdown_phase_close_transports);
    advance_shutdown_phase (spot_shutdown_phase_drain_state);
    advance_shutdown_phase (spot_shutdown_phase_cleanup);
    return 0;
}
}
