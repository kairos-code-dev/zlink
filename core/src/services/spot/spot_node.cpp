/* SPDX-License-Identifier: MPL-2.0 */

#include "precompiled.hpp"

#include "services/spot/spot_data_plane.hpp"
#include "services/spot/spot_node.hpp"
#include "services/spot/spot_pub.hpp"
#include "services/spot/spot_runtime.hpp"
#include "services/spot/spot_sub.hpp"

#include "services/control/service_control_runtime.hpp"
#include "services/discovery/discovery_owned_service.hpp"
#include "services/discovery/discovery_protocol.hpp"
#include "services/discovery/routing_id_utils.hpp"
#include "sockets/socket_base.hpp"
#include "utils/clock.hpp"
#include "utils/err.hpp"
#include "utils/random.hpp"
#include "utils/sleep.hpp"

#include <string.h>
#include <vector>

namespace zlink
{
static const uint32_t spot_node_tag_value = 0x1e6700d9;
static const size_t spot_sub_queue_hwm_default = 16;
static const int ctrl_timeout_ms = 2000;
static const char spot_ready_probe_prefix[] = "__zlink.ready__/";

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

spot_node_t::spot_node_t (ctx_t *ctx_) :
    _ctx (ctx_),
    _tag (spot_node_tag_value),
    _lifecycle (ctx_),
    _runtime (NULL),
    _admission_state (ZLINK_ADMISSION_SERVING),
    _bound_endpoint (_endpoint_state.bound_endpoint),
    _subject_last_changed_ms (_summary_state.subject_last_changed_ms),
    _last_summary_error (_summary_state.last_summary_error),
    _summary_last_changed_ms (_summary_state.summary_last_changed_ms),
    _discovery (_discovery_state.discovery),
    _discovery_service (_discovery_state.discovery_service),
    _discovery_seq (_discovery_state.discovery_seq),
    _pending_service_updates (_discovery_state.pending_service_updates),
    _registered (_discovery_state.registered),
    _advertise_endpoint (_discovery_state.advertise_endpoint),
    _registration_uplink_endpoint (_discovery_state.registration_uplink_endpoint),
    _tls_cert (_tls_state.tls_cert),
    _tls_key (_tls_state.tls_key),
    _tls_ca (_tls_state.tls_ca),
    _tls_hostname (_tls_state.tls_hostname),
    _tls_trust_system (_tls_state.tls_trust_system),
    _server_tls_locked (_tls_state.server_tls_locked),
    _mesh_client_tls_locked (_tls_state.mesh_client_tls_locked),
    _registration_tls_locked (_tls_state.registration_tls_locked),
    _send_ready_handler (NULL),
    _send_ready_handler_userdata (NULL),
    _local_pub_ingress_rcvhwm_cfg (_endpoint_state.local_pub_ingress_rcvhwm_cfg),
    _local_fanout_sndhwm_cfg (_endpoint_state.local_fanout_sndhwm_cfg),
    _local_pub_ingress_rcvhwm_default (
      _endpoint_state.local_pub_ingress_rcvhwm_default),
    _local_fanout_sndhwm_default (_endpoint_state.local_fanout_sndhwm_default),
    _local_filtered_sub_count (_endpoint_state.local_filtered_sub_count),
    _active_peer_count (_endpoint_state.active_peer_count),
    _handle_defaults (_handle_state.handle_defaults),
    _pubs (_handle_state.pubs),
    _subs (_handle_state.subs),
    _facades (_handle_state.facades),
    _service_attachments (_service_attachment_state.attachments),
    _service_attachment_socket_index (_service_attachment_state.socket_index),
    _service_monitors (_service_attachment_state.monitors),
    _service_discoveries (_service_attachment_state.discoveries),
    _channel_dealer_discoveries (
      _service_attachment_state.channel_dealer_discoveries),
    _pub_ingress (_service_attachment_state.pub_ingress)
{
    _lifecycle.transition_to (service_state_starting);

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

int spot_node_t::set_admission_state (zlink_admission_state_t state_)
{
    if (state_ != ZLINK_ADMISSION_SERVING && state_ != ZLINK_ADMISSION_DRAINING) {
        errno = EINVAL;
        return -1;
    }

    service_public_api_scope_t admission (_public_api);
    if (!admission.acquired ())
        return -1;
    if (ensure_healthy () != 0)
        return -1;

    discovery_t *discovery = NULL;
    std::string advertise;
    bool registered = false;
    {
        scoped_lock_t lock (_sync);
        if (_admission_state == state_)
            return 0;
        _admission_state = state_;
        discovery = _discovery;
        advertise = _advertise_endpoint;
        registered = _registered;
    }

    if (registered && discovery && !advertise.empty ()) {
        if (discovery_owned_service::update_attributes (
              discovery, discovery_protocol::service_type_spot_node,
              advertise.c_str (), 0, state_)
            != 0) {
            return -1;
        }
    }

    return 0;
}

int spot_node_t::get_admission_state (
  zlink_admission_state_t *state_out_) const
{
    if (!state_out_) {
        errno = EFAULT;
        return -1;
    }

    service_public_api_scope_t admission (
      const_cast<service_public_api_guard_t &> (_public_api));
    if (!admission.acquired ())
        return -1;
    if (ensure_healthy () != 0)
        return -1;

    scoped_lock_t lock (const_cast<mutex_t &> (_sync));
    *state_out_ = _admission_state;
    return 0;
}

bool spot_node_t::peer_is_admitted (const zlink_routing_id_t *peer_rid_) const
{
    if (!peer_rid_ || peer_rid_->size == 0)
        return true;

    const std::string key (
      reinterpret_cast<const char *> (peer_rid_->data), peer_rid_->size);
    scoped_lock_t lock (const_cast<mutex_t &> (_sync));
    std::map<std::string, zlink_admission_state_t>::const_iterator it =
      _peer_state.peer_admission_by_rid.find (key);
    return it == _peer_state.peer_admission_by_rid.end ()
           || it->second == ZLINK_ADMISSION_SERVING;
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
    if (_peer_state.active_endpoints.empty ())
        return std::string ();
    return *_peer_state.active_endpoints.begin ();
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
