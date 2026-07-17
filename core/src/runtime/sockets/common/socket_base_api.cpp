/* SPDX-License-Identifier: MPL-2.0 */

#include "utils/precompiled.hpp"

#include "core/ctx.hpp"
#include "sockets/common/socket_base.hpp"
#include "core/mailbox.hpp"
#include "core/msg.hpp"
#include "core/options.hpp"
#include "utils/err.hpp"

void zlink::socket_base_t::finish_close_handoff (int handoff_timeout_ms_)
{
    lifecycle_coordinator ().complete_deferred_close_handoff (static_cast<mailbox_t *> (_mailbox),
                                                              handoff_timeout_ms_);

    _tag = 0xdeadbeef;
    send_reap (this);
}

int zlink::socket_base_t::get_peer_state (const void *routing_id_, size_t routing_id_size_) const
{
    LIBZLINK_UNUSED (routing_id_);
    LIBZLINK_UNUSED (routing_id_size_);

    errno = ENOTSUP;
    return -1;
}

int zlink::socket_base_t::xterm_peer_rid (const zlink_routing_id_t *peer_rid_)
{
    if (!peer_rid_ || peer_rid_->size == 0) {
        errno = EINVAL;
        return -1;
    }

    std::vector<pipe_t *> pipes;
    snapshot_attached_pipes (&pipes);

    pipe_t *match = NULL;
    for (size_t i = 0; i < pipes.size (); ++i) {
        pipe_t *pipe = pipes[i];
        if (!pipe)
            continue;

        const blob_t &routing_id = pipe->get_routing_id ();
        bool matches = routing_id.size () == peer_rid_->size && routing_id.size () > 0
                       && memcmp (routing_id.data (), peer_rid_->data, peer_rid_->size) == 0;
        if (!matches && pipe->get_peer ()) {
            const blob_t &peer_routing_id = pipe->get_peer ()->get_routing_id ();
            matches = peer_routing_id.size () == peer_rid_->size && peer_routing_id.size () > 0
                      && memcmp (peer_routing_id.data (), peer_rid_->data, peer_rid_->size) == 0;
        }
        if (!matches)
            continue;
        if (match && match != pipe) {
            errno = EADDRINUSE;
            return -1;
        }
        match = pipe;
    }

    if (!match) {
        errno = ENOENT;
        return -1;
    }

    match->terminate (false);
    return 0;
}

void zlink::socket_base_t::attach_pipe (pipe_t *pipe_,
                                        bool subscribe_to_all_,
                                        bool locally_initiated_)
{
    pipe_->set_event_sink (this);
    {
        scoped_lock_t lock (monitor_runtime ().sync);
        endpoint_runtime ().attach_pipe (pipe_);
    }

    xattach_pipe (pipe_, subscribe_to_all_, locally_initiated_);
    if (get_ctx ())
        get_ctx ()->schedule_auto_hwm_recalculate ();

    if (is_terminating ()) {
        if (_term_pipes.insert (pipe_).second) {
            register_term_acks (1);
            ++_term_pipe_acks_registered;
            pipe_->terminate (false);
        }
    }
}

int zlink::socket_base_t::setsockopt (int option_, const void *optval_, size_t optvallen_)
{
    socket_lifecycle_coordinator_t &lifecycle = lifecycle_coordinator ();
    socket_public_api_scope_t admission (lifecycle);
    if (!admission.acquired ())
        return -1;

    if (unlikely (_ctx_terminated)) {
        errno = ETERM;
        return -1;
    }

    int rc = 0;
    {
        socket_public_api_lock_scope_t guard (lifecycle);
        rc = xsetsockopt (option_, optval_, optvallen_);
    }
    if (rc == 0 || errno != EINVAL) {
        return rc;
    }

    {
        socket_public_api_lock_scope_t guard (lifecycle);
        rc = options.setsockopt (option_, optval_, optvallen_);
        if (rc == 0) {
            if (option_ == ZLINK_INTERNAL_OPT_SNDHWM)
                _manual_sndhwm = true;
            else if (option_ == ZLINK_INTERNAL_OPT_RCVHWM)
                _manual_rcvhwm = true;
            else if (option_ == ZLINK_INTERNAL_OPT_SNDBUF)
                _manual_sndbuf = true;
            else if (option_ == ZLINK_INTERNAL_OPT_RCVBUF)
                _manual_rcvbuf = true;
            else if (option_ == ZLINK_INTERNAL_OPT_AUTO_HWM_MSG_UNIT_BYTES)
                _auto_hwm_msg_unit_override = options.auto_hwm_msg_unit_bytes > 0;

            if (option_ == ZLINK_INTERNAL_OPT_AUTO_HWM_MSG_UNIT_BYTES
                || option_ == ZLINK_INTERNAL_OPT_SNDBUF || option_ == ZLINK_INTERNAL_OPT_RCVBUF) {
                _auto_hwm_last_recalc_reason = ZLINK_AUTO_HWM_RECALC_REASON_REFRESH;
                refresh_auto_hwm_policy ();
            }

            if (option_ == ZLINK_INTERNAL_OPT_PEER_WEIGHT) {
                _local_peer_weight = static_cast<uint32_t> (options.peer_weight);
                xlocal_peer_weight_changed ();
            }
        }
        update_pipe_options (option_);
    }
    return rc;
}

int zlink::socket_base_t::getsockopt (int option_, void *optval_, size_t *optvallen_)
{
    socket_lifecycle_coordinator_t &lifecycle = lifecycle_coordinator ();
    socket_public_api_scope_t admission (lifecycle);
    if (!admission.acquired ())
        return -1;

    if (unlikely (_ctx_terminated)) {
        errno = ETERM;
        return -1;
    }

    int rc = 0;
    {
        socket_public_api_lock_scope_t guard (lifecycle);
        rc = xgetsockopt (option_, optval_, optvallen_);
    }
    if (rc == 0 || errno != EINVAL) {
        return rc;
    }

    if (option_ == ZLINK_INTERNAL_OPT_FD) {
        rc =
          do_getsockopt<fd_t> (optval_, optvallen_, static_cast<mailbox_t *> (_mailbox)->get_fd ());
        return rc;
    }

    if (option_ == ZLINK_INTERNAL_OPT_EVENTS) {
        {
            socket_public_api_lock_scope_t guard (lifecycle);
            const int events_rc = process_commands (0, false);
            if (events_rc != 0 && (errno == EINTR || errno == ETERM))
                return -1;

            errno_assert (events_rc == 0);
            return do_getsockopt<int> (optval_, optvallen_, has_out () ? ZLINK_POLLOUT : 0);
        }
    }

    if (option_ == ZLINK_INTERNAL_OPT_LAST_ENDPOINT) {
        socket_public_api_lock_scope_t guard (lifecycle);
        return do_getsockopt (optval_, optvallen_, endpoint_runtime ().last_endpoint_uri ());
    }

    {
        socket_public_api_lock_scope_t guard (lifecycle);
        rc = options.getsockopt (option_, optval_, optvallen_);
    }
    return rc;
}

int zlink::socket_base_t::get_events (int events_, uint32_t *out_)
{
    socket_lifecycle_coordinator_t &lifecycle = lifecycle_coordinator ();
    socket_public_api_scope_t admission (lifecycle);
    if (!admission.acquired ())
        return -1;

    if (!out_) {
        errno = EINVAL;
        return -1;
    }

    {
        socket_public_api_lock_scope_t guard (lifecycle);
        const int rc = process_commands (0, false);
        if (rc != 0 && (errno == EINTR || errno == ETERM))
            return -1;
        errno_assert (rc == 0);

        uint32_t events = 0;
        if ((events_ & ZLINK_POLLOUT) && has_out ())
            events |= ZLINK_POLLOUT;

        *out_ = events;
    }
    return 0;
}

int zlink::socket_base_t::get_events_internal (int events_, uint32_t *out_)
{
    if (!out_) {
        errno = EINVAL;
        return -1;
    }

    const int rc = process_commands (0, false);
    if (rc != 0 && (errno == EINTR || errno == ETERM))
        return -1;
    errno_assert (rc == 0);

    uint32_t events = 0;
    if ((events_ & ZLINK_POLLIN) && has_in ())
        events |= ZLINK_POLLIN;
    if ((events_ & ZLINK_POLLOUT) && has_out ())
        events |= ZLINK_POLLOUT;

    *out_ = events;
    return 0;
}

int zlink::socket_base_t::join (const char *group_)
{
    return xjoin (group_);
}

int zlink::socket_base_t::leave (const char *group_)
{
    return xleave (group_);
}

std::recursive_mutex *zlink::socket_base_t::api_sync_mutex ()
{
    return NULL;
}

int zlink::socket_base_t::socket_type () const
{
    return options.type;
}

bool zlink::socket_base_t::has_in ()
{
    return xhas_in ();
}

bool zlink::socket_base_t::has_out ()
{
    const zlink::socket_dispatch_bridge_t &dispatch = dispatch_runtime ();
    if (!dispatch.send_recovery_pending ())
        return false;
    return dispatch.send_recovery_ready ();
}

bool zlink::socket_base_t::transport_has_out ()
{
    return xhas_out ();
}

void zlink::socket_base_t::read_activated (pipe_t *pipe_)
{
    xread_activated (pipe_);
}

void zlink::socket_base_t::write_activated (pipe_t *pipe_)
{
    xwrite_activated (pipe_);
    if (dispatch_runtime ().send_recovery_pending ()
        && !dispatch_runtime ().send_recovery_ready ()) {
        dispatch_runtime ().mark_send_recovery_ready ();
        static_cast<mailbox_t *> (_mailbox)->signal ();
    }
    notify_send_ready_if_armed ();
}

void zlink::socket_base_t::hiccuped (pipe_t *pipe_)
{
    if (options.immediate == 1)
        pipe_->terminate (false);
    else
        xhiccuped (pipe_);
}

void zlink::socket_base_t::pipe_peer_terminated (pipe_t *pipe_)
{
    LIBZLINK_UNUSED (pipe_);
}

void zlink::socket_base_t::pipe_terminated (pipe_t *pipe_)
{
    const bool term_pipe_ack_expected = is_terminating () && _term_pipes.erase (pipe_) != 0;

    endpoint_uri_pair_t endpoint_pair;
    std::vector<unsigned char> routing_id_copy;
    if (pipe_) {
        endpoint_pair = pipe_->get_endpoint_pair ();
        const blob_t &routing_id = pipe_->get_routing_id ();
        if (routing_id.size () > 0)
            routing_id_copy.assign (routing_id.data (), routing_id.data () + routing_id.size ());
    }
    const unsigned char *routing_id_data = routing_id_copy.empty () ? NULL : &routing_id_copy[0];
    const size_t routing_id_size = routing_id_copy.size ();

    xpipe_terminated (pipe_);
    endpoint_runtime ().inprocs.erase_pipe (pipe_);

    uint32_t ready_count = 0;
    bool ready_changed = false;
    {
        scoped_lock_t lock (monitor_runtime ().sync);
        endpoint_runtime ().detach_pipe (pipe_);
        ready_changed = monitor_runtime ().erase_ready_connection (endpoint_pair, routing_id_data,
                                                                   routing_id_size, &ready_count);
    }
    if (ready_changed) {
        uint64_t values[1] = {ready_count};
        event (endpoint_pair, routing_id_data, routing_id_size, values, 1,
               ZLINK_EVENT_CONNECTION_READY);
    }

    const std::string &identifier = pipe_->get_endpoint_pair ().identifier ();
    if (!identifier.empty ()) {
        std::pair<endpoints_t::iterator, endpoints_t::iterator> range;
        range = endpoint_runtime ().endpoints.equal_range (identifier);

        for (endpoints_t::iterator it = range.first; it != range.second; ++it) {
            if (it->second.pipe == pipe_) {
                it->second.pipe = NULL;
                break;
            }
        }
    }

    if (term_pipe_ack_expected) {
        ++_term_pipe_acks_received;
        unregister_term_ack ();
    }

    if (get_ctx ())
        get_ctx ()->schedule_auto_hwm_recalculate ();
}

int zlink::socket_base_t::socket_id () const
{
    return options.socket_id;
}

bool zlink::socket_base_t::is_ctx_terminated () const
{
    return _ctx_terminated.load (std::memory_order_acquire);
}
