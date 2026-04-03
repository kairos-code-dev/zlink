/* SPDX-License-Identifier: MPL-2.0 */

#include "utils/precompiled.hpp"

#include "api/monitor_api_internal.hpp"

#include <map>

#include "api/socket_api_internal.hpp"
#include "core/ctx.hpp"
#include "services/control/service_control_runtime.hpp"
#include "utils/mutex.hpp"

namespace
{
struct monitor_handler_registry_t
{
    zlink::mutex_t sync;
    std::map<zlink::socket_base_t *, monitor_handler_state_t *> handlers;
};

monitor_handler_registry_t &monitor_handler_registry ()
{
    static monitor_handler_registry_t registry;
    return registry;
}

void stop_monitor_handler_state (monitor_handler_state_t *state_)
{
    if (!state_)
        return;

    state_->stop.store (true, std::memory_order_release);
    if (state_->socket) {
        zlink::service_control_runtime_t *runtime =
          state_->socket->get_ctx ()->service_control_runtime ();
        if (runtime && state_->dispatch_task_id != 0)
            (void) runtime->remove_task (state_->dispatch_task_id);
    }
    delete state_;
}

void stop_monitor_handler_worker (monitor_handler_state_t *state_)
{
    if (!state_)
        return;

    state_->stop.store (true, std::memory_order_release);
    if (state_->socket)
        state_->socket->stop ();
    if (state_->socket) {
        zlink::service_control_runtime_t *runtime =
          state_->socket->get_ctx ()->service_control_runtime ();
        if (runtime && state_->dispatch_task_id != 0)
            (void) runtime->remove_task (state_->dispatch_task_id);
    }
}

void erase_monitor_handler_state (zlink::socket_base_t *socket_,
                                  monitor_handler_state_t *state_)
{
    if (!socket_ || !state_)
        return;

    monitor_handler_registry_t &registry = monitor_handler_registry ();
    zlink::scoped_lock_t lock (registry.sync);
    std::map<zlink::socket_base_t *, monitor_handler_state_t *>::iterator it =
      registry.handlers.find (socket_);
    if (it != registry.handlers.end () && it->second == state_)
        registry.handlers.erase (it);
}

void finalize_monitor_handler_self_close (monitor_handler_state_t *state_)
{
    if (!state_)
        return;

    zlink::socket_base_t *socket = state_->socket;
    state_->stop.store (true, std::memory_order_release);
    erase_monitor_handler_state (socket, state_);
    state_->socket = NULL;
    if (socket) {
        socket->stop ();
        socket->close ();
    }
    delete state_;
}

void monitor_handler_task (void *arg_)
{
    monitor_handler_state_t *state_ =
      static_cast<monitor_handler_state_t *> (arg_);
    if (!state_ || !state_->socket)
        return;

    void *monitor_socket = static_cast<void *> (state_->socket);
    while (!state_->stop.load (std::memory_order_acquire)) {
        if (!state_->service) {
            zlink_monitor_event_t event;
            const int rc = recv_socket_monitor_event_unchecked (
              monitor_socket, &event, ZLINK_DONTWAIT);
            if (rc != 0)
                break;

            zlink_monitor_handler_fn handler = NULL;
            void *handler_userdata = NULL;
            {
                zlink::scoped_lock_t lock (state_->dispatch_sync);
                if (state_->stop.load (std::memory_order_acquire)
                    || state_->close_requested.load (
                         std::memory_order_acquire)) {
                    break;
                }

                handler =
                  state_->socket_handler.load (std::memory_order_acquire);
                if (!handler)
                    break;

                handler_userdata = state_->socket_handler_userdata.load (
                  std::memory_order_acquire);
                state_->callback_depth.fetch_add (
                  1, std::memory_order_acq_rel);
            }
            monitor_handler_state_t *previous =
              g_current_monitor_handler_state;
            g_current_monitor_handler_state = state_;
            handler (&event, handler_userdata);
            const int depth_after =
              state_->callback_depth.fetch_sub (
                1, std::memory_order_acq_rel)
              - 1;
            g_current_monitor_handler_state = previous;
            if (state_->close_requested.load (std::memory_order_acquire)
                && depth_after == 0) {
                finalize_monitor_handler_self_close (state_);
                return;
            }
        } else {
            zlink_service_event_t event;
            const int rc = recv_service_monitor_event_unchecked (
              monitor_socket, &event, ZLINK_DONTWAIT);
            if (rc != 0)
                break;

            zlink_service_monitor_handler_fn handler = NULL;
            void *handler_userdata = NULL;
            {
                zlink::scoped_lock_t lock (state_->dispatch_sync);
                if (state_->stop.load (std::memory_order_acquire)
                    || state_->close_requested.load (
                         std::memory_order_acquire)) {
                    break;
                }

                handler =
                  state_->service_handler.load (std::memory_order_acquire);
                if (!handler)
                    break;

                handler_userdata =
                  state_->service_handler_userdata.load (
                    std::memory_order_acquire);
                state_->callback_depth.fetch_add (
                  1, std::memory_order_acq_rel);
            }
            monitor_handler_state_t *previous =
              g_current_monitor_handler_state;
            g_current_monitor_handler_state = state_;
            handler (&event, handler_userdata);
            const int depth_after =
              state_->callback_depth.fetch_sub (
                1, std::memory_order_acq_rel)
              - 1;
            g_current_monitor_handler_state = previous;
            if (state_->close_requested.load (std::memory_order_acquire)
                && depth_after == 0) {
                finalize_monitor_handler_self_close (state_);
                return;
            }
        }
    }
}
}

thread_local monitor_handler_state_t *g_current_monitor_handler_state = NULL;

int require_monitor_recv_model (void *monitor_, bool service_)
{
    if (!monitor_) {
        errno = EFAULT;
        return -1;
    }

    socket_handle_t handle = as_socket_handle (monitor_);
    if (!handle.socket)
        return -1;

    monitor_handler_state_t *state = find_monitor_handler_state (handle.socket);
    if (!state || state->service != service_) {
        errno = EINVAL;
        return -1;
    }

    if (state->socket_handler.load (std::memory_order_acquire)
        || state->service_handler.load (std::memory_order_acquire)) {
        errno = EBUSY;
        return -1;
    }
    return 0;
}

namespace zlink
{
void *current_monitor_dispatch_handle ()
{
    return g_current_monitor_handler_state
             ? static_cast<void *> (g_current_monitor_handler_state->socket)
             : NULL;
}
}

monitor_handler_state_t *find_monitor_handler_state (zlink::socket_base_t *socket_)
{
    monitor_handler_registry_t &registry = monitor_handler_registry ();
    zlink::scoped_lock_t lock (registry.sync);
    std::map<zlink::socket_base_t *, monitor_handler_state_t *>::iterator it =
      registry.handlers.find (socket_);
    return it != registry.handlers.end () ? it->second : NULL;
}

bool has_open_service_monitor_for_subject (void *snapshot_subject_)
{
    if (!snapshot_subject_)
        return false;

    monitor_handler_registry_t &registry = monitor_handler_registry ();
    zlink::scoped_lock_t lock (registry.sync);
    for (std::map<zlink::socket_base_t *, monitor_handler_state_t *>::iterator it =
           registry.handlers.begin ();
         it != registry.handlers.end (); ++it) {
        monitor_handler_state_t *state = it->second;
        if (!state || !state->service)
            continue;
        if (state->snapshot_subject.load (std::memory_order_acquire)
            == snapshot_subject_) {
            return true;
        }
    }
    return false;
}

zlink::socket_base_t *raw_monitor_snapshot_subject (
  monitor_handler_state_t *state_)
{
    if (!state_ || state_->service)
        return NULL;

    monitor_snapshot_provider_fn provider =
      state_->snapshot_provider.load (std::memory_order_acquire);
    if (provider != &socket_monitor_snapshot_provider)
        return NULL;

    return static_cast<zlink::socket_base_t *> (
      state_->snapshot_subject.load (std::memory_order_acquire));
}

void clear_raw_monitor_snapshot_subjects (zlink::socket_base_t *source_)
{
    if (!source_)
        return;

    monitor_handler_registry_t &registry = monitor_handler_registry ();
    zlink::scoped_lock_t lock (registry.sync);
    for (std::map<zlink::socket_base_t *, monitor_handler_state_t *>::iterator it =
           registry.handlers.begin ();
         it != registry.handlers.end (); ++it) {
        monitor_handler_state_t *state = it->second;
        if (!state || state->service)
            continue;

        monitor_snapshot_provider_fn provider =
          state->snapshot_provider.load (std::memory_order_acquire);
        if (provider != &socket_monitor_snapshot_provider)
            continue;

        if (state->snapshot_subject.load (std::memory_order_acquire) == source_)
            state->snapshot_subject.store (NULL, std::memory_order_release);
    }
}

void unregister_monitor_handlers (zlink::socket_base_t *socket_)
{
    if (!socket_)
        return;

    monitor_handler_registry_t &registry = monitor_handler_registry ();
    monitor_handler_state_t *state = NULL;
    {
        zlink::scoped_lock_t lock (registry.sync);
        std::map<zlink::socket_base_t *, monitor_handler_state_t *>::iterator it =
          registry.handlers.find (socket_);
        if (it == registry.handlers.end ())
            return;
        state = it->second;
        registry.handlers.erase (it);
    }

    stop_monitor_handler_state (state);
}

int set_monitor_handler_state (zlink::socket_base_t *socket_,
                               zlink_monitor_handler_fn socket_handler_,
                               zlink_service_monitor_handler_fn service_handler_,
                               bool service_,
                               monitor_snapshot_provider_fn snapshot_provider_,
                               void *snapshot_subject_,
                               void *socket_handler_userdata_,
                               void *service_handler_userdata_)
{
    if (!socket_) {
        errno = EINVAL;
        return -1;
    }

    monitor_handler_registry_t &registry = monitor_handler_registry ();
    monitor_handler_state_t *state = NULL;
    {
        zlink::scoped_lock_t lock (registry.sync);
        std::map<zlink::socket_base_t *, monitor_handler_state_t *>::iterator it =
          registry.handlers.find (socket_);
        if (it == registry.handlers.end ()) {
            state = new (std::nothrow) monitor_handler_state_t (socket_, service_);
            if (!state) {
                errno = ENOMEM;
                return -1;
            }
            registry.handlers[socket_] = state;
        } else {
            state = it->second;
            if (state->service != service_) {
                errno = EINVAL;
                return -1;
            }
        }
    }

    state->socket_handler.store (socket_handler_, std::memory_order_release);
    state->service_handler.store (service_handler_, std::memory_order_release);
    state->socket_handler_userdata.store (socket_handler_userdata_,
                                          std::memory_order_release);
    state->service_handler_userdata.store (service_handler_userdata_,
                                           std::memory_order_release);
    state->snapshot_provider.store (snapshot_provider_,
                                    std::memory_order_release);
    state->snapshot_subject.store (snapshot_subject_, std::memory_order_release);
    if ((socket_handler_ || service_handler_) && state->dispatch_task_id == 0) {
        zlink::service_control_runtime_t *runtime =
          socket_->get_ctx ()->service_control_runtime ();
        if (!runtime) {
            errno = ETERM;
            return -1;
        }
        state->dispatch_task_id =
          runtime->add_periodic_task (&monitor_handler_task, state, 10, true);
        if (state->dispatch_task_id == 0)
            return -1;
    }
    return 0;
}

int zlink_monitor_close (void **monitor_p_)
{
    if (!monitor_p_ || !*monitor_p_) {
        errno = EFAULT;
        return -1;
    }
    void *monitor = *monitor_p_;
    socket_handle_t handle = as_socket_handle (monitor);
    if (!handle.socket)
        return -1;

    zlink::socket_base_t *socket = handle.socket;
    const int linger = 0;
    (void) socket->setsockopt (ZLINK_INTERNAL_OPT_LINGER, &linger,
                               sizeof (linger));
    monitor_handler_state_t *monitor_state = find_monitor_handler_state (socket);
    const bool had_dispatch_monitor =
      monitor_state
      && (monitor_state->socket_handler.load (std::memory_order_acquire)
            || monitor_state->service_handler.load (std::memory_order_acquire));
    const bool no_dispatch_monitor =
      monitor_state
      && !monitor_state->socket_handler.load (std::memory_order_acquire)
      && !monitor_state->service_handler.load (std::memory_order_acquire);
    bool stop_socket_before_close = no_dispatch_monitor;
    if (monitor_state) {
        zlink::scoped_lock_t dispatch_lock (monitor_state->dispatch_sync);
        if (had_dispatch_monitor) {
            monitor_state->socket_handler.store (NULL,
                                                 std::memory_order_release);
            monitor_state->service_handler.store (NULL,
                                                  std::memory_order_release);
            if (g_current_monitor_handler_state != monitor_state) {
                monitor_state->stop.store (true, std::memory_order_release);
                stop_socket_before_close = true;
            }
        }
    }
    if (monitor_state && g_current_monitor_handler_state == monitor_state) {
        const int rc = zlink_close (monitor);
        if (rc == 0)
            *monitor_p_ = NULL;
        return rc;
    }
    if (stop_socket_before_close) {
        socket->stop ();
    }
    const int rc = zlink_close (monitor);
    if (rc == 0)
        *monitor_p_ = NULL;
    return rc;
}
