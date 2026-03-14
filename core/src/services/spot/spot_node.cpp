/* SPDX-License-Identifier: MPL-2.0 */

#include "precompiled.hpp"

#include "services/spot/spot_data_plane.hpp"
#include "services/spot/spot_node.hpp"
#include "services/spot/spot_pub.hpp"
#include "services/spot/spot_runtime.hpp"
#include "services/spot/spot_sub.hpp"

#include "services/control/service_control_runtime.hpp"
#include "services/discovery/discovery_protocol.hpp"
#include "services/gateway/routing_id_utils.hpp"
#include "sockets/socket_base.hpp"
#include "utils/clock.hpp"
#include "utils/err.hpp"
#include "utils/random.hpp"

#include <algorithm>
#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <string.h>
#include <unistd.h>
#include <vector>

namespace zlink
{
static const uint32_t spot_node_tag_value = 0x1e6700d9;
static const size_t spot_sub_queue_hwm_default = 1000;
static const int ctrl_timeout_ms = 2000;
static const char spot_ready_probe_prefix[] = "__zlink.ready__/";

static void spot_node_debugf (const char *fmt_, ...)
{
    if (!std::getenv ("ZLINK_SPOT_NODE_DEBUG"))
        return;
    va_list args;
    va_start (args, fmt_);
    std::fprintf (stderr, "[spot-node] ");
    std::vfprintf (stderr, fmt_, args);
    std::fprintf (stderr, "\n");
    va_end (args);
}

static void spot_shutdown_logf (bool always_, const char *fmt_, ...)
{
    if (!always_ && !std::getenv ("ZLINK_SPOT_SHUTDOWN_LOG"))
        return;
    va_list stderr_args;
    va_start (stderr_args, fmt_);
    std::fprintf (stderr, "[spot-shutdown] ");
    std::vfprintf (stderr, fmt_, stderr_args);
    std::fprintf (stderr, "\n");
    va_end (stderr_args);

    FILE *fp = std::fopen ("/tmp/zlink_spot_shutdown.log", "a");
    if (!fp)
        return;

    std::fprintf (fp,
                  "ts=%llu pid=%ld ",
                  static_cast<unsigned long long> (zlink::clock_t ().now_ms ()),
                  static_cast<long> (getpid ()));
    va_list file_args;
    va_start (file_args, fmt_);
    std::vfprintf (fp, fmt_, file_args);
    std::fprintf (fp, "\n");
    va_end (file_args);
    std::fclose (fp);
}

static void preserve_first_error (int rc_, int *first_error_)
{
    if (rc_ == 0 || !first_error_ || *first_error_ != 0)
        return;
    *first_error_ = errno != 0 ? errno : EIO;
}

static int send_ascii_frame (socket_base_t *socket_,
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

static void snapshot_connected_mesh_peer_endpoints (socket_base_t *mesh_xsub_,
                                                    std::set<std::string> *out_)
{
    if (!out_)
        return;
    out_->clear ();
    if (!mesh_xsub_)
        return;

    std::vector<std::string> peers;
    mesh_xsub_->socket_peer_remote_endpoints (&peers);
    for (size_t i = 0; i < peers.size (); ++i)
        out_->insert (peers[i]);
}

static unsigned int subscription_ready_holdoff_ticks (
  const std::set<std::string> &connected_endpoints_)
{
    for (std::set<std::string>::const_iterator it =
           connected_endpoints_.begin ();
         it != connected_endpoints_.end (); ++it) {
        if (it->compare (0, 6, "wss://") == 0)
            return 500;
        if (it->compare (0, 6, "tls://") == 0)
            return 150;
    }

    return 50;
}

static unsigned int subscription_replay_attempt_count (
  const std::set<std::string> &connected_endpoints_)
{
    for (std::set<std::string>::const_iterator it =
           connected_endpoints_.begin ();
         it != connected_endpoints_.end (); ++it) {
        if (it->compare (0, 6, "wss://") == 0)
            return 300;
        if (it->compare (0, 6, "tls://") == 0)
            return 150;
    }

    return 50;
}

static unsigned int pub_delivery_ready_holdoff_ticks (
  const std::set<std::string> &connected_endpoints_)
{
    for (std::set<std::string>::const_iterator it =
           connected_endpoints_.begin ();
         it != connected_endpoints_.end (); ++it) {
        if (it->compare (0, 6, "wss://") == 0)
            return 50;
        if (it->compare (0, 6, "tls://") == 0)
            return 15;
    }

    return 20;
}

static std::string make_ready_ack_arg (const std::string &target_endpoint_,
                                       const std::string &raw_filter_,
                                       const std::string &ack_source_id_)
{
    return target_endpoint_ + "\n" + raw_filter_ + "\n" + ack_source_id_;
}

static int recv_ascii_command (socket_base_t *socket_,
                               std::vector<std::string> *frames_)
{
    if (!frames_)
        return -1;
    frames_->clear ();
    while (true) {
        msg_t frame;
        if (frame.init () != 0)
            return -1;
        if (socket_->recv (&frame, 0) != 0) {
            frame.close ();
            return -1;
        }
        frames_->push_back (std::string (
          static_cast<const char *> (frame.data ()), frame.size ()));
        const bool more = (frame.flags () & msg_t::more) != 0;
        frame.close ();
        if (!more)
            break;
    }
    return frames_->empty () ? -1 : 0;
}

static void close_socket_ptr (socket_base_t **socket_p_)
{
    if (socket_p_ && *socket_p_) {
        socket_base_t *socket = *socket_p_;
        socket->stop ();
        socket->close ();
        *socket_p_ = NULL;
    }
}

spot_runtime_t::spot_runtime_t (spot_node_t *owner_) :
    owner (owner_),
    data_ctrl_front (NULL),
    data_ctrl_back (NULL),
    mesh_pub (NULL),
    mesh_xsub (NULL),
    local_pub_ingress_sub (NULL),
    local_fanout_xpub (NULL),
    stop (0),
    task_id (0),
    node_id (generate_random ()),
    faulted (false),
    fault_errno (0),
    abortive_shutdown (false),
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

    const int socket_type = kind_ == spot_attachment_pub ? ZLINK_PUB : ZLINK_SUB;
    socket_base_t *socket = owner->_ctx->create_socket (socket_type);
    if (!socket)
        return -1;

    owner->track_owned_socket (socket);
    if (socket->connect (endpoint_) != 0) {
        (void) owner->_lifecycle.close_socket_and_wait (socket, 1000);
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
        attachments[attachment.id] = attachment;
    }
    *out_id_ = attachment.id;
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
    {
        scoped_lock_t lock (attachment_sync);
        std::map<uint64_t, spot_attachment_t>::iterator it = attachments.find (id_);
        if (it == attachments.end ())
            return 0;
        socket = it->second.socket;
        attachments.erase (it);
    }

    if (!socket)
        return 0;
    if (owner && owner->_ctx)
        return owner->_lifecycle.close_socket (socket, 2000);

    socket->stop ();
    socket->close ();
    return 0;
}

int spot_runtime_t::start ()
{
    if (!owner || !owner->_ctx) {
        errno = EFAULT;
        return -1;
    }

    data_ctrl_front = owner->_ctx->create_socket (ZLINK_PAIR);
    if (!data_ctrl_front || data_ctrl_front->bind (data_ctrl_endpoint.c_str ()) != 0) {
        close_socket_ptr (&data_ctrl_front);
        return -1;
    }
    owner->track_owned_socket (data_ctrl_front);

    const int linger = 0;
    const int timeout = ctrl_timeout_ms;
    data_ctrl_front->setsockopt (ZLINK_LINGER, &linger, sizeof (linger));
    data_ctrl_front->setsockopt (ZLINK_SNDTIMEO, &timeout, sizeof (timeout));
    data_ctrl_front->setsockopt (ZLINK_RCVTIMEO, &timeout, sizeof (timeout));

    data_plane_thread.start (spot_data_plane_t::thread_entry, owner, "spot-data");

    int worker_errno = 0;
    if (!spot_node_t::recv_ctrl_reply (data_ctrl_front, &worker_errno)) {
        errno = worker_errno != 0 ? worker_errno : ETIMEDOUT;
        stop.set (1);
        stop_sockets ();
        if (data_plane_thread.get_started ())
            data_plane_thread.stop ();
        if (owner)
            owner->untrack_owned_socket (data_ctrl_front);
        close_socket_ptr (&data_ctrl_front);
        return -1;
    }
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
    socket_base_t *ingress = NULL;
    socket_base_t *fanout = NULL;

    {
        scoped_lock_t lock (owner->_sync);
        ctrl_front = data_ctrl_front;
        ctrl_back = data_ctrl_back;
        mesh_pub_local = mesh_pub;
        mesh_xsub_local = mesh_xsub;
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
    if (ingress)
        ingress->stop ();
    if (fanout)
        fanout->stop ();
}

int spot_runtime_t::close_control_sockets ()
{
    int first_error = 0;
    socket_base_t *ctrl_front = NULL;
    {
        scoped_lock_t lock (owner->_sync);
        ctrl_front = data_ctrl_front;
        data_ctrl_back = NULL;
        mesh_pub = NULL;
        mesh_xsub = NULL;
        local_pub_ingress_sub = NULL;
        local_fanout_xpub = NULL;
    }
    if (ctrl_front) {
        if (owner && owner->_ctx)
            preserve_first_error (owner->_lifecycle.close_socket (data_ctrl_front, 2000),
                                  &first_error);
        else {
            ctrl_front->stop ();
            ctrl_front->close ();
            data_ctrl_front = NULL;
        }
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
    if (send_ascii_frame (data_ctrl_front, verb_, arg_ ? ZLINK_SNDMORE : 0) != 0)
        return -1;
    if (arg_ && send_ascii_frame (data_ctrl_front, arg_, 0) != 0)
        return -1;

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

int spot_runtime_t::stop_and_join ()
{
    int first_error = 0;

    stop.set (1);
    if (data_ctrl_front)
        preserve_first_error (send_command ("terminate", NULL), &first_error);
    if (data_plane_thread.get_started ())
        data_plane_thread.stop ();
    preserve_first_error (close_control_sockets (), &first_error);
    if (first_error != 0) {
        errno = first_error;
        return -1;
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

    socket_base_t *ctrl_front = NULL;
    socket_base_t *ctrl_back = NULL;
    socket_base_t *mesh_pub_local = NULL;
    socket_base_t *mesh_xsub_local = NULL;
    socket_base_t *ingress = NULL;
    socket_base_t *fanout = NULL;
    {
        scoped_lock_t lock (owner->_sync);
        ctrl_front = data_ctrl_front;
        ctrl_back = data_ctrl_back;
        mesh_pub_local = mesh_pub;
        mesh_xsub_local = mesh_xsub;
        ingress = local_pub_ingress_sub;
        fanout = local_fanout_xpub;
        data_ctrl_front = NULL;
        data_ctrl_back = NULL;
        mesh_pub = NULL;
        mesh_xsub = NULL;
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
        (void) owner->_lifecycle.close_socket (ctrl_front, 1000);
        (void) owner->_lifecycle.close_socket (ctrl_back, 1000);
        (void) owner->_lifecycle.close_socket (mesh_pub_local, 1000);
        (void) owner->_lifecycle.close_socket (mesh_xsub_local, 1000);
        (void) owner->_lifecycle.close_socket (ingress, 1000);
        (void) owner->_lifecycle.close_socket (fanout, 1000);
        for (size_t i = 0; i < attachment_sockets.size (); ++i) {
            socket_base_t *socket = attachment_sockets[i];
            (void) owner->_lifecycle.close_socket (socket, 1000);
        }
    } else {
        close_socket_ptr (&ctrl_front);
        close_socket_ptr (&ctrl_back);
        close_socket_ptr (&mesh_pub_local);
        close_socket_ptr (&mesh_xsub_local);
        close_socket_ptr (&ingress);
        close_socket_ptr (&fanout);
        for (size_t i = 0; i < attachment_sockets.size (); ++i) {
            socket_base_t *socket = attachment_sockets[i];
            close_socket_ptr (&socket);
        }
    }

    return 0;
}

spot_node_t::spot_node_t (ctx_t *ctx_, const char *service_name_) :
    _ctx (ctx_),
    _tag (spot_node_tag_value),
    _lifecycle (ctx_),
    _runtime (NULL),
    _subscription_ready_refresh_pending (false),
    _subscription_ready_refresh_holdoff_ticks (0),
    _subscription_replay_pending (false),
    _subscription_replay_attempts (0),
    _subscription_replay_holdoff_ticks (0),
    _pub_delivery_ready_refresh_pending (false),
    _pub_delivery_ready_refresh_holdoff_ticks (0),
    _discovery (NULL),
    _discovery_seq (0),
    _registered (false),
    _tls_trust_system (0),
    _server_tls_locked (false),
    _mesh_client_tls_locked (false),
    _registration_tls_locked (false),
    _send_ready_handler (NULL),
    _local_filtered_sub_count (0),
    _active_peer_count (0),
    _default_pub (NULL),
    _default_sub (NULL)
{
    _lifecycle.transition_to (service_state_starting);
    _service_name = service_name_ ? service_name_ : "";
    if (!validate_service_name (_service_name)) {
        _lifecycle.mark_faulted (EINVAL);
        _tag = 0xdeadbeef;
        return;
    }

    _runtime = new (std::nothrow) spot_runtime_t (this);
    if (!_runtime) {
        errno = ENOMEM;
        _lifecycle.mark_faulted (ENOMEM);
        _tag = 0xdeadbeef;
        return;
    }

    if (start_data_plane () != 0) {
        _lifecycle.mark_faulted (errno);
        _tag = 0xdeadbeef;
        return;
    }

    service_control_runtime_t *runtime = _ctx->service_control_runtime ();
    if (!runtime) {
        _lifecycle.mark_faulted (errno);
        _tag = 0xdeadbeef;
        return;
    }
    _runtime->task_id = runtime->add_periodic_task (control_task, this, 10, true);
    if (_runtime->task_id == 0) {
        _lifecycle.mark_faulted (errno);
        _tag = 0xdeadbeef;
    } else {
        _lifecycle.transition_to (service_state_running);
    }
}

spot_node_t::~spot_node_t ()
{
    _tag = 0xdeadbeef;
    delete _runtime;
    _runtime = NULL;
}

bool spot_node_t::check_tag () const
{
    return _tag == spot_node_tag_value;
}

int spot_node_t::apply_tls_server (socket_base_t *socket_,
                                   const std::string &cert_,
                                   const std::string &key_)
{
    if (!socket_)
        return -1;
    if (cert_.empty () || key_.empty ())
        return 0;
    if (socket_->setsockopt (ZLINK_TLS_CERT, cert_.data (), cert_.size ()) != 0
        || socket_->setsockopt (ZLINK_TLS_KEY, key_.data (), key_.size ()) != 0)
        return -1;
    return 0;
}

int spot_node_t::apply_tls_client (socket_base_t *socket_,
                                   const std::string &ca_cert_,
                                   const std::string &hostname_,
                                   int trust_system_)
{
    if (!socket_)
        return -1;
    if (ca_cert_.empty () && hostname_.empty () && trust_system_ == 0)
        return 0;
    if (!ca_cert_.empty ()
        && socket_->setsockopt (ZLINK_TLS_CA, ca_cert_.data (), ca_cert_.size ())
             != 0)
        return -1;
    if (!hostname_.empty ()
        && socket_->setsockopt (ZLINK_TLS_HOSTNAME, hostname_.data (),
                                hostname_.size ())
             != 0)
        return -1;
    if (socket_->setsockopt (ZLINK_TLS_TRUST_SYSTEM, &trust_system_,
                             sizeof (trust_system_))
        != 0)
        return -1;
    return 0;
}

bool spot_node_t::validate_service_name (const std::string &name_)
{
    if (name_.empty () || name_.size () > 64)
        return false;
    for (size_t i = 0; i < name_.size (); ++i) {
        const char c = name_[i];
        if (!((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '.'
              || c == '-'))
            return false;
    }
    return true;
}

bool spot_node_t::validate_public_endpoint (const std::string &endpoint_)
{
    if (endpoint_.empty ())
        return false;
    if (endpoint_.find ("tcp://*:") == 0 || endpoint_.find ("tcp://0.0.0.0:")
                                              == 0
        || endpoint_.find ("tcp://[::]:") == 0)
        return false;
    return true;
}

bool spot_node_t::recv_ctrl_reply (socket_base_t *socket_, int *out_errno_)
{
    std::vector<std::string> frames;
    if (recv_ascii_command (socket_, &frames) != 0)
        return false;
    if (!frames.empty () && frames[0] == "ok") {
        if (out_errno_)
            *out_errno_ = 0;
        return true;
    }
    if (out_errno_ && frames.size () > 1)
        *out_errno_ = atoi (frames[1].c_str ());
    return false;
}

int spot_node_t::start_data_plane ()
{
    if (!_runtime) {
        errno = EFAULT;
        return -1;
    }
    return _runtime->start ();
}

const std::string &spot_node_t::pub_ingress_endpoint () const
{
    return _runtime->pub_ingress_endpoint;
}

const std::string &spot_node_t::sub_fanout_endpoint () const
{
    return _runtime->sub_fanout_endpoint;
}

bool spot_node_t::has_active_peers () const
{
    return _active_peer_count.load (std::memory_order_acquire) != 0;
}

bool spot_node_t::has_local_filtered_subs () const
{
    return _local_filtered_sub_count.load (std::memory_order_acquire) != 0;
}

void spot_node_t::note_local_sub_filters_changed (bool had_filters_,
                                                  bool has_filters_)
{
    if (had_filters_ == has_filters_)
        return;

    if (has_filters_) {
        _local_filtered_sub_count.fetch_add (1, std::memory_order_acq_rel);
        return;
    }

    uint32_t current = _local_filtered_sub_count.load (std::memory_order_acquire);
    while (current != 0
           && !_local_filtered_sub_count.compare_exchange_weak (
             current, current - 1, std::memory_order_acq_rel,
             std::memory_order_acquire)) {
    }
}

int spot_node_t::replay_subscriptions_if_active_peers ()
{
    if (ensure_healthy () != 0)
        return -1;
    if (!has_active_peers ())
        return 0;
    if (std::getenv ("ZLINK_DEBUG_SPOT_REPLAY"))
        std::fprintf (stderr, "[spot-replay] immediate replay request\n");
    if (send_data_plane_command ("replay_subscriptions") != 0)
        return -1;
    queue_all_subscription_ready_filters ();
    return 0;
}

void spot_node_t::schedule_subscription_replay ()
{
    service_control_runtime_t *runtime = NULL;
    uint64_t task_id = 0;
    unsigned int attempts = 0;
    {
        scoped_lock_t lock (_sync);
        _subscription_replay_pending = true;
        const unsigned int target_attempts =
          subscription_replay_attempt_count (_active_peer_endpoints);
        if (_subscription_replay_attempts < target_attempts)
            _subscription_replay_attempts = target_attempts;
        _subscription_replay_holdoff_ticks = 0;
        attempts = _subscription_replay_attempts;
        if (_runtime) {
            task_id = _runtime->task_id;
            runtime = _ctx ? _ctx->service_control_runtime () : NULL;
        }
    }
    if (std::getenv ("ZLINK_DEBUG_SPOT_REPLAY"))
        std::fprintf (stderr, "[spot-replay] scheduled attempts=%u\n",
                      attempts);
    if (runtime && task_id != 0)
        runtime->wakeup_task (task_id);
}

std::string spot_node_t::first_active_peer_endpoint () const
{
    scoped_lock_t lock (const_cast<mutex_t &> (_sync));
    if (_active_peer_endpoints.empty ())
        return std::string ();
    return *_active_peer_endpoints.begin ();
}

int spot_node_t::ensure_healthy () const
{
    if (!_lifecycle.is_running ()) {
        const int fe = _lifecycle.fault_errno ();
        errno = fe != 0 ? fe : EFSM;
        return -1;
    }
    scoped_lock_t lock (const_cast<mutex_t &> (_sync));
    if (!_runtime) {
        errno = EFAULT;
        return -1;
    }
    return _runtime->ensure_healthy ();
}

void spot_node_t::debug_mark_fault (int err_)
{
    std::vector<spot_pub_t *> pubs;
    std::vector<spot_sub_t *> subs;
    {
        scoped_lock_t lock (_sync);
        if (!_runtime)
            return;
        _runtime->mark_fault (err_);
        pubs.assign (_pubs.begin (), _pubs.end ());
        subs.assign (_subs.begin (), _subs.end ());
    }

    for (size_t i = 0; i < pubs.size (); ++i)
        submit_pub_summary (pubs[i], ZLINK_TOPOLOGY_STATE_ERROR,
                            _runtime->fault_errno);
    for (size_t i = 0; i < subs.size (); ++i)
        submit_sub_summary (subs[i], ZLINK_TOPOLOGY_STATE_LOST,
                            _runtime->fault_errno);
}

void spot_node_t::control_task (void *arg_)
{
    static_cast<spot_node_t *> (arg_)->control_tick ();
}

void spot_node_t::control_tick ()
{
    if (!_runtime || _runtime->stop.get () != 0)
        return;
    if (ensure_healthy () != 0)
        return;

    refresh_discovery_peers ();
    if (std::getenv ("ZLINK_DEBUG_SPOT_SKIP_CONTROL_EXTRA")) {
        bool skip_extra = false;
        {
            scoped_lock_t lock (_sync);
            skip_extra = _discovery == NULL
                         && !_connected_peer_endpoints.empty ()
                         && !_subscription_replay_pending
                         && !_subscription_ready_refresh_pending
                         && !_pub_delivery_ready_refresh_pending;
        }
        if (skip_extra)
            return;
    }
    refresh_connected_peer_endpoints ();
    emit_pending_subscription_replays ();
    emit_pending_subscription_ready_events ();
}

void spot_node_t::emit_pending_subscription_replays ()
{
    bool should_replay = false;
    {
        scoped_lock_t lock (_sync);
        if (!_subscription_replay_pending)
            return;
        if (_active_peer_endpoints.empty ())
            return;
        if (_subscription_replay_attempts == 0) {
            _subscription_replay_pending = false;
            _subscription_replay_holdoff_ticks = 0;
            return;
        }
        if (_subscription_replay_holdoff_ticks > 0) {
            --_subscription_replay_holdoff_ticks;
            return;
        }
        should_replay = true;
        --_subscription_replay_attempts;
        _subscription_replay_holdoff_ticks = 10;
        if (_subscription_replay_attempts == 0)
            _subscription_replay_pending = false;
    }

    if (!should_replay)
        return;

    if (std::getenv ("ZLINK_DEBUG_SPOT_REPLAY"))
        std::fprintf (stderr, "[spot-replay] emit pending replay\n");
    if (send_data_plane_command ("replay_subscriptions") != 0) {
        debug_mark_fault (errno);
        return;
    }
}

void spot_node_t::refresh_discovery_peers ()
{
    discovery_t *discovery = NULL;
    std::string service;
    uint64_t seq = 0;
    {
        scoped_lock_t lock (_sync);
        discovery = _discovery;
        service = _discovery_service;
        if (!discovery || service.empty ())
            return;
        seq = discovery->service_update_seq (service);
        if (_pending_service_updates.empty () && seq == _discovery_seq)
            return;
        _pending_service_updates.clear ();
        _discovery_seq = seq;
    }

    std::vector<provider_info_t> providers;
    discovery->snapshot_providers (service, &providers);

    std::set<std::string> new_endpoints;
    std::string self_endpoint;
    {
        scoped_lock_t lock (_sync);
        self_endpoint = _advertise_endpoint;
    }
    for (size_t i = 0; i < providers.size (); ++i) {
        if (!providers[i].endpoint.empty ()
            && providers[i].endpoint != self_endpoint)
            new_endpoints.insert (providers[i].endpoint);
    }

    std::vector<std::string> to_connect;
    std::vector<std::string> to_disconnect;
    size_t old_active_count = 0;
    {
        scoped_lock_t lock (_sync);
        old_active_count = _active_peer_endpoints.size ();
        for (std::set<std::string>::const_iterator it = new_endpoints.begin ();
             it != new_endpoints.end (); ++it) {
            if (_discovery_peer_endpoints.count (*it) == 0
                && _active_peer_endpoints.count (*it) == 0)
                to_connect.push_back (*it);
        }

        for (std::set<std::string>::const_iterator it =
               _discovery_peer_endpoints.begin ();
             it != _discovery_peer_endpoints.end (); ++it) {
            if (new_endpoints.count (*it) == 0
                && _manual_peer_endpoints.count (*it) == 0)
                to_disconnect.push_back (*it);
        }
    }

    for (size_t i = 0; i < to_connect.size (); ++i) {
        if (send_data_plane_command ("connect_peer_pub", to_connect[i].c_str ())
            == 0) {
            scoped_lock_t lock (_sync);
            if (_active_peer_endpoints.insert (to_connect[i]).second)
                _active_peer_count.fetch_add (1, std::memory_order_acq_rel);
        }
    }

    for (size_t i = 0; i < to_disconnect.size (); ++i) {
        if (send_data_plane_command ("disconnect_peer_pub", to_disconnect[i].c_str ())
            == 0) {
            scoped_lock_t lock (_sync);
            if (_active_peer_endpoints.erase (to_disconnect[i]) != 0)
                _active_peer_count.fetch_sub (1, std::memory_order_acq_rel);
        }
    }

    size_t new_active_count = 0;
    {
        scoped_lock_t lock (_sync);
        _discovery_peer_endpoints.swap (new_endpoints);
        new_active_count = _active_peer_endpoints.size ();
    }

    if (old_active_count == 0 && new_active_count > 0) {
        if (has_local_filtered_subs ()) {
            schedule_subscription_replay ();
            if (replay_subscriptions_if_active_peers () != 0) {
                debug_mark_fault (errno);
                return;
            }
        }
        refresh_sub_peer_summaries (true, false);
    } else if (!to_connect.empty () && new_active_count > 0) {
        if (has_local_filtered_subs ()) {
            schedule_subscription_replay ();
            if (replay_subscriptions_if_active_peers () != 0) {
                debug_mark_fault (errno);
                return;
            }
        }
    } else if (old_active_count > 0 && new_active_count == 0)
        refresh_sub_peer_summaries (false, true);
}

void spot_node_t::refresh_connected_peer_endpoints ()
{
    std::set<std::string> connected;
    {
        scoped_lock_t lock (_sync);
        if (!_runtime || !_runtime->mesh_xsub)
            return;
        snapshot_connected_mesh_peer_endpoints (_runtime->mesh_xsub,
                                                &connected);
    }

    bool changed = false;
    std::vector<spot_sub_t *> subs;
    std::vector<spot_pub_t *> pubs;
    std::vector<std::pair<std::string, uint32_t> > pub_ready_updates;
    bool became_empty = false;
    {
        scoped_lock_t lock (_sync);
        if (connected == _connected_peer_endpoints)
            return;
        const size_t previous_connected_count = _connected_peer_endpoints.size ();
        _connected_peer_endpoints.swap (connected);
        changed = true;
        if (_connected_peer_endpoints.empty ()) {
            _subscription_ready_refresh_pending = false;
            _subscription_ready_refresh_holdoff_ticks = 0;
            _pending_subscription_ready_filters.clear ();
            _pub_delivery_ready_refresh_pending = false;
            _pub_delivery_ready_refresh_holdoff_ticks = 0;
            _pending_pub_delivery_ready_counts.clear ();
            subs.assign (_subs.begin (), _subs.end ());
            pubs.assign (_pubs.begin (), _pubs.end ());
            for (std::map<std::string, std::set<std::string> >::iterator it =
                   _pub_delivery_ready_sources.begin ();
                 it != _pub_delivery_ready_sources.end (); ++it) {
                pub_ready_updates.push_back (
                  std::make_pair (it->first, static_cast<uint32_t> (0)));
            }
            _pub_delivery_ready_sources.clear ();
            became_empty = true;
        } else {
            subs.assign (_subs.begin (), _subs.end ());
            pubs.assign (_pubs.begin (), _pubs.end ());
            const size_t connected_peer_count = _connected_peer_endpoints.size ();
            if (connected_peer_count < previous_connected_count) {
                const uint32_t max_ready =
                  static_cast<uint32_t> (connected_peer_count);
                for (std::map<std::string, std::set<std::string> >::iterator it =
                       _pub_delivery_ready_sources.begin ();
                     it != _pub_delivery_ready_sources.end (); ++it) {
                    const uint32_t current_ready =
                      static_cast<uint32_t> (it->second.size ());
                    if (current_ready <= max_ready)
                        continue;
                    pub_ready_updates.push_back (
                      std::make_pair (it->first, max_ready));
                }
            }
        }
    }

    for (size_t i = 0; i < pubs.size (); ++i) {
        for (size_t j = 0; j < pub_ready_updates.size (); ++j) {
            pubs[i]->emit_delivery_ready_changed_event (
              pub_ready_updates[j].first.c_str (), false,
              ZLINK_SERVICE_EVENT_SUBJECT_NONE,
              pub_ready_updates[j].second);
            pubs[i]->emit_first_delivery_ready_changed_event (
              pub_ready_updates[j].first.c_str (), false,
              ZLINK_SERVICE_EVENT_SUBJECT_NONE,
              pub_ready_updates[j].second);
        }
    }

    if (became_empty) {
        for (size_t i = 0; i < subs.size (); ++i)
            subs[i]->mark_all_subjects_lost (NULL);
        return;
    }

    bool has_filters = false;
    for (size_t i = 0; i < subs.size (); ++i) {
        if (subs[i]->has_filters ()) {
            has_filters = true;
            break;
        }
    }

    if (changed && has_filters) {
        if (send_data_plane_command ("replay_subscriptions") != 0) {
            debug_mark_fault (errno);
            return;
        }
        queue_all_subscription_ready_filters ();
    }
}

std::string spot_node_t::first_connected_peer_endpoint () const
{
    scoped_lock_t lock (const_cast<mutex_t &> (_sync));
    if (_connected_peer_endpoints.empty ())
        return std::string ();
    return *_connected_peer_endpoints.begin ();
}

void spot_node_t::schedule_subscription_ready_refresh ()
{
    service_control_runtime_t *runtime = NULL;
    uint64_t task_id = 0;
    unsigned int holdoff_ticks = 20;
    {
        scoped_lock_t lock (_sync);
        _subscription_ready_refresh_pending = true;
        holdoff_ticks =
          subscription_ready_holdoff_ticks (_connected_peer_endpoints);
        _subscription_ready_refresh_holdoff_ticks = holdoff_ticks;
        if (_runtime) {
            task_id = _runtime->task_id;
            runtime = _ctx ? _ctx->service_control_runtime () : NULL;
        }
    }
    if (runtime && task_id != 0)
        runtime->wakeup_task (task_id);
}

void spot_node_t::schedule_pub_delivery_ready_refresh ()
{
    service_control_runtime_t *runtime = NULL;
    uint64_t task_id = 0;
    unsigned int holdoff_ticks = 20;
    {
        scoped_lock_t lock (_sync);
        _pub_delivery_ready_refresh_pending = true;
        holdoff_ticks =
          pub_delivery_ready_holdoff_ticks (_active_peer_endpoints);
        _pub_delivery_ready_refresh_holdoff_ticks = holdoff_ticks;
        if (_runtime) {
            task_id = _runtime->task_id;
            runtime = _ctx ? _ctx->service_control_runtime () : NULL;
        }
    }
    if (runtime && task_id != 0)
        runtime->wakeup_task (task_id);
}

void spot_node_t::queue_all_subscription_ready_filters ()
{
    std::vector<spot_sub_t *> subs;
    {
        scoped_lock_t lock (_sync);
        subs.assign (_subs.begin (), _subs.end ());
    }

    std::set<std::string> raw_filters;
    for (size_t i = 0; i < subs.size (); ++i)
        subs[i]->append_raw_filters (&raw_filters);

    if (raw_filters.empty ())
        return;

    {
        scoped_lock_t lock (_sync);
        _pending_subscription_ready_filters.insert (raw_filters.begin (),
                                                    raw_filters.end ());
    }
    schedule_subscription_ready_refresh ();
}

void spot_node_t::queue_subscription_ready_filter (const std::string &raw_filter_)
{
    if (raw_filter_.empty ())
        return;

    {
        scoped_lock_t lock (_sync);
        _pending_subscription_ready_filters.insert (raw_filter_);
    }
    schedule_subscription_ready_refresh ();
}

void spot_node_t::emit_pending_subscription_ready_events ()
{
    std::vector<spot_sub_t *> subs;
    std::set<std::string> raw_filters;
    std::string ready_endpoint;
    {
        scoped_lock_t lock (_sync);
        if (!_subscription_ready_refresh_pending)
            return;
        if (_connected_peer_endpoints.empty ()) {
            _subscription_ready_refresh_pending = false;
            _subscription_ready_refresh_holdoff_ticks = 0;
            _pending_subscription_ready_filters.clear ();
            return;
        }
        if (_subscription_ready_refresh_holdoff_ticks > 0) {
            --_subscription_ready_refresh_holdoff_ticks;
            return;
        }
        if (_pending_subscription_ready_filters.empty ()) {
            _subscription_ready_refresh_pending = false;
            _subscription_ready_refresh_holdoff_ticks = 0;
            return;
        }
        ready_endpoint = *_connected_peer_endpoints.begin ();
        subs.assign (_subs.begin (), _subs.end ());
        raw_filters.swap (_pending_subscription_ready_filters);
        _subscription_ready_refresh_pending = false;
        _subscription_ready_refresh_holdoff_ticks = 0;
    }

    for (std::set<std::string>::const_iterator filter_it =
           raw_filters.begin ();
         filter_it != raw_filters.end (); ++filter_it) {
        for (size_t i = 0; i < subs.size (); ++i) {
            std::vector<spot_sub_t::subject_descriptor_t> subjects;
            subs[i]->append_subjects_for_raw_filter (*filter_it, &subjects);
            for (size_t j = 0; j < subjects.size (); ++j)
                subs[i]->mark_subject_subscription_ready (
                  subjects[j], ready_endpoint.c_str ());
        }
    }
}

void spot_node_t::emit_pending_pub_delivery_ready_events ()
{
    std::vector<spot_pub_t *> pubs;
    std::vector<std::pair<std::string, uint32_t> > updates;
    {
        scoped_lock_t lock (_sync);
        if (!_pub_delivery_ready_refresh_pending)
            return;
        if (_pub_delivery_ready_refresh_holdoff_ticks > 0) {
            --_pub_delivery_ready_refresh_holdoff_ticks;
            return;
        }
        if (_pending_pub_delivery_ready_counts.empty ()) {
            _pub_delivery_ready_refresh_pending = false;
            _pub_delivery_ready_refresh_holdoff_ticks = 0;
            return;
        }
        pubs.assign (_pubs.begin (), _pubs.end ());
        for (std::map<std::string, uint32_t>::const_iterator it =
               _pending_pub_delivery_ready_counts.begin ();
             it != _pending_pub_delivery_ready_counts.end (); ++it) {
            updates.push_back (std::make_pair (it->first, it->second));
        }
        _pending_pub_delivery_ready_counts.clear ();
        _pub_delivery_ready_refresh_pending = false;
        _pub_delivery_ready_refresh_holdoff_ticks = 0;
    }

    for (size_t i = 0; i < pubs.size (); ++i) {
        for (size_t j = 0; j < updates.size (); ++j) {
            pubs[i]->emit_first_delivery_ready_changed_event (
              updates[j].first.c_str (), false,
              ZLINK_SERVICE_EVENT_SUBJECT_NONE, updates[j].second);
            if (updates[j].second > 0)
                pubs[i]->dispatch_send_ready ();
        }
    }
}

void spot_node_t::notify_subscription_forwarded (const std::string &raw_filter_)
{
    queue_subscription_ready_filter (raw_filter_);
}

void spot_node_t::notify_pub_delivery_ready_ack (
  const std::string &target_endpoint_,
  const std::string &subject_,
  const std::string &ack_source_id_,
  bool subscribe_)
{
    if (target_endpoint_.empty () || subject_.empty () || ack_source_id_.empty ())
        return;

    std::string self_endpoint;
    {
        scoped_lock_t lock (_sync);
        self_endpoint =
          _advertise_endpoint.empty () ? _bound_endpoint : _advertise_endpoint;
    }
    if (self_endpoint.empty () || self_endpoint != target_endpoint_)
        return;

    std::vector<spot_pub_t *> pubs;
    uint32_t ready_count = 0;
    {
        scoped_lock_t lock (_sync);
        std::set<std::string> &ready_sources =
          _pub_delivery_ready_sources[subject_];
        const uint32_t current_count =
          static_cast<uint32_t> (ready_sources.size ());

        if (subscribe_) {
            if (!ready_sources.insert (ack_source_id_).second)
                return;
            ready_count = static_cast<uint32_t> (ready_sources.size ());
        } else {
            if (ready_sources.erase (ack_source_id_) == 0)
                return;
            ready_count = static_cast<uint32_t> (ready_sources.size ());
            if (ready_sources.empty ())
                _pub_delivery_ready_sources.erase (subject_);
        }

        pubs.assign (_pubs.begin (), _pubs.end ());
        if (!subscribe_) {
            _pending_pub_delivery_ready_counts.erase (subject_);
            _pub_delivery_ready_refresh_pending = false;
            _pub_delivery_ready_refresh_holdoff_ticks = 0;
        }
    }

    for (size_t i = 0; i < pubs.size (); ++i) {
        pubs[i]->emit_delivery_ready_changed_event (
          subject_.c_str (), false, ZLINK_SERVICE_EVENT_SUBJECT_NONE,
          ready_count);
        if (!subscribe_) {
            pubs[i]->emit_first_delivery_ready_changed_event (
              subject_.c_str (), false, ZLINK_SERVICE_EVENT_SUBJECT_NONE,
              ready_count);
        }
    }

}

void spot_node_t::notify_pub_first_delivery_ready_settled (
  const std::string &subject_,
  uint32_t ready_count_)
{
    if (subject_.empty ())
        return;

    std::vector<spot_pub_t *> pubs;
    {
        scoped_lock_t lock (_sync);
        pubs.assign (_pubs.begin (), _pubs.end ());
    }

    for (size_t i = 0; i < pubs.size (); ++i) {
        pubs[i]->emit_first_delivery_ready_changed_event (
          subject_.c_str (), false, ZLINK_SERVICE_EVENT_SUBJECT_NONE,
          ready_count_);
        if (ready_count_ > 0)
            pubs[i]->dispatch_send_ready ();
    }
}

int spot_node_t::send_subscription_update (const std::string &raw_filter_,
                                           bool subscribe_)
{
    if (raw_filter_.empty ()) {
        errno = EINVAL;
        return -1;
    }

    return send_data_plane_command (
      subscribe_ ? "subscription_subscribe" : "subscription_unsubscribe",
      raw_filter_.c_str ());
}

int spot_node_t::send_ready_ack_update (const std::string &target_endpoint_,
                                        const std::string &raw_filter_,
                                        const std::string &ack_source_id_,
                                        bool subscribe_)
{
    if (target_endpoint_.empty () || raw_filter_.empty ()
        || ack_source_id_.empty ()) {
        errno = EINVAL;
        return -1;
    }

    const std::string arg =
      make_ready_ack_arg (target_endpoint_, raw_filter_, ack_source_id_);
    return send_data_plane_command (
      subscribe_ ? "ready_ack_subscribe" : "ready_ack_unsubscribe",
      arg.c_str ());
}

int spot_node_t::send_data_plane_command (const char *verb_,
                                          const char *arg_) const
{
    if (!_runtime) {
        errno = EFAULT;
        return -1;
    }
    return _runtime->send_command (verb_, arg_);
}

int spot_node_t::wait_facade_peer (socket_base_t *socket_) const
{
    const uint64_t deadline_ms = clock_t ().now_ms () + 1000;
    while (clock_t ().now_ms () < deadline_ms) {
        zlink_monitor_snapshot_t snapshot;
        if (socket_ && socket_->monitor_snapshot (&snapshot) == 0
            && snapshot.ready_peer_count > 0)
            return 0;
        usleep (1000);
    }
    errno = ETIMEDOUT;
    return -1;
}

void spot_node_t::track_owned_socket (socket_base_t *socket_)
{
    _lifecycle.register_socket (socket_);
}

void spot_node_t::untrack_owned_socket (const socket_base_t *socket_)
{
    _lifecycle.unregister_socket (socket_);
}

int spot_node_t::wait_owned_socket_removals (int timeout_ms_)
{
    return _lifecycle.wait_drained (timeout_ms_);
}

std::string spot_node_t::summary_service_name () const
{
    scoped_lock_t lock (const_cast<mutex_t &> (_sync));
    if (!_service_name.empty ())
        return _service_name;
    return _discovery_service;
}

void spot_node_t::submit_pub_summary (spot_pub_t *pub_,
                                      uint16_t state_,
                                      int error_code_)
{
    if (!pub_)
        return;

    discovery_t *discovery = NULL;
    std::string service_name;
    std::string endpoint;
    {
        scoped_lock_t lock (_sync);
        discovery = _discovery;
        service_name = !_service_name.empty () ? _service_name : _discovery_service;
        endpoint = _advertise_endpoint.empty () ? _bound_endpoint : _advertise_endpoint;
    }
    if (!discovery || service_name.empty ())
        return;

    zlink_routing_id_t rid;
    if (pub_->routing_id (&rid) != 0 || rid.size == 0)
        return;

    zlink_registry_topology_entry_t entry;
    memset (&entry, 0, sizeof (entry));
    entry.routing_id = rid;
    entry.service_kind = ZLINK_SERVICE_KIND_SPOT_PUB;
    strncpy (entry.service_name, service_name.c_str (),
             sizeof (entry.service_name) - 1);
    strncpy (entry.endpoint, endpoint.c_str (), sizeof (entry.endpoint) - 1);
    entry.source = ZLINK_TOPOLOGY_SOURCE_DISCOVERY;
    entry.state = static_cast<zlink_topology_state_t> (state_);
    entry.desired_count = 1;
    entry.ready_count = state_ == ZLINK_TOPOLOGY_STATE_READY ? 1 : 0;
    entry.error_code = static_cast<uint32_t> (error_code_ > 0 ? error_code_ : 0);
    entry.last_reported_ms = clock_t ().now_ms ();
    discovery->upsert_service_summary (entry);
}

void spot_node_t::submit_sub_summary (spot_sub_t *sub_,
                                      uint16_t state_,
                                      int error_code_)
{
    if (!sub_)
        return;

    discovery_t *discovery = NULL;
    std::string service_name;
    {
        scoped_lock_t lock (_sync);
        discovery = _discovery;
        service_name = !_service_name.empty () ? _service_name : _discovery_service;
    }
    if (!discovery || service_name.empty ())
        return;

    zlink_routing_id_t rid;
    if (sub_->routing_id (&rid) != 0 || rid.size == 0)
        return;

    zlink_registry_topology_entry_t entry;
    memset (&entry, 0, sizeof (entry));
    entry.routing_id = rid;
    entry.service_kind = ZLINK_SERVICE_KIND_SPOT_SUB;
    strncpy (entry.service_name, service_name.c_str (),
             sizeof (entry.service_name) - 1);
    entry.source = ZLINK_TOPOLOGY_SOURCE_DISCOVERY;
    entry.state = static_cast<zlink_topology_state_t> (state_);
    entry.desired_count = 1;
    entry.ready_count = state_ == ZLINK_TOPOLOGY_STATE_READY ? 1 : 0;
    entry.error_code = static_cast<uint32_t> (error_code_ > 0 ? error_code_ : 0);
    entry.last_reported_ms = clock_t ().now_ms ();
    discovery->upsert_service_summary (entry);
}

void spot_node_t::submit_stopped_summaries ()
{
    std::vector<spot_pub_t *> pubs;
    std::vector<spot_sub_t *> subs;
    {
        scoped_lock_t lock (_sync);
        pubs.assign (_pubs.begin (), _pubs.end ());
        subs.assign (_subs.begin (), _subs.end ());
    }

    for (size_t i = 0; i < pubs.size (); ++i)
        submit_pub_summary (pubs[i], ZLINK_TOPOLOGY_STATE_STOPPED, 0);
    for (size_t i = 0; i < subs.size (); ++i)
        submit_sub_summary (subs[i], ZLINK_TOPOLOGY_STATE_STOPPED, 0);
}

void spot_node_t::refresh_existing_summaries ()
{
    std::vector<spot_pub_t *> pubs;
    std::vector<spot_sub_t *> subs;
    bool bound = false;
    {
        scoped_lock_t lock (_sync);
        pubs.assign (_pubs.begin (), _pubs.end ());
        subs.assign (_subs.begin (), _subs.end ());
        bound = !_bound_endpoint.empty ();
    }

    if (bound) {
        for (size_t i = 0; i < pubs.size (); ++i)
            submit_pub_summary (pubs[i], ZLINK_TOPOLOGY_STATE_READY, 0);
    }
    for (size_t i = 0; i < subs.size (); ++i) {
        const uint16_t state =
          subs[i]->has_filters () ? ZLINK_TOPOLOGY_STATE_READY
                                  : ZLINK_TOPOLOGY_STATE_CONNECTING;
        submit_sub_summary (subs[i], state, 0);
    }
}

void spot_node_t::refresh_sub_peer_summaries (bool has_active_peers,
                                              bool lost_transition)
{
    std::vector<spot_sub_t *> subs;
    {
        scoped_lock_t lock (_sync);
        subs.assign (_subs.begin (), _subs.end ());
    }

    for (size_t i = 0; i < subs.size (); ++i) {
        if (lost_transition) {
            submit_sub_summary (subs[i], ZLINK_TOPOLOGY_STATE_LOST, 0);
        } else if (has_active_peers) {
            const bool ready = subs[i]->has_filters ();
            submit_sub_summary (subs[i], ready ? ZLINK_TOPOLOGY_STATE_READY
                                               : ZLINK_TOPOLOGY_STATE_CONNECTING,
                                0);
        }
    }
}

void spot_node_t::snapshot_raw_subscription_filters (
  std::set<std::string> *out_) const
{
    if (!out_)
        return;

    std::vector<spot_sub_t *> subs;
    {
        scoped_lock_t lock (const_cast<mutex_t &> (_sync));
        subs.assign (_subs.begin (), _subs.end ());
    }

    for (size_t i = 0; i < subs.size (); ++i)
        subs[i]->append_replay_raw_filters (out_);
}

int spot_node_t::resolve_advertise_endpoint (const char *advertise_endpoint_,
                                             std::string *out_) const
{
    if (!out_) {
        errno = EINVAL;
        return -1;
    }

    if (advertise_endpoint_ && advertise_endpoint_[0] != '\0') {
        *out_ = advertise_endpoint_;
        return validate_public_endpoint (*out_) ? 0 : -1;
    }

    scoped_lock_t lock (const_cast<mutex_t &> (_sync));
    if (_bound_endpoint.empty ()) {
        errno = EFSM;
        return -1;
    }
    *out_ = _bound_endpoint;
    if (!validate_public_endpoint (*out_)) {
        errno = EINVAL;
        return -1;
    }
    return 0;
}

int spot_node_t::bind (const char *endpoint_)
{
    if (!endpoint_ || endpoint_[0] == '\0') {
        errno = EINVAL;
        return -1;
    }
    if (ensure_healthy () != 0)
        return -1;

    {
        scoped_lock_t lock (_sync);
        if (!_bound_endpoint.empty ()) {
            errno = EBUSY;
            return -1;
        }
    }

    if (send_data_plane_command ("bind_pub", endpoint_) != 0)
        return -1;

    std::vector<spot_pub_t *> pubs;
    bool should_register = false;
    {
        scoped_lock_t lock (_sync);
        _bound_endpoint = endpoint_;
        _server_tls_locked = true;
        pubs.assign (_pubs.begin (), _pubs.end ());
        should_register = _discovery != NULL;
    }
    if (should_register && ensure_registered () != 0)
        return -1;
    for (size_t i = 0; i < pubs.size (); ++i)
        submit_pub_summary (pubs[i], ZLINK_TOPOLOGY_STATE_READY, 0);
    return 0;
}

int spot_node_t::connect_peer_pub (const char *peer_pub_endpoint_)
{
    if (!peer_pub_endpoint_ || peer_pub_endpoint_[0] == '\0') {
        errno = EINVAL;
        return -1;
    }
    if (ensure_healthy () != 0)
        return -1;

    bool need_connect = false;
    bool had_active_peers = false;
    {
        scoped_lock_t lock (_sync);
        if (_discovery) {
            errno = EBUSY;
            return -1;
        }
        had_active_peers = !_active_peer_endpoints.empty ();
        if (_manual_peer_endpoints.count (peer_pub_endpoint_) != 0)
            return 0;
        _manual_peer_endpoints.insert (peer_pub_endpoint_);
        if (_active_peer_endpoints.count (peer_pub_endpoint_) == 0)
            need_connect = true;
    }

    if (need_connect && send_data_plane_command ("connect_peer_pub",
                                                 peer_pub_endpoint_)
                           != 0) {
        scoped_lock_t lock (_sync);
        _manual_peer_endpoints.erase (peer_pub_endpoint_);
        return -1;
    }

    bool has_active_peers = false;
    {
        scoped_lock_t lock (_sync);
        _mesh_client_tls_locked = true;
        if (_active_peer_endpoints.insert (peer_pub_endpoint_).second)
            _active_peer_count.fetch_add (1, std::memory_order_acq_rel);
        has_active_peers = !_active_peer_endpoints.empty ();
    }
    if (has_active_peers) {
        if (has_local_filtered_subs ()) {
            schedule_subscription_replay ();
            if (replay_subscriptions_if_active_peers () != 0)
                return -1;
        }
        if (!had_active_peers)
            refresh_sub_peer_summaries (true, false);
    }
    return 0;
}

int spot_node_t::disconnect_peer_pub (const char *peer_pub_endpoint_)
{
    if (!peer_pub_endpoint_ || peer_pub_endpoint_[0] == '\0') {
        errno = EINVAL;
        return -1;
    }
    if (ensure_healthy () != 0)
        return -1;

    bool need_disconnect = false;
    bool had_active_peers = false;
    {
        scoped_lock_t lock (_sync);
        if (_discovery) {
            errno = EBUSY;
            return -1;
        }
        had_active_peers = !_active_peer_endpoints.empty ();
        _manual_peer_endpoints.erase (peer_pub_endpoint_);
        if (_discovery_peer_endpoints.count (peer_pub_endpoint_) == 0
            && _active_peer_endpoints.count (peer_pub_endpoint_) != 0)
            need_disconnect = true;
    }

    if (need_disconnect
        && send_data_plane_command ("disconnect_peer_pub", peer_pub_endpoint_)
             != 0)
        return -1;

    if (need_disconnect) {
        bool has_active_peers = false;
        {
            scoped_lock_t lock (_sync);
            if (_active_peer_endpoints.erase (peer_pub_endpoint_) != 0)
                _active_peer_count.fetch_sub (1, std::memory_order_acq_rel);
            has_active_peers = !_active_peer_endpoints.empty ();
        }
        if (had_active_peers && !has_active_peers) {
            std::vector<spot_sub_t *> subs;
            std::vector<spot_pub_t *> pubs;
            std::vector<std::pair<std::string, uint32_t> > pub_ready_updates;
            {
                scoped_lock_t lock (_sync);
                _connected_peer_endpoints.clear ();
                _subscription_ready_refresh_pending = false;
                _subscription_ready_refresh_holdoff_ticks = 0;
                _pending_subscription_ready_filters.clear ();
                subs.assign (_subs.begin (), _subs.end ());
                pubs.assign (_pubs.begin (), _pubs.end ());
                for (std::map<std::string, std::set<std::string> >::iterator it =
                       _pub_delivery_ready_sources.begin ();
                     it != _pub_delivery_ready_sources.end (); ++it) {
                    pub_ready_updates.push_back (
                      std::make_pair (it->first, static_cast<uint32_t> (0)));
                }
                _pub_delivery_ready_sources.clear ();
            }
            refresh_sub_peer_summaries (false, true);
            for (size_t i = 0; i < subs.size (); ++i)
                subs[i]->mark_all_subjects_lost (NULL);
            for (size_t i = 0; i < pubs.size (); ++i) {
                for (size_t j = 0; j < pub_ready_updates.size (); ++j) {
                    pubs[i]->emit_delivery_ready_changed_event (
                      pub_ready_updates[j].first.c_str (), false,
                      ZLINK_SERVICE_EVENT_SUBJECT_NONE,
                      pub_ready_updates[j].second);
                    pubs[i]->emit_first_delivery_ready_changed_event (
                      pub_ready_updates[j].first.c_str (), false,
                      ZLINK_SERVICE_EVENT_SUBJECT_NONE,
                      pub_ready_updates[j].second);
                }
            }
        }
    }
    return 0;
}

int spot_node_t::ensure_registered ()
{
    if (ensure_healthy () != 0)
        return -1;

    discovery_t *discovery = NULL;
    std::string advertise;
    bool need_default_pub = false;
    {
        scoped_lock_t lock (_sync);
        discovery = _discovery;
        if (_registered)
            return 0;
        if (_bound_endpoint.empty ()) {
            errno = EFSM;
            return -1;
        }
        advertise = _advertise_endpoint.empty () ? _bound_endpoint
                                                 : _advertise_endpoint;
    }
    if (!discovery) {
        errno = EFSM;
        return -1;
    }
    if (!validate_public_endpoint (advertise)) {
        errno = EINVAL;
        return -1;
    }

    std::string resolved;
    if (discovery->register_service (discovery_protocol::service_type_spot_node,
                                     _service_name.c_str (),
                                     advertise.c_str (), 1, &resolved)
        != 0) {
        return -1;
    }

    {
        scoped_lock_t lock (_sync);
        _registered = true;
        _advertise_endpoint = resolved.empty () ? advertise : resolved;
        if (!discovery->latest_registry_uplink (&_registration_uplink_endpoint))
            _registration_uplink_endpoint.clear ();
        _registration_tls_locked = true;
        need_default_pub = _pubs.empty () && !_default_pub;
    }

    if (need_default_pub && !ensure_default_pub ())
        return -1;

    return 0;
}

int spot_node_t::unregister_registered ()
{
    if (ensure_healthy () != 0)
        return -1;

    std::string advertise;
    {
        scoped_lock_t lock (_sync);
        if (!_registered) {
            return 0;
        }
        advertise = _advertise_endpoint;
    }

    discovery_t *discovery = NULL;
    {
        scoped_lock_t lock (_sync);
        discovery = _discovery;
    }
    if (!discovery) {
        errno = EFSM;
        return -1;
    }
    if (discovery->unregister_service (discovery_protocol::service_type_spot_node,
                                       _service_name.c_str (),
                                       advertise.c_str ())
        != 0)
        return -1;

    scoped_lock_t lock (_sync);
    _registered = false;
    _advertise_endpoint.clear ();
    _registration_uplink_endpoint.clear ();
    return 0;
}

int spot_node_t::attach_discovery (discovery_t *discovery_)
{
    if (!discovery_ || discovery_->service_type ()
                          != discovery_protocol::service_type_spot_node) {
        errno = EINVAL;
        return -1;
    }

    bool should_register = false;
    {
        scoped_lock_t lock (_sync);
        if (_discovery == discovery_)
            return 0;
        if (_discovery || !_manual_peer_endpoints.empty ()) {
            errno = EBUSY;
            return -1;
        }
        _discovery = discovery_;
        _discovery_service = _service_name;
        _discovery_seq = 0;
        _pending_service_updates.insert (_service_name);
        _discovery_peer_endpoints.clear ();
        should_register = !_bound_endpoint.empty ();
    }
    discovery_->add_observer (this);
    if (should_register && ensure_registered () != 0) {
        scoped_lock_t lock (_sync);
        if (_discovery == discovery_) {
            _discovery->remove_observer (this);
            _discovery = NULL;
            _discovery_service.clear ();
        }
        return -1;
    }
    refresh_existing_summaries ();

    service_control_runtime_t *runtime = _ctx->service_control_runtime ();
    if (runtime && _runtime && _runtime->task_id != 0)
        runtime->wakeup_task (_runtime->task_id);
    return 0;
}

void spot_node_t::on_service_update (const std::string &service_name_)
{
    scoped_lock_t lock (_sync);
    if (_discovery_service.empty () || service_name_ != _discovery_service)
        return;
    _pending_service_updates.insert (service_name_);
    service_control_runtime_t *runtime = _ctx->service_control_runtime ();
    if (runtime && _runtime && _runtime->task_id != 0)
        runtime->wakeup_task (_runtime->task_id);
}

void spot_node_t::on_discovery_destroyed (discovery_t *discovery_)
{
    scoped_lock_t lock (_sync);
    if (_discovery != discovery_)
        return;
    _discovery = NULL;
    _discovery_service.clear ();
    _discovery_seq = 0;
    _pending_service_updates.clear ();
    _discovery_peer_endpoints.clear ();
    _connected_peer_endpoints.clear ();
    _registered = false;
    _advertise_endpoint.clear ();
    _registration_uplink_endpoint.clear ();
}

int spot_node_t::set_tls_server (const char *cert_, const char *key_)
{
    if (!cert_ || !key_ || cert_[0] == '\0' || key_[0] == '\0') {
        errno = EINVAL;
        return -1;
    }

    scoped_lock_t lock (_sync);
    if (_server_tls_locked || !_bound_endpoint.empty ()) {
        errno = EBUSY;
        return -1;
    }
    _tls_cert = cert_;
    _tls_key = key_;
    return 0;
}

int spot_node_t::set_tls_client (const char *ca_cert_,
                                 const char *hostname_,
                                 int trust_system_)
{
    if (trust_system_ < 0) {
        errno = EINVAL;
        return -1;
    }

    scoped_lock_t lock (_sync);
    if (_mesh_client_tls_locked || _registration_tls_locked) {
        errno = EBUSY;
        return -1;
    }
    _tls_ca = ca_cert_ ? ca_cert_ : "";
    _tls_hostname = hostname_ ? hostname_ : "";
    _tls_trust_system = trust_system_;
    return 0;
}

int spot_node_t::set_send_ready_handler (zlink_send_ready_handler_fn handler_)
{
    if (!handler_) {
        errno = EINVAL;
        return -1;
    }

    spot_pub_t *pub = ensure_default_pub ();
    if (!pub)
        return -1;

    const int rc = pub->set_send_ready_handler (handler_, this);
    if (rc == 0)
        _send_ready_handler.store (handler_, std::memory_order_release);
    return rc;
}

int spot_node_t::validate_pub_option (int option_,
                                      const void *optval_,
                                      size_t optvallen_)
{
    if (!optval_ || optvallen_ == 0 || optvallen_ > sizeof (int)) {
        errno = EINVAL;
        return -1;
    }

    switch (option_) {
        case ZLINK_SPOT_PUB_OPT_SNDHWM:
        case ZLINK_SPOT_PUB_OPT_SNDTIMEO:
        case ZLINK_SPOT_PUB_OPT_LINGER:
        case ZLINK_SPOT_PUB_OPT_NODROP:
        case ZLINK_SPOT_PUB_OPT_SNDBUF:
        case ZLINK_SPOT_PUB_OPT_RCVBUF:
            return 0;
        default:
            errno = EINVAL;
            return -1;
    }
}

int spot_node_t::validate_sub_option (int option_,
                                      const void *optval_,
                                      size_t optvallen_)
{
    if (!optval_ || optvallen_ == 0 || optvallen_ > sizeof (int)) {
        errno = EINVAL;
        return -1;
    }

    switch (option_) {
        case ZLINK_SPOT_SUB_OPT_RCVHWM:
        case ZLINK_SPOT_SUB_OPT_LINGER:
        case ZLINK_SPOT_SUB_OPT_SNDBUF:
        case ZLINK_SPOT_SUB_OPT_RCVBUF:
        case ZLINK_SPOT_SUB_OPT_RCVTIMEO:
            return 0;
        default:
            errno = EINVAL;
            return -1;
    }
}

void spot_node_t::copy_option_setting (option_setting_t *dst_,
                                       const void *optval_,
                                       size_t optvallen_)
{
    if (!dst_)
        return;

    dst_->enabled = true;
    dst_->value = 0;
    dst_->size = optvallen_;
    memcpy (&dst_->value, optval_, optvallen_);
}

void spot_node_t::store_pub_option (int option_,
                                    const void *optval_,
                                    size_t optvallen_)
{
    switch (option_) {
        case ZLINK_SPOT_PUB_OPT_SNDHWM:
            copy_option_setting (&_pub_defaults.sndhwm, optval_, optvallen_);
            return;
        case ZLINK_SPOT_PUB_OPT_SNDTIMEO:
            copy_option_setting (&_pub_defaults.sndtimeo, optval_, optvallen_);
            return;
        case ZLINK_SPOT_PUB_OPT_LINGER:
            copy_option_setting (&_pub_defaults.linger, optval_, optvallen_);
            return;
        case ZLINK_SPOT_PUB_OPT_NODROP:
            copy_option_setting (&_pub_defaults.nodrop, optval_, optvallen_);
            return;
        case ZLINK_SPOT_PUB_OPT_SNDBUF:
            copy_option_setting (&_pub_defaults.sndbuf, optval_, optvallen_);
            return;
        case ZLINK_SPOT_PUB_OPT_RCVBUF:
            copy_option_setting (&_pub_defaults.rcvbuf, optval_, optvallen_);
            return;
        default:
            return;
    }
}

void spot_node_t::store_sub_option (int option_,
                                    const void *optval_,
                                    size_t optvallen_)
{
    switch (option_) {
        case ZLINK_SPOT_SUB_OPT_RCVHWM:
            copy_option_setting (&_sub_defaults.rcvhwm, optval_, optvallen_);
            return;
        case ZLINK_SPOT_SUB_OPT_LINGER:
            copy_option_setting (&_sub_defaults.linger, optval_, optvallen_);
            return;
        case ZLINK_SPOT_SUB_OPT_SNDBUF:
            copy_option_setting (&_sub_defaults.sndbuf, optval_, optvallen_);
            return;
        case ZLINK_SPOT_SUB_OPT_RCVBUF:
            copy_option_setting (&_sub_defaults.rcvbuf, optval_, optvallen_);
            return;
        case ZLINK_SPOT_SUB_OPT_RCVTIMEO:
            copy_option_setting (&_sub_defaults.rcvtimeo, optval_, optvallen_);
            return;
        default:
            return;
    }
}

int spot_node_t::set_pub_option (int option_,
                                 const void *optval_,
                                 size_t optvallen_)
{
    if (validate_pub_option (option_, optval_, optvallen_) != 0)
        return -1;

    scoped_lock_t init_lock (_default_pub_sync);
    spot_pub_t *default_pub = NULL;
    {
        scoped_lock_t lock (_sync);
        default_pub = _default_pub;
    }

    if (default_pub && default_pub->set_option (option_, optval_, optvallen_) != 0)
        return -1;

    {
        scoped_lock_t lock (_sync);
        store_pub_option (option_, optval_, optvallen_);
    }
    return 0;
}

int spot_node_t::set_sub_option (int option_,
                                 const void *optval_,
                                 size_t optvallen_)
{
    if (validate_sub_option (option_, optval_, optvallen_) != 0)
        return -1;

    scoped_lock_t init_lock (_default_sub_sync);
    spot_sub_t *default_sub = NULL;
    {
        scoped_lock_t lock (_sync);
        default_sub = _default_sub;
    }

    if (default_sub && default_sub->set_option (option_, optval_, optvallen_) != 0)
        return -1;

    {
        scoped_lock_t lock (_sync);
        store_sub_option (option_, optval_, optvallen_);
    }
    return 0;
}

int spot_node_t::apply_pub_defaults (spot_pub_t *pub_,
                                     const pub_defaults_t &defaults_)
{
    if (!pub_) {
        errno = EINVAL;
        return -1;
    }

    if (defaults_.sndhwm.enabled
        && pub_->set_option (ZLINK_SPOT_PUB_OPT_SNDHWM, &defaults_.sndhwm.value,
                             defaults_.sndhwm.size)
             != 0)
        return -1;
    if (defaults_.sndtimeo.enabled
        && pub_->set_option (ZLINK_SPOT_PUB_OPT_SNDTIMEO,
                             &defaults_.sndtimeo.value, defaults_.sndtimeo.size)
             != 0)
        return -1;
    if (defaults_.linger.enabled
        && pub_->set_option (ZLINK_SPOT_PUB_OPT_LINGER, &defaults_.linger.value,
                             defaults_.linger.size)
             != 0)
        return -1;
    if (defaults_.nodrop.enabled
        && pub_->set_option (ZLINK_SPOT_PUB_OPT_NODROP, &defaults_.nodrop.value,
                             defaults_.nodrop.size)
             != 0)
        return -1;
    if (defaults_.sndbuf.enabled
        && pub_->set_option (ZLINK_SPOT_PUB_OPT_SNDBUF, &defaults_.sndbuf.value,
                             defaults_.sndbuf.size)
             != 0)
        return -1;
    if (defaults_.rcvbuf.enabled
        && pub_->set_option (ZLINK_SPOT_PUB_OPT_RCVBUF, &defaults_.rcvbuf.value,
                             defaults_.rcvbuf.size)
             != 0)
        return -1;
    return 0;
}

int spot_node_t::apply_sub_defaults (spot_sub_t *sub_,
                                     const sub_defaults_t &defaults_)
{
    if (!sub_) {
        errno = EINVAL;
        return -1;
    }

    if (defaults_.rcvhwm.enabled
        && sub_->set_option (ZLINK_SPOT_SUB_OPT_RCVHWM, &defaults_.rcvhwm.value,
                             defaults_.rcvhwm.size)
             != 0)
        return -1;
    if (defaults_.linger.enabled
        && sub_->set_option (ZLINK_SPOT_SUB_OPT_LINGER, &defaults_.linger.value,
                             defaults_.linger.size)
             != 0)
        return -1;
    if (defaults_.sndbuf.enabled
        && sub_->set_option (ZLINK_SPOT_SUB_OPT_SNDBUF, &defaults_.sndbuf.value,
                             defaults_.sndbuf.size)
             != 0)
        return -1;
    if (defaults_.rcvbuf.enabled
        && sub_->set_option (ZLINK_SPOT_SUB_OPT_RCVBUF, &defaults_.rcvbuf.value,
                             defaults_.rcvbuf.size)
             != 0)
        return -1;
    if (defaults_.rcvtimeo.enabled
        && sub_->set_option (ZLINK_SPOT_SUB_OPT_RCVTIMEO,
                             &defaults_.rcvtimeo.value,
                             defaults_.rcvtimeo.size)
             != 0)
        return -1;
    return 0;
}

spot_pub_t *spot_node_t::create_spot_pub_with_defaults (
  const pub_defaults_t &defaults_, bool node_owned_default_)
{
    if (ensure_healthy () != 0)
        return NULL;
    uint64_t attachment_id = 0;
    socket_base_t *attachment_socket = NULL;
    if (!_runtime
        || _runtime->create_attachment (spot_attachment_pub,
                                        pub_ingress_endpoint ().c_str (),
                                        &attachment_id)
             != 0)
        return NULL;
    attachment_socket = _runtime->attachment_socket (attachment_id);
    if (!attachment_socket || wait_facade_peer (attachment_socket) != 0) {
        const int err = errno != 0 ? errno : ETIMEDOUT;
        (void) _runtime->destroy_attachment (attachment_id);
        errno = err;
        return NULL;
    }

    spot_pub_t *pub = new (std::nothrow)
      spot_pub_t (this, attachment_socket, attachment_id, node_owned_default_);
    if (!pub) {
        (void) _runtime->destroy_attachment (attachment_id);
        errno = ENOMEM;
        return NULL;
    }

    if (apply_pub_defaults (pub, defaults_) != 0) {
        const int err = errno;
        pub->abort_create ();
        delete pub;
        errno = err;
        return NULL;
    }

    bool bound = false;
    {
        scoped_lock_t lock (_sync);
        _pubs.insert (pub);
        if (node_owned_default_)
            _default_pub = pub;
        bound = !_bound_endpoint.empty ();
    }
    pub->emit_ready_event ();
    if (node_owned_default_) {
        zlink_send_ready_handler_fn handler =
          _send_ready_handler.load (std::memory_order_acquire);
        if (handler && pub->set_send_ready_handler (handler, this) != 0) {
            const int err = errno;
            remove_spot_pub (pub);
            pub->abort_create ();
            delete pub;
            errno = err;
            return NULL;
        }
    }
    if (bound)
        submit_pub_summary (pub, ZLINK_TOPOLOGY_STATE_READY, 0);
    return pub;
}

spot_sub_t *spot_node_t::create_spot_sub_with_defaults (
  const sub_defaults_t &defaults_, bool node_owned_default_)
{
    if (ensure_healthy () != 0)
        return NULL;
    uint64_t attachment_id = 0;
    socket_base_t *attachment_socket = NULL;
    if (!_runtime
        || _runtime->create_attachment (spot_attachment_sub,
                                        sub_fanout_endpoint ().c_str (),
                                        &attachment_id)
             != 0)
        return NULL;
    attachment_socket = _runtime->attachment_socket (attachment_id);
    if (!attachment_socket || wait_facade_peer (attachment_socket) != 0) {
        const int err = errno != 0 ? errno : ETIMEDOUT;
        (void) _runtime->destroy_attachment (attachment_id);
        errno = err;
        return NULL;
    }

    spot_sub_t *sub = new (std::nothrow)
      spot_sub_t (this, attachment_socket, attachment_id, node_owned_default_);
    if (!sub) {
        (void) _runtime->destroy_attachment (attachment_id);
        errno = ENOMEM;
        return NULL;
    }

    if (apply_sub_defaults (sub, defaults_) != 0) {
        const int err = errno;
        sub->abort_create ();
        delete sub;
        errno = err;
        return NULL;
    }

    {
        scoped_lock_t lock (_sync);
        _subs.insert (sub);
        if (node_owned_default_)
            _default_sub = sub;
    }
    sub->emit_ready_event ();
    submit_sub_summary (sub, ZLINK_TOPOLOGY_STATE_CONNECTING, 0);
    return sub;
}

spot_pub_t *spot_node_t::create_spot_pub ()
{
    pub_defaults_t defaults;
    scoped_lock_t init_lock (_default_pub_sync);
    {
        scoped_lock_t lock (_sync);
        defaults = _pub_defaults;
    }
    return create_spot_pub_with_defaults (defaults, false);
}

spot_sub_t *spot_node_t::create_spot_sub ()
{
    sub_defaults_t defaults;
    scoped_lock_t init_lock (_default_sub_sync);
    {
        scoped_lock_t lock (_sync);
        defaults = _sub_defaults;
    }
    return create_spot_sub_with_defaults (defaults, false);
}

spot_pub_t *spot_node_t::ensure_default_pub ()
{
    {
        scoped_lock_t lock (_sync);
        if (_default_pub)
            return _default_pub;
    }

    pub_defaults_t defaults;
    scoped_lock_t init_lock (_default_pub_sync);
    {
        scoped_lock_t lock (_sync);
        if (_default_pub)
            return _default_pub;
        defaults = _pub_defaults;
    }
    return create_spot_pub_with_defaults (defaults, true);
}

spot_sub_t *spot_node_t::ensure_default_sub ()
{
    {
        scoped_lock_t lock (_sync);
        if (_default_sub)
            return _default_sub;
    }

    sub_defaults_t defaults;
    scoped_lock_t init_lock (_default_sub_sync);
    {
        scoped_lock_t lock (_sync);
        if (_default_sub)
            return _default_sub;
        defaults = _sub_defaults;
    }
    return create_spot_sub_with_defaults (defaults, true);
}

void spot_node_t::remove_spot_pub (spot_pub_t *pub_)
{
    scoped_lock_t lock (_sync);
    if (_default_pub == pub_)
        _default_pub = NULL;
    _pubs.erase (pub_);
}

void spot_node_t::remove_spot_sub (spot_sub_t *sub_)
{
    scoped_lock_t lock (_sync);
    if (_default_sub == sub_)
        _default_sub = NULL;
    _subs.erase (sub_);
    if (sub_ && sub_->has_filters ())
        note_local_sub_filters_changed (true, false);
}

int spot_node_t::destroy_handles ()
{
    std::vector<spot_pub_t *> pubs;
    std::vector<spot_sub_t *> subs;
    int first_error = 0;
    {
        scoped_lock_t lock (_sync);
        pubs.assign (_pubs.begin (), _pubs.end ());
        subs.assign (_subs.begin (), _subs.end ());
        _default_pub = NULL;
        _default_sub = NULL;
        _pubs.clear ();
        _subs.clear ();
    }

    for (size_t i = 0; i < pubs.size (); ++i) {
        preserve_first_error (pubs[i]->destroy_from_node (), &first_error);
        delete pubs[i];
    }
    for (size_t i = 0; i < subs.size (); ++i) {
        preserve_first_error (subs[i]->destroy_from_node (), &first_error);
        delete subs[i];
    }
    if (first_error != 0) {
        errno = first_error;
        return -1;
    }
    return 0;
}

void spot_node_t::stop_data_plane_sockets ()
{
    if (_runtime)
        _runtime->stop_sockets ();
}

void spot_node_t::close_control_sockets ()
{
    if (_runtime)
        _runtime->close_control_sockets ();
}

int spot_node_t::destroy ()
{
    _lifecycle.transition_to (service_state_stopping);
    discovery_t *discovery = NULL;
    std::vector<std::string> active_peer_endpoints;
    std::string bound_endpoint;
    int first_error = 0;
    int graceful_error = 0;
    int final_error = 0;
    bool used_abortive = false;

    spot_shutdown_logf (false,
                        "step=begin node=%p service=%s state=%d tracked=%zu",
                        static_cast<void *> (this), _service_name.c_str (),
                        static_cast<int> (_lifecycle.state ()),
                        _lifecycle.owned_socket_count ());
    if (_discovery && _registered)
        (void) unregister_registered ();
    {
        scoped_lock_t lock (_sync);
        active_peer_endpoints.assign (_active_peer_endpoints.begin (),
                                      _active_peer_endpoints.end ());
        bound_endpoint = _bound_endpoint;
    }
    for (size_t i = 0; i < active_peer_endpoints.size (); ++i)
        (void) send_data_plane_command ("disconnect_peer_pub",
                                        active_peer_endpoints[i].c_str ());
    if (!bound_endpoint.empty ())
        (void) send_data_plane_command ("unbind_pub", bound_endpoint.c_str ());
    spot_shutdown_logf (false,
                        "step=peer_disconnect node=%p",
                        static_cast<void *> (this));
    if (_runtime)
        _runtime->stop.set (1);
    submit_stopped_summaries ();
    spot_shutdown_logf (false,
                        "step=summaries_stopped node=%p",
                        static_cast<void *> (this));

    service_control_runtime_t *runtime = _ctx->service_control_runtime ();
    if (runtime && _runtime && _runtime->task_id != 0)
        runtime->remove_task (_runtime->task_id);
    if (_runtime)
        _runtime->task_id = 0;
    spot_shutdown_logf (false,
                        "step=task_removed node=%p",
                        static_cast<void *> (this));

    {
        scoped_lock_t lock (_sync);
        discovery = _discovery;
        _discovery = NULL;
        _discovery_service.clear ();
        _discovery_seq = 0;
        _pending_service_updates.clear ();
        _manual_peer_endpoints.clear ();
        _active_peer_endpoints.clear ();
        _active_peer_count.store (0, std::memory_order_release);
        _connected_peer_endpoints.clear ();
        _discovery_peer_endpoints.clear ();
        _registered = false;
        _advertise_endpoint.clear ();
        _registration_uplink_endpoint.clear ();
    }

    if (discovery)
        discovery->remove_observer (this);
    spot_shutdown_logf (false,
                        "step=observer_removed node=%p",
                        static_cast<void *> (this));

    if (_runtime)
        preserve_first_error (_runtime->stop_and_join (), &first_error);
    spot_shutdown_logf (false,
                        "step=data_plane_stopped node=%p error=%d",
                        static_cast<void *> (this), first_error);
    preserve_first_error (destroy_handles (), &first_error);
    spot_shutdown_logf (false,
                        "step=handles_destroyed node=%p error=%d tracked=%zu",
                        static_cast<void *> (this), first_error,
                        _lifecycle.owned_socket_count ());
    preserve_first_error (wait_owned_socket_removals (10000), &first_error);
    graceful_error = first_error;
    final_error = graceful_error;

    if (_runtime
        && (first_error != 0 || _runtime->live_socket_slot_count () != 0
            || _runtime->attachment_count () != 0)) {
        const int abort_reason = first_error != 0 ? first_error : ETIMEDOUT;
        const size_t live_slots = _runtime->live_socket_slot_count ();
        const size_t live_attachments = _runtime->attachment_count ();
        const size_t tracked_sockets = _lifecycle.owned_socket_count ();
        used_abortive = true;
        spot_shutdown_logf (true,
                            "service=spot node=%p shutdown=abortive reason=%d live_slots=%zu attachments=%zu tracked=%zu",
                            static_cast<void *> (this), abort_reason,
                            live_slots, live_attachments, tracked_sockets);
        _runtime->abortive_stop ();
        preserve_first_error (_lifecycle.force_wait_remaining (5000),
                              &final_error);
        preserve_first_error (wait_owned_socket_removals (5000), &final_error);
    }

    if (!used_abortive)
        spot_shutdown_logf (false,
                            "service=spot node=%p shutdown=graceful",
                            static_cast<void *> (this));
    _lifecycle.transition_to (service_state_stopped);
    spot_shutdown_logf (false,
                        "step=complete node=%p state=%d error=%d tracked=%zu",
                        static_cast<void *> (this),
                        static_cast<int> (_lifecycle.state ()),
                        final_error, _lifecycle.owned_socket_count ());
    if (final_error != 0) {
        errno = final_error;
        return -1;
    }
    return 0;
}
}
