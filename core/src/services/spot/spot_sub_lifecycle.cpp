/* SPDX-License-Identifier: MPL-2.0 */

#include "precompiled.hpp"

#include "services/spot/spot_sub.hpp"

#include "services/common/monitor_decode.hpp"
#include "services/common/socket_monitor_bridge.hpp"
#include "services/spot/spot_data_plane_internal.hpp"
#include "services/spot/spot_node.hpp"

#include "sockets/socket_base.hpp"

#include <stdio.h>
#include <string.h>

namespace zlink
{
namespace
{
static void preserve_first_error (int rc_, int *first_error_)
{
    if (rc_ == 0 || !first_error_ || *first_error_ != 0)
        return;
    *first_error_ = errno != 0 ? errno : EIO;
}

static void spot_sub_diag_log (const char *stage_)
{
    if (!getenv ("ZLINK_SPOT_SUB_DIAG_LOG"))
        return;

    FILE *fp = fopen ("/tmp/zlink_spot_sub_diag.log", "a");
    if (!fp)
        return;

    fprintf (fp,
             "ts=%llu pid=%ld stage=%s\n",
             static_cast<unsigned long long> (zlink::clock_t ().now_ms ()),
             static_cast<long> (getpid ()),
             stage_ ? stage_ : "?");
    fclose (fp);
}

static void fill_terminal_monitor_event (zlink_service_event_t *event_,
                                         uint32_t event_type_,
                                         const zlink_routing_id_t &rid_)
{
    memset (event_, 0, sizeof (*event_));
    event_->service_kind = ZLINK_SERVICE_KIND_SPOT_SUB;
    event_->event_type = event_type_;
    event_->detail_flags = ZLINK_EVENT_DETAIL_SUBJECT_RID;
    event_->routing_id = rid_;
}

static void copy_endpoint (char *dst_, size_t dst_size_, const char *src_)
{
    if (!dst_ || dst_size_ == 0)
        return;
    dst_[0] = '\0';
    if (!src_ || src_[0] == '\0')
        return;
    const size_t copy_size = strlen (src_) < dst_size_ - 1 ? strlen (src_)
                                                           : dst_size_ - 1;
    if (copy_size > 0)
        memcpy (dst_, src_, copy_size);
    dst_[copy_size] = '\0';
}

static void fill_socket_monitor_event (zlink_service_event_t *event_,
                                       uint32_t event_type_,
                                       const zlink_monitor_event_t &raw_)
{
    memset (event_, 0, sizeof (*event_));
    event_->service_kind = ZLINK_SERVICE_KIND_SPOT_SUB;
    event_->event_type = event_type_;
    event_->status = static_cast<int32_t> (raw_.event);
    event_->value = static_cast<uint32_t> (raw_.value);
    if (raw_.routing_id.size > 0) {
        event_->routing_id = raw_.routing_id;
        event_->detail_flags |= ZLINK_EVENT_DETAIL_PEER_RID;
    }
    if (raw_.remote_addr[0] != '\0') {
        copy_endpoint (event_->endpoint, sizeof (event_->endpoint),
                       raw_.remote_addr);
        event_->detail_flags |= ZLINK_EVENT_DETAIL_ENDPOINT;
    }
}
}

void spot_sub_t::monitor_thread_main (void *arg_)
{
    static_cast<spot_sub_t *> (arg_)->monitor_loop ();
}

int spot_sub_t::ensure_monitor_bridge_started ()
{
    socket_base_t *socket = this->socket ();
    scoped_lock_t lock (_sync);
    if (_raw_monitor_socket)
        return 0;
    if (_destroying.load (std::memory_order_acquire)) {
        errno = EFSM;
        return -1;
    }
    if (!socket) {
        errno = EFAULT;
        return -1;
    }

    void *monitor_socket = open_socket_monitor_bridge (
      socket, ZLINK_EVENT_CONNECTED | ZLINK_EVENT_ACCEPTED
                 | ZLINK_EVENT_CONNECTION_READY_CHANGED | ZLINK_EVENT_DISCONNECTED
                 | ZLINK_EVENT_BIND_FAILED | ZLINK_EVENT_ACCEPT_FAILED
                 | ZLINK_EVENT_CLOSE_FAILED
                 | ZLINK_EVENT_HANDSHAKE_FAILED_NO_DETAIL
                 | ZLINK_EVENT_HANDSHAKE_FAILED_PROTOCOL
                 | ZLINK_EVENT_HANDSHAKE_FAILED_AUTH);
    if (!monitor_socket)
        return -1;

    _raw_monitor_socket = monitor_socket;
    _monitor_stop.set (0);
    _monitor_thread.start (monitor_thread_main, this, "spot-sub-mon");
    _monitor_thread_started = true;
    return 0;
}

int spot_sub_t::stop_monitor_bridge ()
{
    void *raw_monitor_socket = NULL;
    ctx_t *ctx = _node ? _node->ctx () : NULL;
    socket_base_t *socket = this->socket ();
    int first_error = 0;
    {
        scoped_lock_t lock (_sync);
        _monitor_stop.set (1);
        raw_monitor_socket = _raw_monitor_socket;
        _raw_monitor_socket = NULL;
    }

    if (socket)
        preserve_first_error (socket->monitor (NULL, 0, 3, ZLINK_CORE_SOCKET_PAIR),
                              &first_error);

    if (_monitor_thread_started) {
        _monitor_thread.stop ();
        _monitor_thread_started = false;
    }
    if (raw_monitor_socket) {
        socket_base_t *monitor_socket =
          static_cast<socket_base_t *> (raw_monitor_socket);
        if (_node && ctx)
            preserve_first_error (
              _node->_lifecycle.close_socket (monitor_socket, 2000),
              &first_error);
        else {
            monitor_socket->stop ();
            monitor_socket->close ();
            monitor_socket = NULL;
        }
    }
    if (first_error != 0) {
        errno = first_error;
        return -1;
    }
    return 0;
}

void spot_sub_t::monitor_loop ()
{
    while (_monitor_stop.get () == 0) {
        void *raw_monitor_socket = NULL;
        {
            scoped_lock_t lock (_sync);
            raw_monitor_socket = _raw_monitor_socket;
        }
        if (!raw_monitor_socket)
            return;

        if (zlink::wait_socket_events_internal (raw_monitor_socket,
                                                ZLINK_POLLIN, 50)
            <= 0)
            continue;

        zlink_monitor_event_t raw;
        if (recv_socket_monitor_event (raw_monitor_socket, &raw,
                                       ZLINK_DONTWAIT)
            != 0) {
            if (errno == EAGAIN)
                continue;
            if (_monitor_stop.get () != 0)
                return;
            continue;
        }

        zlink_service_event_t event;
        zlink_service_event_t batch[2];
        switch (raw.event) {
            case ZLINK_EVENT_CONNECTED:
            case ZLINK_EVENT_ACCEPTED:
                fill_socket_monitor_event (&event, ZLINK_MONITOR_EVENT_PEER_UP,
                                           raw);
                emit_monitor_event (event);
                break;

            case ZLINK_EVENT_CONNECTION_READY_CHANGED: {
                {
                    scoped_lock_t lock (_sync);
                    (void) sync_monitor_ready_endpoint (
                      &_ready_peer_endpoints, raw);
                }
                fill_socket_monitor_event (
                  &batch[0], ZLINK_SPOT_MONITOR_EVENT_READY_CHANGED, raw);
                batch[0].value = raw.value > 0 ? 1u : 0u;
                emit_monitor_event (batch[0]);
                break;
            }

            case ZLINK_EVENT_DISCONNECTED:
                {
                    scoped_lock_t lock (_sync);
                    if (raw.remote_addr[0] != '\0')
                        _ready_peer_endpoints.erase (raw.remote_addr);
                }
                fill_socket_monitor_event (&event, ZLINK_MONITOR_EVENT_PEER_DOWN,
                                           raw);
                emit_monitor_event (event);
                break;

            case ZLINK_EVENT_BIND_FAILED:
            case ZLINK_EVENT_ACCEPT_FAILED:
            case ZLINK_EVENT_CLOSE_FAILED:
            case ZLINK_EVENT_HANDSHAKE_FAILED_NO_DETAIL:
            case ZLINK_EVENT_HANDSHAKE_FAILED_PROTOCOL:
            case ZLINK_EVENT_HANDSHAKE_FAILED_AUTH:
                fill_socket_monitor_event (&event, ZLINK_MONITOR_EVENT_ERROR,
                                           raw);
                event.error_code = static_cast<int32_t> (raw.value);
                emit_monitor_event (event);
                break;

            default:
                break;
        }
    }
}

int spot_sub_t::destroy_internal (bool allow_embedded_default_,
                                  bool notify_node_)
{
    spot_sub_diag_log ("destroy.begin");
    if (_node_owned_default && !allow_embedded_default_) {
        errno = EINVAL;
        return -1;
    }

    _destroying.store (true, std::memory_order_release);

    socket_base_t *socket = this->socket ();
    int first_error = 0;
    std::vector<std::pair<std::string, std::string> > ready_ack_updates;
    bool had_filters = false;
    const bool node_shutting_down = _node && _node->is_shutting_down ();

    {
        scoped_lock_t lock (_sync);
        had_filters = !_topics.empty () || !_patterns.empty ();
    }

    bool has_handler = false;
    {
        scoped_lock_t lock (_sync);
        has_handler = _handler_state.load (std::memory_order_acquire)
                      != handler_none;
        if (has_handler)
            _handler_state.store (handler_clearing, std::memory_order_release);
    }

    if (socket) {
        for (std::set<std::string>::const_iterator it = _topics.begin (),
                                                   end = _topics.end ();
             it != end; ++it)
            (void) socket->setsockopt (ZLINK_INTERNAL_OPT_UNSUBSCRIBE, it->c_str (),
                                       it->size ());
        for (std::set<std::string>::const_iterator it = _patterns.begin (),
                                                   end = _patterns.end ();
             it != end; ++it)
            (void) socket->setsockopt (ZLINK_INTERNAL_OPT_UNSUBSCRIBE, it->c_str (),
                                       it->size ());
    }
    if (has_handler && socket && socket->sub_dispatch_active ())
        spot_sub_diag_log ("destroy.before-sub-dispatch-stop");
    if (has_handler && socket && socket->sub_dispatch_active ())
        socket->sub_dispatch_stop ();
    if (has_handler)
        spot_sub_diag_log ("destroy.after-sub-dispatch-stop");
    {
        clock_t clock;
        const uint64_t callback_wait_deadline = clock.now_ms () + 10000;
        scoped_lock_t lock (_sync);
        while (_callback_inflight.get () > 0) {
            const uint64_t now = clock.now_ms ();
            if (now >= callback_wait_deadline) {
                errno = ETIMEDOUT;
                return -1;
            }

            const int wait_ms =
              static_cast<int> (callback_wait_deadline - now);
            if (_callback_cv.wait (&_sync, wait_ms) != 0 && errno != EAGAIN)
                return -1;
        }
        _active_direct_handler.store (NULL, std::memory_order_release);
        _handler_state.store (handler_none, std::memory_order_release);
        _callback_cv.broadcast ();
    }
    spot_sub_diag_log ("destroy.before-stop-monitor-bridge");
    preserve_first_error (stop_monitor_bridge (), &first_error);
    spot_sub_diag_log ("destroy.after-stop-monitor-bridge");

    release_all_ready_ack_endpoints (&ready_ack_updates);
    if (_node && !node_shutting_down) {
        const std::string ack_source_id = ready_ack_source_id ();
        for (size_t i = 0; i < ready_ack_updates.size (); ++i) {
            (void) _node->send_ready_ack_update (ready_ack_updates[i].second,
                                                 ready_ack_updates[i].first,
                                                 ack_source_id, false);
        }
    }

    if (notify_node_ && _node)
        _node->remove_spot_sub (this);
    if (notify_node_ && _node)
        _node->submit_sub_summary (this, ZLINK_TOPOLOGY_STATE_STOPPED, 0);
    if (notify_node_ && _node && had_filters && !node_shutting_down) {
        _node->schedule_subscription_replay ();
        preserve_first_error (_node->replay_subscriptions_if_active_peers (),
                              &first_error);
    }
    {
        scoped_lock_t lock (_sync);
        _topics.clear ();
        _patterns.clear ();
        _delivery_ready_raw_filters.clear ();
        _ready_peer_endpoints.clear ();
        _ready_subject_endpoints.clear ();
        _ready_ack_endpoints.clear ();
    }

    zlink_service_event_t terminal;
    fill_terminal_monitor_event (&terminal, ZLINK_MONITOR_EVENT_CLOSED,
                                 _routing_id);
    _monitor.close_all (&terminal);
    spot_sub_diag_log ("destroy.after-monitor-close-all");

    if (socket) {
        if (_node)
            spot_sub_diag_log ("destroy.before-destroy-attachment");
        if (socket && _node)
            preserve_first_error (
              node_shutting_down ? _node->destroy_attachment_async (_attachment_id)
                                 : _node->destroy_attachment (_attachment_id),
              &first_error);
        if (socket && _node)
            spot_sub_diag_log ("destroy.after-destroy-attachment");
        else {
            socket->stop ();
            socket->close ();
        }
    }
    _socket = NULL;
    _attachment_id = 0;
    _node = NULL;
    _node_owned_default = false;
    if (first_error != 0) {
        errno = first_error;
        return -1;
    }
    spot_sub_diag_log ("destroy.end");
    return 0;
}

int spot_sub_t::destroy ()
{
    return destroy_internal (false, true);
}

int spot_sub_t::destroy_from_node ()
{
    return destroy_internal (true, true);
}

int spot_sub_t::abort_create ()
{
    return destroy_internal (true, false);
}
} // namespace zlink
