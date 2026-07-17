/* SPDX-License-Identifier: MPL-2.0 */

#include "utils/precompiled.hpp"
#define ZLINK_TYPE_UNSAFE

#include "utils/macros.hpp"
#include "utils/random.hpp"

#if !defined ZLINK_HAVE_WINDOWS
#include <unistd.h>
#ifdef ZLINK_HAVE_VXWORKS
#include <strings.h>
#endif
#endif

// XSI vector I/O
#if defined ZLINK_HAVE_UIO
#include <sys/uio.h>
#else
struct iovec
{
    void *iov_base;
    size_t iov_len;
};
#endif

#include <string.h>
#include <stdlib.h>
#include <new>

#include "sockets/proxy/proxy.hpp"
#include "sockets/common/socket_base.hpp"
#include "api/monitoring/monitor_api_internal.hpp"
#include "api/socket/part_helper_internal.hpp"
#include "api/monitoring/poller_api_internal.hpp"
#include "api/mesh/mesh_api_internal.hpp"
#include "api/socket/socket_request_reply_router_state_internal.hpp"
#include "api/socket/socket_api_internal.hpp"
#include "api/mesh/mesh_stream_session_internal.hpp"
#include "api/socket/socket_request_reply_internal.hpp"
#include "api/core/close_result_internal.hpp"
#include "api/core/config_result_internal.hpp"
#include "api/core/zlink_option_internal.hpp"
#include "utils/mutex.hpp"
#include "utils/stdint.hpp"
#include "utils/config.hpp"
#include "utils/clock.hpp"
#include "utils/sleep.hpp"
#include "core/ctx.hpp"
#include "utils/err.hpp"
#include "core/socket_poller.hpp"
#include "utils/fd.hpp"
#include "protocol/metadata.hpp"
#include "utils/ip.hpp"
#include "core/address.hpp"

#ifdef ZLINK_HAVE_PPOLL
#include "utils/polling_util.hpp"
#include <sys/select.h>
#endif

//  Compile time check whether msg_t fits into zlink_msg_t.
typedef char check_msg_t_size[sizeof (zlink::msg_t) == sizeof (zlink_msg_t) ? 1 : -1];

//  Forward declarations for internal API functions
zlink_config_result_t zlink_msg_init_buffer (zlink_msg_t *msg_, const void *buf_, size_t size_);
int zlink_ctx_set_ext (void *ctx_, int option_, const void *optval_, size_t optvallen_);
zlink_config_result_t zlink_set_subscription (void *handle_, const char *filter_);
zlink_config_result_t zlink_unset_subscription (void *handle_, const char *filter_);
static void *create_socket_handle (void *ctx_, zlink_socket_type_t type_)
{
    if (!ctx_ || !(static_cast<zlink::ctx_t *> (ctx_))->check_tag ()) {
        errno = EFAULT;
        return NULL;
    }

    const int core_type = core_socket_type_from_public_type (type_);
    if (!is_send_only_socket_type (core_type)) {
        switch (core_type) {
            case ZLINK_CORE_SOCKET_PAIR:
            case ZLINK_CORE_SOCKET_DEALER:
            case ZLINK_CORE_SOCKET_ROUTER:
            case ZLINK_CORE_SOCKET_STREAM:
            case ZLINK_CORE_SOCKET_SUB:
            case ZLINK_CORE_SOCKET_XSUB:
            case ZLINK_CORE_SOCKET_XPUB:
                break;
            default:
                errno = EINVAL;
                return NULL;
        }
    }

    zlink::ctx_t *ctx = static_cast<zlink::ctx_t *> (ctx_);
    zlink::socket_base_t *socket = ctx->create_socket (core_type);
    if (!socket)
        return NULL;

    return static_cast<void *> (socket);
}

extern "C" void zlink_socket_request_reply_cleanup (void *socket_);

void *zlink_socket (void *ctx_, zlink_socket_type_t type_)
{
    return create_socket_handle (ctx_, type_);
}

zlink_close_result_t zlink_close (void *s_)
{
    socket_handle_t handle = as_socket_handle (s_);
    if (!handle.socket)
        return ZLINK_CLOSE_INVALID_HANDLE;
    if (zlink::mesh::stream_session_owns_socket (s_)) {
        errno = EBUSY;
        return ZLINK_CLOSE_BUSY;
    }

    std::shared_ptr<zlink::socket_reqrep_internal::socket_request_reply_state_t>
      request_reply_state = zlink::socket_reqrep_internal::find_request_reply_state (handle);
    std::shared_ptr<zlink::reqrep_internal::router_request_reply_state_t>
      router_spot_state = handle.socket->router_request_reply_state ();
    if (zlink::socket_reqrep_internal::in_socket_request_completion_callback (s_)) {
        errno = EBUSY;
        return ZLINK_CLOSE_BUSY;
    }
    if (zlink::reqrep_internal::in_request_completion_callback (s_)) {
        errno = EBUSY;
        return ZLINK_CLOSE_BUSY;
    }
    //  The pin keeps the state alive while this close inspects it. It must be
    //  released before unregister_monitor_handlers: the unregister path waits
    //  for reader pins to drain before deleting the state.
    zlink::socket_base_t *raw_monitor_source = NULL;
    {
        monitor_state_pin_t monitor_pin (handle.socket);
        monitor_handler_state_t *monitor_state = monitor_pin.get ();
        raw_monitor_source = raw_monitor_snapshot_subject (monitor_state);
        if (monitor_state) {
            if (zlink::current_monitor_handler_state () == monitor_state) {
                monitor_state->close_requested.store (true, std::memory_order_release);
                return ZLINK_CLOSE_OK;
            }

            const bool monitor_dispatch_detached =
              !monitor_state->socket_handler.load (std::memory_order_acquire);
            if (monitor_state->close_requested.load (std::memory_order_acquire)
                || monitor_state->callback_depth.load (std::memory_order_acquire) > 0) {
                if (!monitor_dispatch_detached) {
                    errno = EBUSY;
                    return ZLINK_CLOSE_BUSY;
                }
            }
        }

        if (raw_monitor_source && raw_monitor_source != handle.socket) {
            monitor_state->snapshot_subject.store (NULL, std::memory_order_release);
            (void) raw_monitor_source->monitor (NULL, 0, 3, ZLINK_CORE_SOCKET_PAIR);
        } else {
            clear_raw_monitor_snapshot_subjects (handle.socket);
        }
    }

    unregister_monitor_handlers (handle.socket);
    zlink::part_helper_internal::cleanup_handle (s_);

    if (handle.socket->api_sync_mutex ()) {
        if (handle.socket->socket_msg_dispatch_active ()) {
            if (zlink::socket_base_t::current_socket_msg_dispatch_socket () == handle.socket) {
                errno = EBUSY;
                return ZLINK_CLOSE_BUSY;
            }
            (void) handle.socket->socket_msg_dispatch_stop ();
        }
        if (handle.socket->stream_dispatch_in_callback ()) {
            errno = EBUSY;
            return ZLINK_CLOSE_BUSY;
        }

        handle.socket->stop ();
        stream_api_lock_t api_lock (handle);
        (void) handle.socket->stream_dispatch_stop ();
        if (zlink::socket_reqrep_internal::drain_close_request_reply_socket (handle) != 0) {
            return zlink::close_result_internal::from_rc (-1);
        }
        if (zlink::reqrep_internal::drain_close_router_request_reply_state (s_) != 0) {
            return zlink::close_result_internal::from_rc (-1);
        }
        zlink_socket_request_reply_cleanup (s_);
        zlink::reqrep_internal::cleanup_router_request_reply_state (s_);
        const int rc = handle.socket->close ();
        return zlink::close_result_internal::from_rc (rc);
    }

    if (handle.socket->socket_msg_dispatch_active ()) {
        if (zlink::socket_base_t::current_socket_msg_dispatch_socket () == handle.socket) {
            errno = EBUSY;
            return ZLINK_CLOSE_BUSY;
        }
        (void) handle.socket->socket_msg_dispatch_stop ();
    }

    (void) handle.socket->stream_dispatch_stop ();
    if (zlink::socket_reqrep_internal::drain_close_request_reply_socket (handle) != 0) {
        return zlink::close_result_internal::from_rc (-1);
    }
    if (zlink::reqrep_internal::drain_close_router_request_reply_state (s_) != 0) {
        return zlink::close_result_internal::from_rc (-1);
    }
    zlink_socket_request_reply_cleanup (s_);
    zlink::reqrep_internal::cleanup_router_request_reply_state (s_);
    const int rc = handle.socket->close ();
    return zlink::close_result_internal::from_rc (rc);
}

zlink_config_result_t
zlink_poller_add (void *poller_, void *socket_, void *user_data_, short events_)
{
    poller_handle_t *poller = as_poller_handle (poller_);
    if (!poller)
        return ZLINK_CONFIG_INVALID_HANDLE;
    if ((events_ & ZLINK_POLLCOMPLETION) != 0 && events_ != ZLINK_POLLCOMPLETION) {
        errno = EINVAL;
        return ZLINK_CONFIG_INVALID_ARGUMENT;
    }
    const zlink::option_target_t target = zlink::resolve_option_target (socket_);
    if (target.kind == zlink::option_target_service) {
        errno = 0;
        return zlink::config_result_internal::from_rc (
          zlink::mesh::poller_add (poller_, socket_, user_data_, events_));
    }
    if (target.kind != zlink::option_target_socket) {
        errno = EFAULT;
        return ZLINK_CONFIG_INVALID_HANDLE;
    }
    socket_handle_t handle = make_socket_handle (target.socket);
    const int type = socket_type (handle);
    if (events_ == ZLINK_POLLCOMPLETION) {
        if (type != ZLINK_CORE_SOCKET_DEALER && type != ZLINK_CORE_SOCKET_ROUTER) {
            errno = EINVAL;
            return ZLINK_CONFIG_INVALID_ARGUMENT;
        }

        std::shared_ptr<zlink::socket_reqrep_internal::socket_request_reply_state_t> socket_state =
          zlink::socket_reqrep_internal::find_or_create_request_reply_state (handle);
        if (!socket_state
            || zlink::socket_reqrep_internal::ensure_completion_queue_ready (socket_state) != 0) {
            const int err = errno ? errno : EFAULT;
            errno = err;
            return zlink::config_result_internal::from_errno (err);
        }
        if (poller_add_hidden_completion_registration (
              poller, zlink::socket_reqrep_internal::completion_signal_socket (socket_state),
              socket_, poller_subject_socket_request_completion, &socket_state->completion,
              socket_state)
            != 0) {
            const int err = errno ? errno : EFAULT;
            errno = err;
            return zlink::config_result_internal::from_errno (err);
        }

        if (type == ZLINK_CORE_SOCKET_ROUTER) {
            std::shared_ptr<zlink::reqrep_internal::router_request_reply_state_t>
              router_state = zlink::reqrep_internal::find_or_create_router_state (socket_);
            if (!router_state
                || zlink::reqrep_internal::ensure_router_completion_queue_ready (router_state)
                     != 0) {
                const int err = errno ? errno : EFAULT;
                (void) poller_remove_all_registrations_for_subject (poller, socket_);
                errno = err;
                return zlink::config_result_internal::from_errno (err);
            }
            if (poller_add_hidden_completion_registration (
                  poller,
                  zlink::reqrep_internal::router_completion_signal_socket (router_state),
                  socket_, poller_subject_router_request_completion, &router_state->completion,
                  router_state)
                != 0) {
                const int err = errno ? errno : EFAULT;
                (void) poller_remove_all_registrations_for_subject (poller, socket_);
                errno = err;
                return zlink::config_result_internal::from_errno (err);
            }
        }

        return ZLINK_CONFIG_OK;
    }
    if (validate_socket_callback_poller_events (handle, events_) != 0)
        return ZLINK_CONFIG_INVALID_ARGUMENT;
    if (poller_add_registration (poller, handle.socket, user_data_, events_, socket_,
                                 poller_subject_none)
        != 0) {
        return zlink::config_result_internal::from_errno (errno);
    }

    if (type == ZLINK_CORE_SOCKET_DEALER || type == ZLINK_CORE_SOCKET_ROUTER) {
        std::shared_ptr<zlink::socket_reqrep_internal::socket_request_reply_state_t> state =
          zlink::socket_reqrep_internal::find_or_create_request_reply_state (handle);
        if (!state || zlink::socket_reqrep_internal::ensure_completion_queue_ready (state) != 0) {
            const int err = errno;
            (void) poller_remove_all_registrations_for_subject (poller, socket_);
            errno = err;
            return zlink::config_result_internal::from_errno (err);
        }
        if (poller_add_hidden_completion_registration (
              poller, zlink::socket_reqrep_internal::completion_signal_socket (state), socket_,
              poller_subject_socket_request_completion, &state->completion, state)
            != 0) {
            const int err = errno;
            (void) poller_remove_all_registrations_for_subject (poller, socket_);
            errno = err;
            return zlink::config_result_internal::from_errno (err);
        }
    }

    if (type == ZLINK_CORE_SOCKET_ROUTER) {
        std::shared_ptr<zlink::reqrep_internal::router_request_reply_state_t> state =
          zlink::reqrep_internal::find_or_create_router_state (socket_);
        if (!state
            || zlink::reqrep_internal::ensure_router_completion_queue_ready (state) != 0) {
            const int err = errno;
            (void) poller_remove_all_registrations_for_subject (poller, socket_);
            errno = err;
            return zlink::config_result_internal::from_errno (err);
        }
        if (poller_add_hidden_completion_registration (
              poller, zlink::reqrep_internal::router_completion_signal_socket (state), socket_,
              poller_subject_router_request_completion, &state->completion, state)
            != 0) {
            const int err = errno;
            (void) poller_remove_all_registrations_for_subject (poller, socket_);
            errno = err;
            return zlink::config_result_internal::from_errno (err);
        }
    }

    return ZLINK_CONFIG_OK;
}

zlink_config_result_t zlink_poller_modify (void *poller_, void *socket_, short events_)
{
    poller_handle_t *poller = as_poller_handle (poller_);
    if (!poller)
        return ZLINK_CONFIG_INVALID_HANDLE;
    if ((events_ & ZLINK_POLLCOMPLETION) != 0) {
        errno = EINVAL;
        return ZLINK_CONFIG_INVALID_ARGUMENT;
    }
    const zlink::option_target_t target = zlink::resolve_option_target (socket_);
    if (target.kind == zlink::option_target_service) {
        errno = 0;
        return zlink::config_result_internal::from_rc (
          zlink::mesh::poller_modify (poller_, socket_, events_));
    }
    if (target.kind != zlink::option_target_socket) {
        errno = EFAULT;
        return ZLINK_CONFIG_INVALID_HANDLE;
    }
    socket_handle_t handle = make_socket_handle (target.socket);
    if (validate_socket_callback_poller_events (handle, events_) != 0)
        return ZLINK_CONFIG_INVALID_ARGUMENT;
    const int index = poller_find_registration_index (poller, socket_);
    if (index < 0) {
        errno = EINVAL;
        return ZLINK_CONFIG_INVALID_ARGUMENT;
    }
    return zlink::config_result_internal::from_rc (poller->poller.modify (
      static_cast<zlink::socket_base_t *> (poller->registrations[index].socket), events_));
}

zlink_config_result_t zlink_poller_remove (void *poller_, void *socket_)
{
    poller_handle_t *poller = as_poller_handle (poller_);
    if (!poller)
        return ZLINK_CONFIG_INVALID_HANDLE;
    const zlink::option_target_t target = zlink::resolve_option_target (socket_);
    if (target.kind == zlink::option_target_service) {
        errno = 0;
        return zlink::config_result_internal::from_rc (
          zlink::mesh::poller_remove (poller_, socket_));
    }
    if (target.kind != zlink::option_target_socket) {
        errno = EFAULT;
        return ZLINK_CONFIG_INVALID_HANDLE;
    }
    return zlink::config_result_internal::from_rc (
      poller_remove_all_registrations_for_subject (poller, socket_));
}
