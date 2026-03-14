/* SPDX-License-Identifier: MPL-2.0 */

#include "precompiled.hpp"

#include "services/common/service_monitor.hpp"

#include "utils/random.hpp"

#include <stdio.h>
#include <string.h>

namespace zlink
{
service_monitor_hub_t::service_monitor_hub_t (ctx_t *ctx_) :
    _ctx (ctx_),
    _next_id (generate_random ())
{
}

service_monitor_hub_t::~service_monitor_hub_t ()
{
    close_all ();
}

static void set_monitor_socket_defaults (socket_base_t *socket_)
{
    if (!socket_)
        return;

    const int linger = 0;
    const int hwm = 1024;
    socket_->setsockopt (ZLINK_LINGER, &linger, sizeof (linger));
    socket_->setsockopt (ZLINK_SNDHWM, &hwm, sizeof (hwm));
    socket_->setsockopt (ZLINK_RCVHWM, &hwm, sizeof (hwm));
}

static bool recv_monitor_handshake (socket_base_t *socket_, long timeout_ms_)
{
    const int timeout = static_cast<int> (timeout_ms_);
    if (socket_->setsockopt (ZLINK_RCVTIMEO, &timeout, sizeof (timeout)) != 0)
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

    socket_base_t *server = _ctx->create_socket (ZLINK_PAIR);
    socket_base_t *client = _ctx->create_socket (ZLINK_PAIR);
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

    scoped_lock_t lock (_sync);
    _watchers.push_back (watcher);
    return static_cast<void *> (client);
}

uint32_t service_monitor_hub_t::event_delivery_mask (
  const zlink_service_event_t &event_)
{
    uint32_t mask = event_.event_type;
    switch (event_.event_type) {
        case ZLINK_DISCOVERY_SERVICE_UP:
        case ZLINK_GATEWAY_SERVICE_READY:
        case ZLINK_SPOT_SUB_SUBSCRIPTION_READY:
            mask |= ZLINK_MONITOR_EVENT_READY;
            break;
        case ZLINK_DISCOVERY_SERVICE_DOWN:
        case ZLINK_GATEWAY_SERVICE_LOST:
            mask |= ZLINK_MONITOR_EVENT_LOST;
            break;
        case ZLINK_GATEWAY_SEND_READY_CHANGED:
            mask |= event_.value > 0 ? ZLINK_MONITOR_EVENT_READY
                                     : ZLINK_MONITOR_EVENT_LOST;
            break;
        case ZLINK_GATEWAY_ROUTE_UP:
            mask |= ZLINK_MONITOR_EVENT_PEER_UP;
            break;
        case ZLINK_GATEWAY_ROUTE_DOWN:
            mask |= ZLINK_MONITOR_EVENT_PEER_DOWN;
            break;
        case ZLINK_SPOT_PUB_QUEUE_FULL:
            mask |= ZLINK_MONITOR_EVENT_ERROR;
            break;
        case ZLINK_SPOT_PUB_DELIVERY_READY_CHANGED:
        case ZLINK_SPOT_SUB_DELIVERY_READY_CHANGED:
            mask |= event_.value > 0 ? ZLINK_MONITOR_EVENT_READY
                                     : ZLINK_MONITOR_EVENT_LOST;
            break;
        case ZLINK_MONITOR_EVENT_CLOSED:
            mask |= ZLINK_MONITOR_EVENT_CLOSED;
            break;
        default:
            break;
    }
    return mask;
}

void service_monitor_hub_t::emit (const zlink_service_event_t &event_)
{
    scoped_lock_t lock (_sync);
    const uint32_t delivery_mask = event_delivery_mask (event_);

    for (std::vector<watcher_t>::iterator it = _watchers.begin ();
         it != _watchers.end ();) {
        if (!it->server) {
            it = _watchers.erase (it);
            continue;
        }
        if ((it->events & delivery_mask) == 0) {
            ++it;
            continue;
        }

        msg_t msg;
        if (msg.init_size (sizeof (event_)) != 0) {
            ++it;
            continue;
        }
        memcpy (msg.data (), &event_, sizeof (event_));
        if (it->server->send (&msg, ZLINK_DONTWAIT) != 0) {
            msg.close ();
            it->server->close ();
            it = _watchers.erase (it);
            continue;
        }
        msg.close ();
        ++it;
    }
}

void service_monitor_hub_t::close_all (const zlink_service_event_t *terminal_event_)
{
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
    }

    for (size_t i = 0; i < servers.size (); ++i) {
        socket_base_t *server = servers[i];
        if (!server)
            continue;
        if (_ctx)
            (void) _ctx->close_socket_and_wait (server, 2000);
        else {
            server->stop ();
            server->close ();
        }
    }
}

bool service_monitor_hub_t::has_watchers () const
{
    scoped_lock_t lock (const_cast<mutex_t &> (_sync));
    return !_watchers.empty ();
}
}
