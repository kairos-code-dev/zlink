/* SPDX-License-Identifier: MPL-2.0 */

#include "utils/precompiled.hpp"

#include "services/common/service_runtime_base.hpp"

#include "sockets/socket_close_ops.hpp"
#include "utils/clock.hpp"

namespace
{
uint64_t compute_deadline_ms (int timeout_ms_)
{
    return timeout_ms_ >= 0 ? zlink::clock_t ().now_ms () + timeout_ms_ : 0;
}

int remaining_timeout_ms (int timeout_ms_, uint64_t deadline_ms_)
{
    if (timeout_ms_ < 0)
        return -1;

    const uint64_t now_ms = zlink::clock_t ().now_ms ();
    if (now_ms >= deadline_ms_) {
        errno = ETIMEDOUT;
        return -1;
    }

    return static_cast<int> (deadline_ms_ - now_ms);
}

int wait_for_closing_sockets (zlink::ctx_t *ctx_,
                              zlink::service_socket_registry_t *registry_,
                              const zlink::service_socket_registry_t::socket_map_t &sockets_,
                              int timeout_ms_,
                              uint64_t deadline_ms_,
                              const char *timeout_debug_prefix_)
{
    for (zlink::service_socket_registry_t::socket_map_t::const_iterator it =
           sockets_.begin ();
         it != sockets_.end (); ++it) {
        const int wait_ms = remaining_timeout_ms (timeout_ms_, deadline_ms_);
        if (timeout_ms_ >= 0 && wait_ms < 0) {
            registry_->debug_dump (timeout_debug_prefix_);
            return -1;
        }

        if (zlink::socket_close_ops_t::wait_until_closed (
              ctx_, it->second, wait_ms)
            != 0) {
            if (errno == ETIMEDOUT)
                registry_->debug_dump (timeout_debug_prefix_);
            return -1;
        }

        registry_->erase_closing_socket (it->first);
    }

    return 0;
}
}

zlink::service_runtime_base_t::service_runtime_base_t (ctx_t *ctx_) :
    _ctx (ctx_),
    _state (service_state_idle),
    _fault_errno (0)
{
}

void zlink::service_runtime_base_t::set_ctx (ctx_t *ctx_)
{
    scoped_lock_t lock (_sync);
    _ctx = ctx_;
}

bool zlink::service_runtime_base_t::transition_to (
  service_lifecycle_state_t target_)
{
    scoped_lock_t lock (_sync);
    bool valid = false;
    switch (target_) {
        case service_state_starting:
            valid = (_state == service_state_idle);
            break;
        case service_state_running:
            valid = (_state == service_state_starting);
            break;
        case service_state_stopping:
            valid = (_state == service_state_running);
            break;
        case service_state_stopped:
            valid = (_state == service_state_stopping);
            break;
        case service_state_faulted:
            valid = (_state == service_state_starting
                     || _state == service_state_running
                     || _state == service_state_stopping);
            break;
        default:
            break;
    }
    if (!valid) {
        errno = EFSM;
        return false;
    }
    _state = target_;
    return true;
}

zlink::service_lifecycle_state_t zlink::service_runtime_base_t::state () const
{
    scoped_lock_t lock (const_cast<mutex_t &> (_sync));
    return _state;
}

bool zlink::service_runtime_base_t::is_running () const
{
    return state () == service_state_running;
}

bool zlink::service_runtime_base_t::is_stopping () const
{
    return state () == service_state_stopping;
}

bool zlink::service_runtime_base_t::is_stopped () const
{
    return state () == service_state_stopped;
}

void zlink::service_runtime_base_t::mark_faulted (int err_)
{
    scoped_lock_t lock (_sync);
    _state = service_state_faulted;
    _fault_errno = err_;
}

int zlink::service_runtime_base_t::fault_errno () const
{
    scoped_lock_t lock (const_cast<mutex_t &> (_sync));
    return _fault_errno;
}

void zlink::service_runtime_base_t::register_socket (socket_base_t *socket_)
{
    _sockets.register_socket (socket_, state () < service_state_stopping);
}

void zlink::service_runtime_base_t::unregister_socket (
  const socket_base_t *socket_)
{
    _sockets.unregister_socket (socket_);
}

int zlink::service_runtime_base_t::close_socket (socket_base_t *&socket_,
                                                 int timeout_ms_)
{
    LIBZLINK_UNUSED (timeout_ms_);
    return _sockets.close_socket (socket_);
}

int zlink::service_runtime_base_t::close_socket_and_wait (
  socket_base_t *&socket_, int timeout_ms_)
{
    const socket_base_t *closed_socket = NULL;
    const int rc = _sockets.close_socket (socket_, &closed_socket);
    if (rc != 0 || !_ctx)
        return rc;

    const int closed_socket_id =
      closed_socket ? closed_socket->socket_id () : -1;
    const int wait_rc =
      socket_close_ops_t::wait_until_closed (_ctx, closed_socket, timeout_ms_);
    if (wait_rc == 0 && closed_socket_id >= 0)
        _sockets.erase_closing_socket (closed_socket_id);
    return wait_rc;
}

int zlink::service_runtime_base_t::wait_drained (int timeout_ms_)
{
    if (!_ctx)
        return 0;

    const uint64_t deadline_ms = compute_deadline_ms (timeout_ms_);
    while (true) {
        size_t owned_count = 0;
        service_socket_registry_t::socket_map_t closing;
        _sockets.snapshot_drain_state (&owned_count, &closing);

        if (owned_count == 0 && closing.empty ())
            return 0;

        if (wait_for_closing_sockets (
              _ctx, &_sockets, closing, timeout_ms_, deadline_ms,
              "[service-drain] timeout")
            != 0)
            return -1;
    }
}

int zlink::service_runtime_base_t::force_wait_remaining (int timeout_ms_)
{
    if (!_ctx)
        return 0;

    const uint64_t deadline_ms = compute_deadline_ms (timeout_ms_);
    while (true) {
        service_socket_registry_t::socket_map_t owned;
        service_socket_registry_t::socket_map_t closing;
        _sockets.handoff_owned_to_closing (&owned, &closing);

        if (owned.empty () && closing.empty ())
            return 0;

        for (service_socket_registry_t::socket_map_t::const_iterator it =
               owned.begin ();
             it != owned.end (); ++it) {
            socket_base_t *socket = const_cast<socket_base_t *> (it->second);
            if (socket)
                socket_close_ops_t::request_close (socket);
        }

        if (wait_for_closing_sockets (
              _ctx, &_sockets, closing, timeout_ms_, deadline_ms,
              "[service-force-drain] timeout")
            != 0)
            return -1;
    }
}

size_t zlink::service_runtime_base_t::owned_socket_count () const
{
    return _sockets.socket_count ();
}

void zlink::service_runtime_base_t::clear_tracked_sockets ()
{
    _sockets.clear ();
}
