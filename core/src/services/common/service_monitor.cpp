/* SPDX-License-Identifier: MPL-2.0 */

#include "precompiled.hpp"

#include "services/common/service_monitor.hpp"

#include "utils/random.hpp"

#include <stdio.h>
#include <string.h>

namespace zlink
{
namespace
{
void close_monitor_servers (ctx_t *ctx_,
                            const std::vector<socket_base_t *> &servers_,
                            int timeout_ms_)
{
    if (servers_.empty ())
        return;

    for (size_t i = 0; i < servers_.size (); ++i) {
        socket_base_t *server = servers_[i];
        if (!server)
            continue;
        server->stop ();
        server->close ();
    }

    if (!ctx_)
        return;

    const uint64_t deadline_ms =
      timeout_ms_ >= 0 ? zlink::clock_t ().now_ms () + timeout_ms_ : 0;
    for (size_t i = 0; i < servers_.size (); ++i) {
        const socket_base_t *server = servers_[i];
        if (!server)
            continue;

        const int remaining_ms =
          timeout_ms_ < 0
            ? -1
            : static_cast<int> (deadline_ms - zlink::clock_t ().now_ms ());
        if (timeout_ms_ >= 0 && remaining_ms <= 0)
            break;

        (void) ctx_->wait_for_socket_removal (server, remaining_ms);
    }
}
}

service_monitor_hub_t::service_monitor_hub_t (ctx_t *ctx_) :
    _ctx (ctx_),
    _watcher_count (0),
    _next_id (generate_random ()),
    _dispatch_stop (false),
    _dispatch_thread_started (false)
{
}

service_monitor_hub_t::~service_monitor_hub_t ()
{
    stop_dispatch_thread ();
    close_all ();
}

static void set_monitor_socket_defaults (socket_base_t *socket_)
{
    if (!socket_)
        return;

    const int linger = 0;
    const int hwm = 1024;
    socket_->setsockopt (ZLINK_INTERNAL_OPT_LINGER, &linger, sizeof (linger));
    socket_->setsockopt (ZLINK_INTERNAL_OPT_SNDHWM, &hwm, sizeof (hwm));
    socket_->setsockopt (ZLINK_INTERNAL_OPT_RCVHWM, &hwm, sizeof (hwm));
}

static bool recv_monitor_handshake (socket_base_t *socket_, long timeout_ms_)
{
    const int timeout = static_cast<int> (timeout_ms_);
    if (socket_->setsockopt (ZLINK_INTERNAL_OPT_RCVTIMEO, &timeout, sizeof (timeout)) != 0)
        return false;

    msg_t msg;
    if (msg.init () != 0)
        return false;

    const int rc = socket_->recv (&msg, 0);
    msg.close ();
    return rc == 0;
}

static bool handshake_monitor_pair (socket_base_t *server_, socket_base_t *client_)
{
    if (!server_ || !client_)
        return false;

    const unsigned char hello = 0x01;
    const unsigned char ack = 0x02;

    msg_t msg;
    if (msg.init_size (sizeof (hello)) != 0)
        return false;
    memcpy (msg.data (), &hello, sizeof (hello));
    if (client_->send (&msg, 0) != 0) {
        msg.close ();
        return false;
    }
    msg.close ();

    if (!recv_monitor_handshake (server_, 100))
        return false;

    if (msg.init_size (sizeof (ack)) != 0)
        return false;
    memcpy (msg.data (), &ack, sizeof (ack));
    if (server_->send (&msg, 0) != 0) {
        msg.close ();
        return false;
    }
    msg.close ();

    if (!recv_monitor_handshake (client_, 100))
        return false;

    return true;
}

void *service_monitor_hub_t::open (int events_)
{
    if (!_ctx || events_ == 0) {
        errno = EINVAL;
        return NULL;
    }

    // Each snapshot-style monitor open closes the client immediately after
    // reading. Prune stale server peers before allocating the next watcher so
    // destroy paths do not inherit an unbounded backlog of already-closed
    // monitor sockets.
    prune_closed_watchers ();

    socket_base_t *server = _ctx->create_socket (ZLINK_CORE_SOCKET_PAIR);
    socket_base_t *client = _ctx->create_socket (ZLINK_CORE_SOCKET_PAIR);
    if (!server || !client) {
        if (server)
            server->close ();
        if (client)
            client->close ();
        errno = ENOMEM;
        return NULL;
    }

    set_monitor_socket_defaults (server);
    set_monitor_socket_defaults (client);

    char endpoint[128];
    snprintf (endpoint, sizeof endpoint, "inproc://svcmon-%p-%u",
              static_cast<void *> (this),
              ++_next_id);

    if (server->bind (endpoint) != 0 || client->connect (endpoint) != 0) {
        client->close ();
        server->close ();
        return NULL;
    }

    if (!handshake_monitor_pair (server, client)) {
        client->close ();
        server->close ();
        errno = EPROTO;
        return NULL;
    }

    watcher_t watcher;
    watcher.server = server;
    watcher.events = static_cast<uint32_t> (events_);
    watcher.endpoint = endpoint;

    ensure_dispatch_thread_started ();

    scoped_lock_t lock (_sync);
    _watchers.push_back (watcher);
    _watcher_count.store (_watchers.size (), std::memory_order_release);
    return static_cast<void *> (client);
}

uint32_t service_monitor_hub_t::event_delivery_mask (
  const zlink_service_event_t &event_)
{
    return event_.event_type;
}

void service_monitor_hub_t::emit (const zlink_service_event_t &event_)
{
    emit_batch (&event_, 1);
}

void service_monitor_hub_t::emit_batch (const zlink_service_event_t *events_,
                                        size_t count_)
{
    if (!events_ || count_ == 0)
        return;
    if (_watcher_count.load (std::memory_order_acquire) == 0)
        return;

    ensure_dispatch_thread_started ();

    scoped_lock_t lock (_dispatch_sync);
    if (_dispatch_stop)
        return;
    for (size_t i = 0; i < count_; ++i)
        _dispatch_queue.push_back (events_[i]);
    _dispatch_cv.broadcast ();
}

void service_monitor_hub_t::close_all (const zlink_service_event_t *terminal_event_)
{
    stop_dispatch_thread ();
    std::vector<socket_base_t *> servers;
    {
        scoped_lock_t lock (_sync);
        if (terminal_event_) {
            for (size_t i = 0; i < _watchers.size (); ++i) {
                watcher_t &watcher = _watchers[i];
                if (!watcher.server)
                    continue;
                if ((watcher.events & event_delivery_mask (*terminal_event_))
                    == 0)
                    continue;

                msg_t msg;
                if (msg.init_size (sizeof (*terminal_event_)) != 0)
                    continue;
                memcpy (msg.data (), terminal_event_, sizeof (*terminal_event_));
                if (watcher.server->send (&msg, ZLINK_DONTWAIT) != 0) {
                    msg.close ();
                    continue;
                }
                msg.close ();
            }
        }

        for (size_t i = 0; i < _watchers.size (); ++i) {
            if (_watchers[i].server)
                servers.push_back (_watchers[i].server);
        }
        _watchers.clear ();
        _watcher_count.store (0, std::memory_order_release);
    }

    close_monitor_servers (_ctx, servers, 2000);
}

size_t service_monitor_hub_t::watcher_count () const
{
    return _watcher_count.load (std::memory_order_acquire);
}

void service_monitor_hub_t::prune_closed_watchers ()
{
    std::vector<socket_base_t *> stale_servers;
    {
        scoped_lock_t lock (_sync);
        for (std::vector<watcher_t>::iterator it = _watchers.begin ();
             it != _watchers.end ();) {
            if (!it->server) {
                it = _watchers.erase (it);
                continue;
            }

            zlink_monitor_snapshot_t snapshot;
            memset (&snapshot, 0, sizeof (snapshot));
            if (it->server->monitor_snapshot (&snapshot) == 0
                && snapshot.ready_count == 0) {
                stale_servers.push_back (it->server);
                it = _watchers.erase (it);
                continue;
            }

            ++it;
        }
        _watcher_count.store (_watchers.size (), std::memory_order_release);
    }

    close_monitor_servers (NULL, stale_servers, 0);
}

void service_monitor_hub_t::dispatch_thread_main (void *arg_)
{
    static_cast<service_monitor_hub_t *> (arg_)->dispatch_loop ();
}

void service_monitor_hub_t::ensure_dispatch_thread_started ()
{
    scoped_lock_t lock (_dispatch_sync);
    if (_dispatch_thread_started)
        return;
    _dispatch_stop = false;
    _dispatch_thread.start (&service_monitor_hub_t::dispatch_thread_main, this,
                            "svc-monitor");
    _dispatch_thread_started = true;
}

void service_monitor_hub_t::dispatch_loop ()
{
    _dispatch_sync.lock ();
    while (true) {
        if (_dispatch_stop)
            break;
        if (_dispatch_queue.empty ()) {
            (void) _dispatch_cv.wait (&_dispatch_sync, -1);
            continue;
        }

        zlink_service_event_t event = _dispatch_queue.front ();
        _dispatch_queue.pop_front ();
        _dispatch_sync.unlock ();
        dispatch_event (event);
        _dispatch_sync.lock ();
    }
    _dispatch_sync.unlock ();
}

void service_monitor_hub_t::dispatch_event (const zlink_service_event_t &event_)
{
    scoped_lock_t lock (_sync);
    const uint32_t delivery_mask = event_delivery_mask (event_);
    msg_t payload;
    const bool payload_ready = payload.init_size (sizeof (event_)) == 0;

    if (payload_ready)
        memcpy (payload.data (), &event_, sizeof (event_));

    for (std::vector<watcher_t>::iterator it = _watchers.begin ();
         it != _watchers.end ();) {
        if (!it->server) {
            it = _watchers.erase (it);
            _watcher_count.store (_watchers.size (), std::memory_order_release);
            continue;
        }
        if ((it->events & delivery_mask) == 0 || !payload_ready) {
            ++it;
            continue;
        }

        msg_t msg;
        if (msg.init () != 0 || msg.copy (payload) != 0) {
            msg.close ();
            ++it;
            continue;
        }

        if (it->server->send (&msg, ZLINK_DONTWAIT) != 0) {
            msg.close ();
            it->server->close ();
            it = _watchers.erase (it);
            _watcher_count.store (_watchers.size (), std::memory_order_release);
            continue;
        }

        msg.close ();
        ++it;
    }

    if (payload_ready)
        payload.close ();
}

void service_monitor_hub_t::stop_dispatch_thread ()
{
    {
        scoped_lock_t lock (_dispatch_sync);
        _dispatch_stop = true;
        _dispatch_queue.clear ();
        _dispatch_cv.broadcast ();
    }

    if (_dispatch_thread_started) {
        _dispatch_thread.stop ();
        _dispatch_thread_started = false;
    }
}

bool service_monitor_hub_t::has_watchers () const
{
    return _watcher_count.load (std::memory_order_acquire) != 0;
}
}
