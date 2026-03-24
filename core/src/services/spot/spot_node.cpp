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
#include "utils/sleep.hpp"

#include <algorithm>
#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <string.h>
#include <vector>

namespace zlink
{
static const uint32_t spot_node_tag_value = 0x1e6700d9;
static const size_t spot_sub_queue_hwm_default = 64;
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

static bool spot_shutdown_debug_enabled ()
{
    return std::getenv ("ZLINK_DEBUG_SPOT_SHUTDOWN") != NULL;
}

static void spot_shutdown_logf (bool always_, const char *fmt_, ...)
{
    LIBZLINK_UNUSED (always_);
    LIBZLINK_UNUSED (fmt_);
    return;
#if 0
    if (!std::getenv ("ZLINK_SPOT_SHUTDOWN_LOG")) {
        LIBZLINK_UNUSED (always_);
        return;
    }
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
#endif
}

static void spot_ready_ack_debugf (const char *fmt_, ...)
{
    if (!std::getenv ("ZLINK_DEBUG_SPOT_READY_ACK"))
        return;

    va_list args;
    va_start (args, fmt_);
    std::fprintf (stderr, "[spot-ready-ack] ");
    std::vfprintf (stderr, fmt_, args);
    std::fprintf (stderr, "\n");
    std::fflush (stderr);
    FILE *fp = std::fopen ("/tmp/zlink_spot_ready_ack.log", "a");
    if (fp) {
        va_list file_args;
        va_start (file_args, fmt_);
        std::vfprintf (fp, fmt_, file_args);
        std::fprintf (fp, "\n");
        va_end (file_args);
        std::fclose (fp);
    }
    va_end (args);
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

static void snapshot_connected_mesh_peer_endpoints (const spot_runtime_t *runtime_,
                                                    std::set<std::string> *out_)
{
    if (!out_)
        return;
    out_->clear ();
    if (!runtime_)
        return;
    scoped_lock_t lock (const_cast<mutex_t &> (runtime_->connected_peer_sync));
    *out_ = runtime_->connected_peer_endpoints;
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
        if (it->compare (0, 5, "ws://") == 0)
            return 50;
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

spot_node_t::spot_node_t (ctx_t *ctx_, const char *service_name_) :
    _ctx (ctx_),
    _tag (spot_node_tag_value),
    _lifecycle (ctx_),
    _runtime (NULL),
    _connected_peer_version_seen (0),
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
    _last_summary_error (0),
    _summary_last_changed_ms (0),
    _tls_trust_system (0),
    _server_tls_locked (false),
    _mesh_client_tls_locked (false),
    _registration_tls_locked (false),
    _send_ready_handler (NULL),
    _send_ready_handler_userdata (NULL),
    _local_filtered_sub_count (0),
    _active_peer_count (0),
    _default_pub (NULL),
    _default_sub (NULL),
    _internal_receiver (NULL),
    _default_pub_fast (NULL),
    _default_sub_fast (NULL),
    _internal_receiver_fast (NULL)
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
    if (ensure_control_task_running () != 0) {
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
    if (socket_->setsockopt (ZLINK_INTERNAL_OPT_TLS_CERT, cert_.data (), cert_.size ()) != 0
        || socket_->setsockopt (ZLINK_INTERNAL_OPT_TLS_KEY, key_.data (), key_.size ()) != 0)
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
        && socket_->setsockopt (ZLINK_INTERNAL_OPT_TLS_CA, ca_cert_.data (), ca_cert_.size ())
             != 0)
        return -1;
    if (!hostname_.empty ()
        && socket_->setsockopt (ZLINK_INTERNAL_OPT_TLS_HOSTNAME, hostname_.data (),
                                hostname_.size ())
             != 0)
        return -1;
    if (socket_->setsockopt (ZLINK_INTERNAL_OPT_TLS_TRUST_SYSTEM, &trust_system_,
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

std::string spot_node_t::public_endpoint () const
{
    scoped_lock_t lock (const_cast<mutex_t &> (_sync));
    return _advertise_endpoint.empty () ? _bound_endpoint : _advertise_endpoint;
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

bool spot_node_t::is_shutting_down () const
{
    const service_lifecycle_state_t state = _lifecycle.state ();
    if (state == service_state_stopping || state == service_state_stopped)
        return true;

    scoped_lock_t lock (const_cast<mutex_t &> (_sync));
    return _runtime && _runtime->stop.get () != 0;
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
        _last_summary_error = _runtime->fault_errno;
        _summary_last_changed_ms = zlink::clock_t ().now_ms ();
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
        if (socket_ && socket_->monitor_has_attached_pipes ())
            return 0;
        zlink::sleep_ms (1);
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

int spot_node_t::destroy_attachment (uint64_t attachment_id_)
{
    if (!_runtime)
        return 0;
    return _runtime->destroy_attachment (attachment_id_);
}

int spot_node_t::destroy_attachment_async (uint64_t attachment_id_)
{
    if (!_runtime)
        return 0;
    return _runtime->destroy_attachment_async (attachment_id_);
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

}
