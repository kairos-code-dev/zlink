/* SPDX-License-Identifier: MPL-2.0 */

#ifndef __ZLINK_SERVICES_COMMON_SERVICE_RUNTIME_BASE_HPP_INCLUDED__
#define __ZLINK_SERVICES_COMMON_SERVICE_RUNTIME_BASE_HPP_INCLUDED__

#include "core/ctx.hpp"
#include "services/common/service_socket_registry.hpp"
#include "utils/mutex.hpp"

namespace zlink
{
enum service_lifecycle_state_t
{
    service_state_idle = 0,
    service_state_starting = 1,
    service_state_running = 2,
    service_state_stopping = 3,
    service_state_stopped = 4,
    service_state_faulted = 5
};

class service_runtime_base_t
{
  public:
    explicit service_runtime_base_t (ctx_t *ctx_ = NULL) :
        _ctx (ctx_),
        _state (service_state_idle),
        _fault_errno (0)
    {
    }

    void set_ctx (ctx_t *ctx_)
    {
        scoped_lock_t lock (_sync);
        _ctx = ctx_;
    }

    bool transition_to (service_lifecycle_state_t target_)
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

    service_lifecycle_state_t state () const
    {
        scoped_lock_t lock (const_cast<mutex_t &> (_sync));
        return _state;
    }

    bool is_running () const
    {
        return state () == service_state_running;
    }

    bool is_stopping () const
    {
        return state () == service_state_stopping;
    }

    bool is_stopped () const
    {
        return state () == service_state_stopped;
    }

    void mark_faulted (int err_)
    {
        scoped_lock_t lock (_sync);
        _state = service_state_faulted;
        _fault_errno = err_;
    }

    int fault_errno () const
    {
        scoped_lock_t lock (const_cast<mutex_t &> (_sync));
        return _fault_errno;
    }

    void register_socket (socket_base_t *socket_)
    {
        _sockets.register_socket (socket_, state () < service_state_stopping);
    }

    void unregister_socket (const socket_base_t *socket_)
    {
        _sockets.unregister_socket (socket_);
    }

    int close_socket (socket_base_t *&socket_, int timeout_ms_ = 10000)
    {
        LIBZLINK_UNUSED (timeout_ms_);
        return _sockets.close_socket (socket_);
    }

    int close_socket_and_wait (socket_base_t *&socket_, int timeout_ms_ = 10000)
    {
        return _sockets.close_socket_and_wait (_ctx, socket_, timeout_ms_);
    }

    int wait_drained (int timeout_ms_)
    {
        return _sockets.wait_drained (_ctx, timeout_ms_);
    }

    int force_wait_remaining (int timeout_ms_)
    {
        return _sockets.force_wait_remaining (_ctx, timeout_ms_);
    }

    size_t owned_socket_count () const
    {
        return _sockets.socket_count ();
    }

    void clear_tracked_sockets ()
    {
        _sockets.clear ();
    }

    ctx_t *_ctx;
    service_lifecycle_state_t _state;
    int _fault_errno;
    mutable mutex_t _sync;
    service_socket_registry_t _sockets;

    ZLINK_NON_COPYABLE_NOR_MOVABLE (service_runtime_base_t)
};
}

#endif
