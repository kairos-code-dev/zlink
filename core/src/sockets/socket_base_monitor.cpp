/* SPDX-License-Identifier: MPL-2.0 */

#include "utils/precompiled.hpp"

#include "core/send_internal.hpp"
#include "sockets/socket_base.hpp"
#include "utils/sleep.hpp"
#include "zlink.h"

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

int zlink::socket_base_t::monitor_snapshot (zlink_monitor_snapshot_t *out_)
{
    if (!out_) {
        errno = EINVAL;
        return -1;
    }

    process_commands (0, false);
    memset (out_, 0, sizeof (*out_));
    out_->source_kind = ZLINK_MONITOR_SOURCE_SOCKET;
    out_->detail_flags =
      ZLINK_MONITOR_SNAPSHOT_DETAIL_READY_COUNT
      | ZLINK_MONITOR_SNAPSHOT_DETAIL_SND_PENDING_MSGS
      | ZLINK_MONITOR_SNAPSHOT_DETAIL_RCV_PENDING_MSGS;
    {
        scoped_lock_t lock (_monitor_sync);
        out_->ready_count = monitor_ready_count ();
        if (out_->ready_count > 0)
            out_->state_flags |= ZLINK_MONITOR_STATE_READY;

        for (pipes_t::size_type i = 0; i < _pipes.size (); ++i) {
            pipe_t *pipe = _pipes[i];
            out_->snd_pending_msgs += pipe->get_snd_pending_msgs ();
            out_->rcv_pending_msgs += pipe->get_rcv_pending_msgs_approx ();
        }
    }

    return 0;
}

uint32_t zlink::socket_base_t::monitor_ready_count () const
{
    return static_cast<uint32_t> (_ready_connection_keys.size ());
}

bool zlink::socket_base_t::has_attached_pipes () const
{
    scoped_lock_t lock (const_cast<socket_base_t *> (this)->_monitor_sync);
    return !const_cast<socket_base_t *> (this)->_pipes.empty ();
}

bool zlink::socket_base_t::monitor_has_attached_pipes () const
{
    return has_attached_pipes ();
}

void zlink::socket_base_t::socket_peer_remote_endpoints (
  std::vector<std::string> *out_)
{
    if (!out_)
        return;

    process_commands (0, false);
    out_->clear ();
    out_->reserve (_pipes.size ());
    for (pipes_t::size_type i = 0; i < _pipes.size (); ++i) {
        pipe_t *pipe = _pipes[i];
        const std::string &remote = pipe->get_endpoint_pair ().remote;
        if (!remote.empty ())
            out_->push_back (remote);
    }
}

int zlink::socket_base_t::monitor (const char *endpoint_,
                                   uint64_t events_,
                                   int event_version_,
                                   int type_)
{
    scoped_lock_t lock (_monitor_sync);

    if (unlikely (_ctx_terminated)) {
        errno = ETERM;
        return -1;
    }

    if (endpoint_ == NULL) {
        stop_monitor ();
        return 0;
    }

    std::string protocol;
    std::string address;
    if (parse_uri (endpoint_, protocol, address) || check_protocol (protocol))
        return -1;

    if (protocol != protocol_name::inproc) {
        errno = EPROTONOSUPPORT;
        return -1;
    }

    if (_monitor_socket != NULL)
        stop_monitor (true);

    switch (type_) {
        case ZLINK_CORE_SOCKET_PAIR:
        case ZLINK_CORE_SOCKET_PUB:
            break;
        default:
            errno = EINVAL;
            return -1;
    }

    _monitor_events = events_;
    _monitor_lossy = event_version_ <= 3;
    _monitor_async_mailbox_owned = false;
    _monitor_socket = static_cast<void *> (get_ctx ()->create_socket (type_));
    if (_monitor_socket == NULL)
        return -1;

    int linger = 0;
    int rc = static_cast<socket_base_t *> (_monitor_socket)
               ->setsockopt (ZLINK_INTERNAL_OPT_LINGER, &linger, sizeof (linger));
    if (rc == -1)
        stop_monitor (false);

    rc = zlink_bind (_monitor_socket, endpoint_);
    if (rc == -1)
        stop_monitor (false);
    else {
        _monitor_queue_sync.lock ();
        _monitor_queue.clear ();
        _monitor_queue_stop = false;
        _monitor_queue_sync.unlock ();
        _monitor_thread.start (&socket_base_t::monitor_thread_main, this,
                               "sock-monitor");
        _monitor_thread_started = true;
        _monitor_events_atomic.store (_monitor_events,
                                      std::memory_order_release);
    }
    return rc;
}

void zlink::socket_base_t::event_connected (
  const endpoint_uri_pair_t &endpoint_uri_pair_, zlink::fd_t fd_)
{
    uint64_t values[1] = {static_cast<uint64_t> (fd_)};
    event (endpoint_uri_pair_, NULL, 0, values, 1, ZLINK_EVENT_CONNECTED);
}

void zlink::socket_base_t::event_connect_delayed (
  const endpoint_uri_pair_t &endpoint_uri_pair_, int err_)
{
    uint64_t values[1] = {static_cast<uint64_t> (err_)};
    event (endpoint_uri_pair_, NULL, 0, values, 1, ZLINK_EVENT_CONNECT_DELAYED);
}

void zlink::socket_base_t::event_connect_retried (
  const endpoint_uri_pair_t &endpoint_uri_pair_, int interval_)
{
    uint64_t values[1] = {static_cast<uint64_t> (interval_)};
    event (endpoint_uri_pair_, NULL, 0, values, 1, ZLINK_EVENT_CONNECT_RETRIED);
}

void zlink::socket_base_t::event_listening (
  const endpoint_uri_pair_t &endpoint_uri_pair_, zlink::fd_t fd_)
{
    uint64_t values[1] = {static_cast<uint64_t> (fd_)};
    event (endpoint_uri_pair_, NULL, 0, values, 1, ZLINK_EVENT_LISTENING);
}

void zlink::socket_base_t::event_bind_failed (
  const endpoint_uri_pair_t &endpoint_uri_pair_, int err_)
{
    uint64_t values[1] = {static_cast<uint64_t> (err_)};
    event (endpoint_uri_pair_, NULL, 0, values, 1, ZLINK_EVENT_BIND_FAILED);
}

void zlink::socket_base_t::event_accepted (
  const endpoint_uri_pair_t &endpoint_uri_pair_, zlink::fd_t fd_)
{
    uint64_t values[1] = {static_cast<uint64_t> (fd_)};
    event (endpoint_uri_pair_, NULL, 0, values, 1, ZLINK_EVENT_ACCEPTED);
}

void zlink::socket_base_t::event_accept_failed (
  const endpoint_uri_pair_t &endpoint_uri_pair_, int err_)
{
    uint64_t values[1] = {static_cast<uint64_t> (err_)};
    event (endpoint_uri_pair_, NULL, 0, values, 1, ZLINK_EVENT_ACCEPT_FAILED);
}

void zlink::socket_base_t::event_closed (
  const endpoint_uri_pair_t &endpoint_uri_pair_, zlink::fd_t fd_)
{
    uint64_t values[1] = {static_cast<uint64_t> (fd_)};
    event (endpoint_uri_pair_, NULL, 0, values, 1, ZLINK_EVENT_CLOSED);
}

void zlink::socket_base_t::event_close_failed (
  const endpoint_uri_pair_t &endpoint_uri_pair_, int err_)
{
    uint64_t values[1] = {static_cast<uint64_t> (err_)};
    event (endpoint_uri_pair_, NULL, 0, values, 1, ZLINK_EVENT_CLOSE_FAILED);
}

void zlink::socket_base_t::event_disconnected (
  const endpoint_uri_pair_t &endpoint_uri_pair_,
  uint64_t reason_,
  const unsigned char *routing_id_,
  size_t routing_id_size_)
{
    uint64_t values[1] = {reason_};
    event (endpoint_uri_pair_, routing_id_, routing_id_size_, values, 1,
           ZLINK_EVENT_DISCONNECTED);

    uint32_t ready_count = 0;
    bool changed = false;
    {
        scoped_lock_t lock (_monitor_sync);
        changed =
          _ready_connection_keys.erase (make_monitor_ready_key (
                                          endpoint_uri_pair_, routing_id_,
                                          routing_id_size_))
          != 0;
        if (changed)
            ready_count = monitor_ready_count ();
    }
    if (changed) {
        uint64_t ready_values[1] = {ready_count};
        event (endpoint_uri_pair_, routing_id_, routing_id_size_, ready_values,
               1, ZLINK_EVENT_CONNECTION_READY_CHANGED);
    }
}

void zlink::socket_base_t::event_handshake_failed_no_detail (
  const endpoint_uri_pair_t &endpoint_uri_pair_, int err_)
{
    uint64_t values[1] = {static_cast<uint64_t> (err_)};
    event (endpoint_uri_pair_, NULL, 0, values, 1,
           ZLINK_EVENT_HANDSHAKE_FAILED_NO_DETAIL);
}

void zlink::socket_base_t::event_handshake_failed_protocol (
  const endpoint_uri_pair_t &endpoint_uri_pair_, int err_)
{
    uint64_t values[1] = {static_cast<uint64_t> (err_)};
    event (endpoint_uri_pair_, NULL, 0, values, 1,
           ZLINK_EVENT_HANDSHAKE_FAILED_PROTOCOL);
}

void zlink::socket_base_t::event_handshake_failed_auth (
  const endpoint_uri_pair_t &endpoint_uri_pair_, int err_)
{
    uint64_t values[1] = {static_cast<uint64_t> (err_)};
    event (endpoint_uri_pair_, NULL, 0, values, 1,
           ZLINK_EVENT_HANDSHAKE_FAILED_AUTH);
}

void zlink::socket_base_t::event_connection_ready_changed (
  const endpoint_uri_pair_t &endpoint_uri_pair_,
  const unsigned char *routing_id_,
  size_t routing_id_size_)
{
    uint32_t ready_count = 0;
    bool changed = false;
    {
        scoped_lock_t lock (_monitor_sync);
        changed = _ready_connection_keys.insert (make_monitor_ready_key (
                                                     endpoint_uri_pair_,
                                                     routing_id_,
                                                     routing_id_size_))
                    .second;
        if (changed)
            ready_count = monitor_ready_count ();
    }
    if (!changed)
        return;

    uint64_t values[1] = {ready_count};
    event (endpoint_uri_pair_, routing_id_, routing_id_size_, values, 1,
           ZLINK_EVENT_CONNECTION_READY_CHANGED);
}

void zlink::socket_base_t::emit_inproc_connection_ready (pipe_t *pipe_)
{
    if (!pipe_)
        return;

    if (!pipe_->mark_connection_ready_event_emitted ())
        return;

    const endpoint_uri_pair_t &endpoint_pair = pipe_->get_endpoint_pair ();
    const blob_t &routing_id = pipe_->get_routing_id ();
    const unsigned char *routing_id_data =
      routing_id.size () > 0 ? routing_id.data () : NULL;
    event_connection_ready_changed (endpoint_pair, routing_id_data,
                                    routing_id.size ());
}

void zlink::socket_base_t::emit_socket_monitor_value_event (
  uint64_t event_,
  uint64_t value_,
  const endpoint_uri_pair_t &endpoint_uri_pair_)
{
    uint64_t values[1] = {value_};
    event (endpoint_uri_pair_, NULL, 0, values, 1, event_);
}

void zlink::socket_base_t::event (const endpoint_uri_pair_t &endpoint_uri_pair_,
                                  const unsigned char *routing_id_,
                                  size_t routing_id_size_,
                                  uint64_t values_[],
                                  uint64_t values_count_,
                                  uint64_t type_)
{
    if (_monitor_events_atomic.load (std::memory_order_acquire) == 0)
        return;

    scoped_lock_t lock (_monitor_sync);
    if (_monitor_events & type_) {
        monitor_event_record_t record;
        if (build_monitor_event_record (&record, type_, values_, values_count_,
                                        routing_id_, routing_id_size_,
                                        endpoint_uri_pair_))
            enqueue_monitor_event (record);
    }
}

void zlink::socket_base_t::monitor_thread_main (void *arg_)
{
    static_cast<socket_base_t *> (arg_)->monitor_loop ();
}

void zlink::socket_base_t::monitor_loop ()
{
    void *monitor_socket = _monitor_socket;
    const bool supports_sub_delivery_ready =
      options.type == ZLINK_CORE_SOCKET_SUB
      || options.type == ZLINK_CORE_SOCKET_XSUB;
    const bool supports_pub_delivery_ready =
      options.type == ZLINK_CORE_SOCKET_PUB
      || options.type == ZLINK_CORE_SOCKET_XPUB;
    const bool pump_delivery_ready =
      (supports_sub_delivery_ready
       && (_monitor_events & ZLINK_EVENT_SUB_DELIVERY_READY_CHANGED) != 0)
      || (supports_pub_delivery_ready
          && (_monitor_events & ZLINK_EVENT_PUB_DELIVERY_READY_CHANGED) != 0);
    _monitor_queue_sync.lock ();
    while (true) {
        if (_monitor_queue_stop)
            break;
        if (_monitor_queue.empty ()) {
            if (pump_delivery_ready) {
                _monitor_queue_sync.unlock ();
                process_commands (0, false);
                _monitor_queue_sync.lock ();
                if (_monitor_queue_stop)
                    break;
                if (_monitor_queue.empty ())
                    (void) _monitor_queue_cv.wait (&_monitor_queue_sync, 10);
            } else {
                (void) _monitor_queue_cv.wait (&_monitor_queue_sync, -1);
            }
            continue;
        }

        monitor_event_record_t record = _monitor_queue.front ();
        _monitor_queue.pop_front ();
        _monitor_queue_sync.unlock ();
        bool delivered = true;
        if (monitor_socket)
            delivered = dispatch_monitor_event (monitor_socket, record);
        if (!delivered && !_monitor_lossy) {
            _monitor_queue_sync.lock ();
            _monitor_queue.push_front (record);
            _monitor_queue_sync.unlock ();
            zlink::sleep_ms (1);
        }
        _monitor_queue_sync.lock ();
    }
    _monitor_queue_sync.unlock ();
}

void zlink::socket_base_t::enqueue_monitor_event (
  const monitor_event_record_t &record_)
{
    _monitor_queue_sync.lock ();
    if (!_monitor_queue_stop
        && (_monitor_queue.size () < static_cast<size_t> (monitor_queue_hwm)
            || !_monitor_lossy)) {
        _monitor_queue.push_back (record_);
        _monitor_queue_cv.broadcast ();
    }
    _monitor_queue_sync.unlock ();
}

bool zlink::socket_base_t::build_monitor_event_record (
  monitor_event_record_t *out_,
  uint64_t event_,
  const uint64_t values_[],
  uint64_t values_count_,
  const unsigned char *routing_id_,
  size_t routing_id_size_,
  const endpoint_uri_pair_t &endpoint_uri_pair_) const
{
    if (!out_ || values_count_ > monitor_max_values
        || routing_id_size_ > sizeof (out_->routing_id.data))
        return false;

    out_->event = event_;
    out_->values_count = values_count_;
    memset (out_->values, 0, sizeof (out_->values));
    for (uint64_t i = 0; i < values_count_; ++i)
        out_->values[i] = values_[i];
    memset (&out_->routing_id, 0, sizeof (out_->routing_id));
    out_->routing_id.size = static_cast<uint8_t> (routing_id_size_);
    if (routing_id_size_ > 0 && routing_id_)
        memcpy (out_->routing_id.data, routing_id_, routing_id_size_);
    out_->endpoint_uri_pair = endpoint_uri_pair_;
    return true;
}

bool zlink::socket_base_t::dispatch_monitor_event (
  void *monitor_socket_,
  const monitor_event_record_t &record_) const
{
    if (!monitor_socket_)
        return false;

    zlink_monitor_event_t wire_event;
    memset (&wire_event, 0, sizeof (wire_event));
    wire_event.event = record_.event;
    if (record_.values_count > 0)
        wire_event.value = record_.values[0];
    wire_event.routing_id = record_.routing_id;

    const size_t local_copy =
      record_.endpoint_uri_pair.local.size () < sizeof (wire_event.local_addr) - 1
        ? record_.endpoint_uri_pair.local.size ()
        : sizeof (wire_event.local_addr) - 1;
    if (local_copy > 0) {
        memcpy (wire_event.local_addr, record_.endpoint_uri_pair.local.data (),
                local_copy);
        wire_event.local_addr[local_copy] = '\0';
    }

    const size_t remote_copy =
      record_.endpoint_uri_pair.remote.size ()
        < sizeof (wire_event.remote_addr) - 1
        ? record_.endpoint_uri_pair.remote.size ()
        : sizeof (wire_event.remote_addr) - 1;
    if (remote_copy > 0) {
        memcpy (wire_event.remote_addr, record_.endpoint_uri_pair.remote.data (),
                remote_copy);
        wire_event.remote_addr[remote_copy] = '\0';
    }

    zlink_msg_t msg;
    zlink_msg_init_size (&msg, sizeof (wire_event));
    memcpy (zlink_msg_data (&msg), &wire_event, sizeof (wire_event));
    const int send_flags = _monitor_lossy ? ZLINK_DONTWAIT : 0;
    if (zlink::send_msg_internal (monitor_socket_, &msg, send_flags) == -1) {
        zlink_msg_close (&msg);
        return false;
    }
    return true;
}

void zlink::socket_base_t::stop_monitor (bool send_monitor_stopped_event_)
{
    if (_monitor_socket) {
        _monitor_events_atomic.store (0, std::memory_order_release);
        socket_base_t *monitor_socket =
          static_cast<socket_base_t *> (_monitor_socket);
        bool can_emit_monitor_stopped = false;
        const bool stop_async_mailbox =
          _monitor_async_mailbox_owned && !socket_msg_dispatch_active ()
          && !sub_dispatch_active () && !xpub_dispatch_active ()
          && !stream_dispatch_active ();

        if ((_monitor_events & ZLINK_EVENT_MONITOR_STOPPED)
            && send_monitor_stopped_event_) {
            monitor_socket->process_commands (0, false);
            can_emit_monitor_stopped = !monitor_socket->_pipes.empty ();
        }

        stop_monitor_thread ();

        if (can_emit_monitor_stopped) {
            uint64_t values[1] = {0};
            monitor_event_record_t record;
            if (build_monitor_event_record (&record, ZLINK_EVENT_MONITOR_STOPPED,
                                            values, 1, NULL, 0,
                                            endpoint_uri_pair_t ()))
                dispatch_monitor_event (_monitor_socket, record);
        }
        zlink_close (_monitor_socket);
        _monitor_socket = NULL;
        _monitor_events = 0;
        _monitor_lossy = true;
        if (stop_async_mailbox) {
            stop_async_mailbox_processing ();
            wait_async_quiesced (10000);
        }
        _monitor_async_mailbox_owned = false;
    }
}

void zlink::socket_base_t::stop_monitor_thread ()
{
    _monitor_queue_sync.lock ();
    _monitor_queue_stop = true;
    _monitor_queue.clear ();
    _monitor_queue_cv.broadcast ();
    _monitor_queue_sync.unlock ();

    if (_monitor_thread_started) {
        _monitor_thread.stop ();
        _monitor_thread_started = false;
    }
}
