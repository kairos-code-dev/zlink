/* SPDX-License-Identifier: MPL-2.0 */

#include "utils/precompiled.hpp"

#include "services/spot/common/spot_control_protocol.hpp"
#include "services/spot/data_plane/spot_data_plane.hpp"
#include "services/spot/node/spot_node.hpp"
#include "services/spot/pubsub/spot_pub.hpp"
#include "services/spot/runtime/spot_runtime.hpp"
#include "services/spot/pubsub/spot_sub.hpp"
#include "services/control/service_control_runtime.hpp"
#include "sockets/common/socket_base.hpp"
#include "utils/clock.hpp"
#include "utils/err.hpp"
#include "utils/routing_id.hpp"
#include "utils/sleep.hpp"

#include <string.h>
#include <vector>

namespace zlink
{
static const uint32_t spot_node_tag_value = 0x1e6700d9;
static const size_t spot_sub_queue_hwm_default = 16;
static const int ctrl_timeout_ms = 2000;
static const char spot_ready_probe_prefix[] = "__zlink.ready__/";

static int recv_ascii_command (socket_base_t *socket_, std::vector<std::string> *frames_)
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
        frames_->push_back (std::string (static_cast<const char *> (frame.data ()), frame.size ()));
        const bool more = (frame.flags () & msg_t::more) != 0;
        frame.close ();
        if (!more)
            break;
    }
    return frames_->empty () ? -1 : 0;
}

spot_node_t::spot_node_t (ctx_t *ctx_, zlink_spot_node_mode_t mode_) :
    _ctx (ctx_),
    _tag (spot_node_tag_value),
    _lifecycle (ctx_),
    _spot_node_mode (mode_),
    _runtime (NULL),
    _pub_routing_id_set (false),
    _sub_routing_id_set (false),
    _send_ready_handler (NULL),
    _send_ready_handler_userdata (NULL),
    _send_ready_signal_armed (false)
{
    generate_random_uuid_routing_id (&_node_routing_id);
    uint64_t actor_generation_seed = 0;
    memcpy (&actor_generation_seed, _node_routing_id.data, sizeof (actor_generation_seed));
    _actor_state.next_generation = actor_generation_seed == 0 ? 1 : actor_generation_seed;
    memset (&_pub_routing_id, 0, sizeof (_pub_routing_id));
    memset (&_sub_routing_id, 0, sizeof (_sub_routing_id));

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
    if (_runtime && _runtime->execution.data_plane_running)
        (void) _runtime->abortive_stop ();
    _tag = 0xdeadbeef;
    delete _runtime;
    _runtime = NULL;
}

spot_node_t::service_attachment_state_t &spot_node_t::service_attachments ()
{
    return _service_attachments.state ();
}

const spot_node_t::service_attachment_state_t &spot_node_t::service_attachments () const
{
    return _service_attachments.state ();
}

bool spot_node_t::check_tag () const
{
    return _tag == spot_node_tag_value;
}

bool spot_node_t::pubsub_enabled () const
{
    return _spot_node_mode == ZLINK_SPOT_NODE_MODE_PUBSUB
           || _spot_node_mode == ZLINK_SPOT_NODE_MODE_ALL;
}

bool spot_node_t::routed_enabled () const
{
    return _spot_node_mode == ZLINK_SPOT_NODE_MODE_ROUTED
           || _spot_node_mode == ZLINK_SPOT_NODE_MODE_ALL;
}

int spot_node_t::set_node_routing_id (const void *data_, size_t size_)
{
    if (!data_ || size_ == 0 || size_ > sizeof (_node_routing_id.data)) {
        errno = EINVAL;
        return -1;
    }
    scoped_lock_t lock (_sync);
    memset (&_node_routing_id, 0, sizeof (_node_routing_id));
    _node_routing_id.size = static_cast<uint8_t> (size_);
    memcpy (_node_routing_id.data, data_, size_);
    return 0;
}

int spot_node_t::node_routing_id (zlink_routing_id_t *out_) const
{
    if (!out_) {
        errno = EINVAL;
        return -1;
    }
    scoped_lock_t lock (_sync);
    *out_ = _node_routing_id;
    return 0;
}

int spot_node_t::set_pub_routing_id (const void *data_, size_t size_)
{
    if (!data_ || size_ == 0 || size_ > sizeof (_pub_routing_id.data)) {
        errno = EINVAL;
        return -1;
    }

    zlink_routing_id_t next;
    memset (&next, 0, sizeof (next));
    next.size = static_cast<uint8_t> (size_);
    memcpy (next.data, data_, size_);

    {
        scoped_lock_t lock (_sync);
        _pub_routing_id = next;
        _pub_routing_id_set = true;
    }

    if (_runtime) {
        if (_runtime->mesh_pub
            && _runtime->mesh_pub->setsockopt (ZLINK_INTERNAL_OPT_ROUTING_ID, next.data, next.size)
                 != 0)
            return -1;
        if (_runtime->local_fanout_xpub
            && _runtime->local_fanout_xpub->setsockopt (ZLINK_INTERNAL_OPT_ROUTING_ID, next.data,
                                                        next.size)
                 != 0)
            return -1;
    }
    return 0;
}

int spot_node_t::set_sub_routing_id (const void *data_, size_t size_)
{
    if (!data_ || size_ == 0 || size_ > sizeof (_sub_routing_id.data)) {
        errno = EINVAL;
        return -1;
    }

    zlink_routing_id_t next;
    memset (&next, 0, sizeof (next));
    next.size = static_cast<uint8_t> (size_);
    memcpy (next.data, data_, size_);

    {
        scoped_lock_t lock (_sync);
        _sub_routing_id = next;
        _sub_routing_id_set = true;
    }

    if (_runtime) {
        if (_runtime->mesh_xsub
            && _runtime->mesh_xsub->setsockopt (ZLINK_INTERNAL_OPT_ROUTING_ID, next.data, next.size)
                 != 0)
            return -1;
        if (_runtime->pub_ingress_sub
            && _runtime->pub_ingress_sub->setsockopt (ZLINK_INTERNAL_OPT_ROUTING_ID, next.data,
                                                      next.size)
                 != 0)
            return -1;
    }
    return 0;
}

bool spot_node_t::pub_routing_id (zlink_routing_id_t *out_) const
{
    if (!out_)
        return false;

    scoped_lock_t lock (_sync);
    if (!_pub_routing_id_set)
        return false;
    *out_ = _pub_routing_id;
    return true;
}

bool spot_node_t::sub_routing_id (zlink_routing_id_t *out_) const
{
    if (!out_)
        return false;

    scoped_lock_t lock (_sync);
    if (!_sub_routing_id_set)
        return false;
    *out_ = _sub_routing_id;
    return true;
}

bool spot_node_t::peer_has_positive_weight (const zlink_routing_id_t *peer_rid_) const
{
    if (!peer_rid_ || peer_rid_->size == 0)
        return true;

    return peer_has_positive_weight_key (zlink::routing_id_key (peer_rid_));
}

bool spot_node_t::peer_has_positive_weight_key (const std::string &peer_rid_key_) const
{
    if (peer_rid_key_.empty ())
        return true;

    scoped_lock_t lock (_sync);
    std::map<std::string, uint32_t>::const_iterator it =
      _peer_state.peer_weight_by_rid.find (peer_rid_key_);
    return it == _peer_state.peer_weight_by_rid.end () || it->second > 0;
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
        && socket_->setsockopt (ZLINK_INTERNAL_OPT_TLS_CA, ca_cert_.data (), ca_cert_.size ()) != 0)
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
    if (endpoint_.find ("tcp://*:") == 0 || endpoint_.find ("tcp://0.0.0.0:") == 0
        || endpoint_.find ("tcp://[::]:") == 0)
        return false;
    return true;
}

bool spot_node_t::recv_ctrl_reply (socket_base_t *socket_, int *out_errno_)
{
    std::vector<std::string> frames;
    if (recv_ascii_command (socket_, &frames) != 0)
        return false;
    if (!frames.empty ()
        && spot_control_protocol::command_is (frames[0], spot_control_protocol::reply_ok)) {
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

const std::string &spot_node_t::sub_fanout_endpoint () const
{
    return _runtime->sub_fanout_endpoint;
}

std::string spot_node_t::public_endpoint () const
{
    scoped_lock_t lock (_sync);
    return _endpoint_state.bound_endpoint;
}

bool spot_node_t::has_active_peers () const
{
    return _endpoint_state.active_peer_count.load (std::memory_order_acquire) != 0;
}

bool spot_node_t::has_local_filtered_subs () const
{
    return _endpoint_state.local_filtered_sub_count.load (std::memory_order_acquire) != 0;
}

void spot_node_t::note_local_sub_filters_changed (bool had_filters_, bool has_filters_)
{
    if (had_filters_ == has_filters_)
        return;

    if (has_filters_) {
        _endpoint_state.local_filtered_sub_count.fetch_add (1, std::memory_order_acq_rel);
        return;
    }

    uint32_t current = _endpoint_state.local_filtered_sub_count.load (std::memory_order_acquire);
    while (current != 0
           && !_endpoint_state.local_filtered_sub_count.compare_exchange_weak (
             current, current - 1, std::memory_order_acq_rel, std::memory_order_acquire)) {
    }
}

std::string spot_node_t::first_active_peer_endpoint () const
{
    scoped_lock_t lock (_sync);
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
    scoped_lock_t lock (_sync);
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

    scoped_lock_t lock (_sync);
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
        _summary_state.last_summary_error = _runtime->fault_errno;
        _summary_state.summary_last_changed_ms = zlink::clock_t ().now_ms ();
        pubs.assign (_handle_state.pubs.begin (), _handle_state.pubs.end ());
        subs.assign (_handle_state.subs.begin (), _handle_state.subs.end ());
    }

    for (size_t i = 0; i < pubs.size (); ++i)
        submit_pub_summary (pubs[i], ZLINK_TOPOLOGY_STATE_ERROR, _runtime->fault_errno);
    for (size_t i = 0; i < subs.size (); ++i)
        submit_sub_summary (subs[i], ZLINK_TOPOLOGY_STATE_LOST, _runtime->fault_errno);
}

int spot_node_t::send_data_plane_command (const char *verb_, const char *arg_) const
{
    if (!_runtime) {
        errno = EFAULT;
        return -1;
    }
    return _runtime->send_command (verb_, arg_);
}

int spot_node_t::send_data_plane_command (const char *verb_,
                                          const std::vector<std::string> &args_) const
{
    if (!_runtime) {
        errno = EFAULT;
        return -1;
    }
    return _runtime->send_command (verb_, args_);
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

void spot_node_t::mark_socket_detached_close (const socket_base_t *socket_)
{
    _lifecycle.mark_socket_detached_close (socket_);
}

bool spot_node_t::owns_socket (const socket_base_t *socket_) const
{
    return _lifecycle.owns_socket (socket_);
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

    scoped_lock_t lock (_sync);
    if (_endpoint_state.bound_endpoint.empty ()) {
        errno = EFSM;
        return -1;
    }
    *out_ = _endpoint_state.bound_endpoint;
    if (!validate_public_endpoint (*out_)) {
        errno = EINVAL;
        return -1;
    }
    return 0;
}

}
