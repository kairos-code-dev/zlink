/* SPDX-License-Identifier: MPL-2.0 */

#include "precompiled.hpp"

#include "services/spot/spot_data_plane.hpp"
#include "services/spot/spot_node.hpp"
#include "services/spot/spot_runtime.hpp"
#include "services/control/service_control_runtime.hpp"

#include "sockets/socket_base.hpp"
#include "utils/random.hpp"

namespace zlink
{
namespace
{
static void preserve_first_error_local (int rc_, int *first_error_)
{
    if (rc_ == 0 || !first_error_ || *first_error_ != 0)
        return;
    *first_error_ = errno != 0 ? errno : EIO;
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
}

spot_runtime_t::spot_runtime_t (spot_node_t *owner_) :
    owner (owner_),
    data_ctrl_front (NULL),
    data_ctrl_back (NULL),
    mesh_pub (NULL),
    mesh_xsub (NULL),
    peer_ctrl_pub (NULL),
    peer_ctrl_sub (NULL),
    local_pub_ingress_sub (NULL),
    local_fanout_xpub (NULL),
    stop (0),
    node_id (generate_random ()),
    faulted (false),
    fault_errno (0),
    abortive_shutdown (false),
    data_plane_task_id_value (0),
    data_plane_running (false),
    next_bootstrap_ms (0),
    last_bootstrap_peer_version (UINT64_MAX),
    next_attachment_id (0)
{
    if (node_id == 0)
        node_id = 1;

    char buf[128];
    snprintf (buf, sizeof (buf), "inproc://zlink.spot.%u.pub-in", node_id);
    pub_ingress_endpoint = buf;
    snprintf (buf, sizeof (buf), "inproc://zlink.spot.%u.sub-out", node_id);
    sub_fanout_endpoint = buf;
    snprintf (buf, sizeof (buf), "inproc://zlink.spot.%u.ctrl", node_id);
    data_ctrl_endpoint = buf;
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
    if (spot_data_plane_t::initialize_runtime (owner, this, &data_plane_state) != 0
        || !spot_node_t::recv_ctrl_reply (data_ctrl_front, &worker_errno)) {
        errno = worker_errno != 0 ? worker_errno : ETIMEDOUT;
        stop.set (1);
        stop_sockets ();
        spot_data_plane_t::teardown_runtime (owner, this, &data_plane_state,
                                             &data_plane_protocol_state);
        if (owner)
            owner->untrack_owned_socket (data_ctrl_front);
        close_socket_ptr_local (&data_ctrl_front);
        return -1;
    }

    service_control_runtime_t *runtime = owner->_ctx->service_data_runtime ();
    if (!runtime) {
        errno = ETERM;
        stop.set (1);
        stop_sockets ();
        spot_data_plane_t::teardown_runtime (owner, this, &data_plane_state,
                                             &data_plane_protocol_state);
        return -1;
    }

    const uint64_t task_id =
      runtime->add_periodic_task (&spot_data_plane_t::task_entry, owner, 10, true);
    if (task_id == 0) {
        stop.set (1);
        stop_sockets ();
        spot_data_plane_t::teardown_runtime (owner, this, &data_plane_state,
                                             &data_plane_protocol_state);
        return -1;
    }
    data_plane_task_id_value = task_id;
    data_plane_running = true;
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
        ingress = local_pub_ingress_sub;
        fanout = local_fanout_xpub;
        data_ctrl_front = NULL;
        data_ctrl_back = NULL;
        mesh_pub = NULL;
        mesh_xsub = NULL;
        peer_ctrl_pub = NULL;
        peer_ctrl_sub = NULL;
        local_pub_ingress_sub = NULL;
        local_fanout_xpub = NULL;
    }

    if (spot_shutdown_debug_enabled_local ()) {
        std::fprintf (
          stderr,
          "[spot-close] ctrl_front=%d ctrl_back=%d mesh_pub=%d mesh_xsub=%d peer_ctrl_pub=%d peer_ctrl_sub=%d ingress=%d fanout=%d\n",
          ctrl_front ? ctrl_front->socket_id () : -1,
          ctrl_back ? ctrl_back->socket_id () : -1,
          mesh_pub_local ? mesh_pub_local->socket_id () : -1,
          mesh_xsub_local ? mesh_xsub_local->socket_id () : -1,
          peer_ctrl_pub_local ? peer_ctrl_pub_local->socket_id () : -1,
          peer_ctrl_sub_local ? peer_ctrl_sub_local->socket_id () : -1,
          ingress ? ingress->socket_id () : -1,
          fanout ? fanout->socket_id () : -1);
        std::fflush (stderr);
    }

    if (owner && owner->_ctx) {
        if (ctrl_front && !data_ctrl_endpoint.empty ())
            (void) ctrl_front->term_endpoint (data_ctrl_endpoint.c_str ());
        if (ctrl_back && !data_ctrl_endpoint.empty ())
            (void) ctrl_back->term_endpoint (data_ctrl_endpoint.c_str ());
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
        if (ingress)
            ingress->set_all_pipes_nodelay ();
        if (fanout)
            fanout->set_all_pipes_nodelay ();
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

    service_control_runtime_t *runtime =
      owner && owner->_ctx ? owner->_ctx->service_data_runtime () : NULL;
    if (runtime && data_plane_task_id_value != 0)
        runtime->wakeup_task (data_plane_task_id_value);

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

bool spot_runtime_t::try_set_data_plane_task_id (uint64_t task_id_)
{
    if (task_id_ == 0)
        return false;

    scoped_lock_t lock (control_state_sync);
    if (data_plane_task_id_value != 0)
        return false;
    data_plane_task_id_value = task_id_;
    return true;
}

uint64_t spot_runtime_t::data_plane_task_id () const
{
    scoped_lock_t lock (const_cast<mutex_t &> (control_state_sync));
    return data_plane_task_id_value;
}

uint64_t spot_runtime_t::clear_data_plane_task_id ()
{
    scoped_lock_t lock (control_state_sync);
    const uint64_t task_id = data_plane_task_id_value;
    data_plane_task_id_value = 0;
    return task_id;
}

bool spot_runtime_t::try_set_control_task_id (uint64_t task_id_)
{
    if (task_id_ == 0)
        return false;

    scoped_lock_t lock (control_state_sync);
    if (control_state.task_id != 0)
        return false;
    control_state.task_id = task_id_;
    return true;
}

uint64_t spot_runtime_t::control_task_id () const
{
    scoped_lock_t lock (const_cast<mutex_t &> (control_state_sync));
    return control_state.task_id;
}

uint64_t spot_runtime_t::clear_control_task_id ()
{
    scoped_lock_t lock (control_state_sync);
    const uint64_t task_id = control_state.task_id;
    control_state.task_id = 0;
    return task_id;
}

bool spot_runtime_t::note_connected_peer_version (
  uint64_t connected_peer_version_)
{
    scoped_lock_t lock (control_state_sync);
    if (control_state.connected_peer_version_seen == connected_peer_version_)
        return false;
    control_state.connected_peer_version_seen = connected_peer_version_;
    return true;
}

uint64_t spot_runtime_t::connected_peer_version_seen () const
{
    scoped_lock_t lock (const_cast<mutex_t &> (control_state_sync));
    return control_state.connected_peer_version_seen;
}

int spot_runtime_t::stop_and_join ()
{
    stop.set (1);
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
    stop_sockets ();
    service_control_runtime_t *runtime =
      owner && owner->_ctx ? owner->_ctx->service_data_runtime () : NULL;
    const uint64_t task_id = clear_data_plane_task_id ();
    if (runtime && task_id != 0)
        (void) runtime->remove_task (task_id);
    if (data_plane_running) {
        spot_data_plane_t::teardown_runtime (owner, this, &data_plane_state,
                                             &data_plane_protocol_state);
        data_plane_running = false;
    }
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
    stop.set (1);
    service_control_runtime_t *runtime =
      owner && owner->_ctx ? owner->_ctx->service_data_runtime () : NULL;
    const uint64_t task_id = clear_data_plane_task_id ();
    if (runtime && task_id != 0)
        (void) runtime->remove_task (task_id);
    if (data_plane_running) {
        spot_data_plane_t::teardown_runtime (owner, this, &data_plane_state,
                                             &data_plane_protocol_state);
        data_plane_running = false;
    }

    socket_base_t *ctrl_front = NULL;
    socket_base_t *ctrl_back = NULL;
    socket_base_t *mesh_pub_local = NULL;
    socket_base_t *mesh_xsub_local = NULL;
    socket_base_t *peer_ctrl_pub_local = NULL;
    socket_base_t *peer_ctrl_sub_local = NULL;
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
        ingress = local_pub_ingress_sub;
        fanout = local_fanout_xpub;
        data_ctrl_front = NULL;
        data_ctrl_back = NULL;
        mesh_pub = NULL;
        mesh_xsub = NULL;
        peer_ctrl_pub = NULL;
        peer_ctrl_sub = NULL;
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
        if (ctrl_front && !data_ctrl_endpoint.empty ())
            (void) ctrl_front->term_endpoint (data_ctrl_endpoint.c_str ());
        if (ctrl_back && !data_ctrl_endpoint.empty ())
            (void) ctrl_back->term_endpoint (data_ctrl_endpoint.c_str ());
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
        close_socket_ptr_local (&ingress);
        close_socket_ptr_local (&fanout);
        for (size_t i = 0; i < attachment_sockets.size (); ++i) {
            socket_base_t *socket = attachment_sockets[i];
            close_socket_ptr_local (&socket);
        }
    }

    return 0;
}
}
