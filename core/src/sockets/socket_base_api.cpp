/* SPDX-License-Identifier: MPL-2.0 */

#include "utils/precompiled.hpp"

#include "sockets/socket_base.hpp"
#include "core/mailbox.hpp"
#include "core/msg.hpp"
#include "core/options.hpp"
#include "utils/err.hpp"
#include "utils/likely.hpp"

namespace
{
std::string make_monitor_ready_key (
  const zlink::endpoint_uri_pair_t &endpoint_uri_pair_,
  const unsigned char *routing_id_,
  size_t routing_id_size_)
{
    std::string key = endpoint_uri_pair_.identifier ();
    if (key.empty ())
        key = endpoint_uri_pair_.remote;
    key.push_back ('\0');
    if (routing_id_ && routing_id_size_ > 0)
        key.append (reinterpret_cast<const char *> (routing_id_),
                    routing_id_size_);
    return key;
}

}

bool zlink::socket_base_t::enter_public_api ()
{
    return lifecycle_coordinator ().enter_public_api ();
}

void zlink::socket_base_t::leave_public_api ()
{
    lifecycle_coordinator ().leave_public_api ();
}

bool zlink::socket_base_t::enter_callback_api ()
{
    return lifecycle_coordinator ().enter_callback_api ();
}

void zlink::socket_base_t::leave_callback_api ()
{
    if (lifecycle_coordinator ().leave_callback_api ())
        finish_close_handoff ();
}

bool zlink::socket_base_t::begin_close_or_fail_busy (bool from_self_callback_)
{
    return lifecycle_coordinator ().begin_close_or_fail_busy (
      from_self_callback_);
}

bool zlink::socket_base_t::public_close_requested () const
{
    return lifecycle_coordinator ().public_close_requested ();
}

void zlink::socket_base_t::lock_public_api_sync ()
{
    lifecycle_coordinator ().lock_public_api_sync ();
}

void zlink::socket_base_t::unlock_public_api_sync ()
{
    lifecycle_coordinator ().unlock_public_api_sync ();
}

bool zlink::socket_base_t::send_ready_slot (
  zlink_send_ready_handler_fn *handler_out_, void **subject_out_) const
{
    if (!handler_out_ || !subject_out_)
        return false;

    const dispatch_bridge_t &dispatch = dispatch_runtime ();
    while (true) {
        const uint32_t s1 = dispatch.send_ready_seq.load (
          std::memory_order_acquire);
        if ((s1 & 1u) != 0)
            continue;

        zlink_send_ready_handler_fn handler =
          dispatch.send_ready_handler.load (std::memory_order_acquire);
        void *subject =
          dispatch.send_ready_handler_subject.load (std::memory_order_acquire);
        const uint32_t s2 = dispatch.send_ready_seq.load (
          std::memory_order_acquire);
        if (s1 != s2 || (s2 & 1u) != 0)
            continue;

        *handler_out_ = handler;
        *subject_out_ = subject;
        return handler != NULL;
    }
}

void zlink::socket_base_t::finish_close_handoff ()
{
    lifecycle_coordinator ().clear_deferred_close ();

    if (lifecycle_coordinator ().is_async_mailbox_active ()) {
        stop_async_mailbox_processing ();
        wait_async_quiesced (2000);
    } else if (lifecycle_coordinator ().is_async_quiesce_pending ()) {
        wait_async_quiesced (2000);
    }

    if (_mailbox)
        static_cast<mailbox_t *> (_mailbox)->clear_signalers ();

    _tag = 0xdeadbeef;
    send_reap (this);
}

int zlink::socket_base_t::get_peer_state (const void *routing_id_,
                                          size_t routing_id_size_) const
{
    LIBZLINK_UNUSED (routing_id_);
    LIBZLINK_UNUSED (routing_id_size_);

    errno = ENOTSUP;
    return -1;
}

void zlink::socket_base_t::attach_pipe (pipe_t *pipe_,
                                        bool subscribe_to_all_,
                                        bool locally_initiated_)
{
    pipe_->set_event_sink (this);
    {
        scoped_lock_t lock (monitor_runtime ().sync);
        _pipes.push_back (pipe_);
    }

    xattach_pipe (pipe_, subscribe_to_all_, locally_initiated_);

    if (is_terminating ()) {
        register_term_acks (1);
        ++_term_pipe_acks_registered;
        pipe_->terminate (false);
    }
}

int zlink::socket_base_t::setsockopt (int option_,
                                      const void *optval_,
                                      size_t optvallen_)
{
    if (!enter_public_api ())
        return -1;

    if (unlikely (_ctx_terminated)) {
        leave_public_api ();
        errno = ETERM;
        return -1;
    }

    lock_public_api_sync ();
    int rc = xsetsockopt (option_, optval_, optvallen_);
    unlock_public_api_sync ();
    if (rc == 0 || errno != EINVAL) {
        leave_public_api ();
        return rc;
    }

    lock_public_api_sync ();
    rc = options.setsockopt (option_, optval_, optvallen_);
    update_pipe_options (option_);
    unlock_public_api_sync ();

    leave_public_api ();
    return rc;
}

int zlink::socket_base_t::getsockopt (int option_,
                                      void *optval_,
                                      size_t *optvallen_)
{
    if (!enter_public_api ())
        return -1;

    if (unlikely (_ctx_terminated)) {
        leave_public_api ();
        errno = ETERM;
        return -1;
    }

    lock_public_api_sync ();
    int rc = xgetsockopt (option_, optval_, optvallen_);
    unlock_public_api_sync ();
    if (rc == 0 || errno != EINVAL) {
        leave_public_api ();
        return rc;
    }

    if (option_ == ZLINK_INTERNAL_OPT_FD) {
        rc = do_getsockopt<fd_t> (
          optval_, optvallen_, static_cast<mailbox_t *> (_mailbox)->get_fd ());
        leave_public_api ();
        return rc;
    }

    if (option_ == ZLINK_INTERNAL_OPT_EVENTS) {
        lock_public_api_sync ();
        const int events_rc = process_commands (0, false);
        if (events_rc != 0 && (errno == EINTR || errno == ETERM)) {
            unlock_public_api_sync ();
            leave_public_api ();
            return -1;
        }
        errno_assert (events_rc == 0);
        const int out_rc = do_getsockopt<int> (optval_, optvallen_,
                                               has_out () ? ZLINK_POLLOUT : 0);
        unlock_public_api_sync ();
        leave_public_api ();
        return out_rc;
    }

    if (option_ == ZLINK_INTERNAL_OPT_LAST_ENDPOINT) {
        lock_public_api_sync ();
        const int out_rc = do_getsockopt (optval_, optvallen_, _last_endpoint);
        unlock_public_api_sync ();
        leave_public_api ();
        return out_rc;
    }

    lock_public_api_sync ();
    rc = options.getsockopt (option_, optval_, optvallen_);
    unlock_public_api_sync ();
    leave_public_api ();
    return rc;
}

int zlink::socket_base_t::get_events (int events_, uint32_t *out_)
{
    if (!enter_public_api ())
        return -1;

    if (!out_) {
        leave_public_api ();
        errno = EINVAL;
        return -1;
    }

    lock_public_api_sync ();
    const int rc = process_commands (0, false);
    if (rc != 0 && (errno == EINTR || errno == ETERM)) {
        unlock_public_api_sync ();
        leave_public_api ();
        return -1;
    }
    errno_assert (rc == 0);

    uint32_t events = 0;
    if ((events_ & ZLINK_POLLOUT) && has_out ())
        events |= ZLINK_POLLOUT;

    *out_ = events;
    unlock_public_api_sync ();
    leave_public_api ();
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

int zlink::socket_base_t::close ()
{
    const bool from_self_callback = send_ready_dispatch_in_callback ();
    if (!begin_close_or_fail_busy (from_self_callback))
        return -1;
    if (from_self_callback)
        return 0;

    finish_close_handoff ();
    return 0;
}

bool zlink::socket_base_t::has_in ()
{
    return xhas_in ();
}

bool zlink::socket_base_t::has_out ()
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
    notify_send_ready_if_armed ();
}

void zlink::socket_base_t::hiccuped (pipe_t *pipe_)
{
    if (options.immediate == 1)
        pipe_->terminate (false);
    else
        xhiccuped (pipe_);
}

void zlink::socket_base_t::pipe_terminated (pipe_t *pipe_)
{
    endpoint_uri_pair_t endpoint_pair;
    const unsigned char *routing_id_data = NULL;
    size_t routing_id_size = 0;
    if (pipe_) {
        endpoint_pair = pipe_->get_endpoint_pair ();
        const blob_t &routing_id = pipe_->get_routing_id ();
        if (routing_id.size () > 0) {
            routing_id_data = routing_id.data ();
            routing_id_size = routing_id.size ();
        }
    }

    xpipe_terminated (pipe_);
    endpoint_runtime ().inprocs.erase_pipe (pipe_);

    {
        scoped_lock_t lock (monitor_runtime ().sync);
        _pipes.erase (pipe_);
    }

    uint32_t ready_count = 0;
    bool ready_changed = false;
    {
        scoped_lock_t lock (monitor_runtime ().sync);
        ready_changed =
          _ready_connection_keys.erase (make_monitor_ready_key (
                                          endpoint_pair, routing_id_data,
                                          routing_id_size))
          != 0;
        if (ready_changed)
            ready_count = monitor_ready_count ();
    }
    if (ready_changed) {
        uint64_t values[1] = {ready_count};
        event (endpoint_pair, routing_id_data, routing_id_size, values, 1,
               ZLINK_EVENT_CONNECTION_READY_CHANGED);
    }

    const std::string &identifier = pipe_->get_endpoint_pair ().identifier ();
    if (!identifier.empty ()) {
        std::pair<endpoints_t::iterator, endpoints_t::iterator> range;
        range = endpoint_runtime ().endpoints.equal_range (identifier);

        for (endpoints_t::iterator it = range.first; it != range.second; ++it) {
            if (it->second.second == pipe_) {
                it->second.second = NULL;
                break;
            }
        }
    }

    if (is_terminating ()) {
        ++_term_pipe_acks_received;
        unregister_term_ack ();
    }
}

int zlink::socket_base_t::socket_id () const
{
    return options.socket_id;
}

bool zlink::socket_base_t::is_disconnected () const
{
    return _disconnected;
}

bool zlink::socket_base_t::is_ctx_terminated () const
{
    return _ctx_terminated;
}
