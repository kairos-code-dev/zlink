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

#include "sockets/proxy.hpp"
#include "sockets/socket_base.hpp"
#include "api/monitor_api_internal.hpp"
#include "api/part_helper_internal.hpp"
#include "api/poller_api_internal.hpp"
#include "api/service_surface_internal.hpp"
#include "api/service_spot_request_reply_internal.hpp"
#include "api/socket_api_internal.hpp"
#include "api/socket_request_reply_internal.hpp"
#include "api/close_result_internal.hpp"
#include "api/config_result_internal.hpp"
#include "api/zlink_testing.hpp"
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
typedef char
  check_msg_t_size[sizeof (zlink::msg_t) == sizeof (zlink_msg_t) ? 1 : -1];

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
extern "C" void zlink_spot_request_reply_cleanup_router (void *router_);

void *zlink_socket (void *ctx_, zlink_socket_type_t type_)
{
    return create_socket_handle (ctx_, type_);
}

zlink_close_result_t zlink_close (void *s_)
{
    socket_handle_t handle = as_socket_handle (s_);
    if (!handle.socket)
        return ZLINK_CLOSE_INVALID_HANDLE;

    std::shared_ptr<zlink::socket_reqrep_internal::socket_request_reply_state_t>
      request_reply_state =
        zlink::socket_reqrep_internal::find_request_reply_state (handle);
    std::shared_ptr<zlink::spot_reqrep_internal::router_spot_request_reply_state_t>
      router_spot_state = handle.socket->router_spot_request_reply_state ();
    if (zlink::socket_reqrep_internal::in_socket_request_completion_callback (
          s_)) {
        errno = EBUSY;
        return ZLINK_CLOSE_BUSY;
    }
    if (zlink::spot_reqrep_internal::in_spot_request_completion_callback (s_)) {
        errno = EBUSY;
        return ZLINK_CLOSE_BUSY;
    }
    if (zlink::socket_reqrep_internal::has_pending_request_work (
          request_reply_state)
        && !zlink::socket_reqrep_internal::current_thread_is_completion_owner (
          request_reply_state)) {
        errno = EBUSY;
        return ZLINK_CLOSE_BUSY;
    }
    if (zlink::spot_reqrep_internal::has_pending_router_spot_request_work (
          router_spot_state)
        && !zlink::spot_reqrep_internal::current_thread_is_router_completion_owner (
          router_spot_state)) {
        errno = EBUSY;
        return ZLINK_CLOSE_BUSY;
    }

    monitor_handler_state_t *monitor_state =
      find_monitor_handler_state (handle.socket);
    zlink::socket_base_t *raw_monitor_source =
      raw_monitor_snapshot_subject (monitor_state);
    if (monitor_state) {
        if (zlink::current_monitor_handler_state () == monitor_state) {
            monitor_state->close_requested.store (true,
                                                 std::memory_order_release);
            return ZLINK_CLOSE_OK;
        }

        const bool monitor_dispatch_detached =
          !monitor_state->socket_handler.load (std::memory_order_acquire);
        if (monitor_state->close_requested.load (std::memory_order_acquire)
            || monitor_state->callback_depth.load (std::memory_order_acquire)
                 > 0) {
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

    unregister_monitor_handlers (handle.socket);
    zlink::part_helper_internal::cleanup_handle (s_);

    if (handle.socket->api_sync_mutex ()) {
        if (handle.socket->socket_msg_dispatch_active ()) {
            if (zlink::socket_base_t::current_socket_msg_dispatch_socket ()
                == handle.socket) {
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
        if (zlink::socket_reqrep_internal::drain_close_request_reply_socket (
              handle)
            != 0) {
            return zlink::close_result_internal::from_rc (-1);
        }
        if (zlink::spot_reqrep_internal::drain_close_router_spot_request_reply_state (
              s_)
            != 0) {
            return zlink::close_result_internal::from_rc (-1);
        }
        zlink_socket_request_reply_cleanup (s_);
        zlink_spot_request_reply_cleanup_router (s_);
        const int rc = handle.socket->close ();
        return zlink::close_result_internal::from_rc (rc);
    }

    if (handle.socket->socket_msg_dispatch_active ()) {
        if (zlink::socket_base_t::current_socket_msg_dispatch_socket ()
            == handle.socket) {
            errno = EBUSY;
            return ZLINK_CLOSE_BUSY;
        }
        (void) handle.socket->socket_msg_dispatch_stop ();
    }

    (void) handle.socket->stream_dispatch_stop ();
    if (zlink::socket_reqrep_internal::drain_close_request_reply_socket (handle)
        != 0) {
        return zlink::close_result_internal::from_rc (-1);
    }
    if (zlink::spot_reqrep_internal::drain_close_router_spot_request_reply_state (
          s_)
        != 0) {
        return zlink::close_result_internal::from_rc (-1);
    }
    zlink_socket_request_reply_cleanup (s_);
    zlink_spot_request_reply_cleanup_router (s_);
    const int rc = handle.socket->close ();
    return zlink::close_result_internal::from_rc (rc);
}

zlink_config_result_t zlink_poller_add (void *poller_,
                                        void *socket_,
                                        void *user_data_,
                                        short events_)
{
    poller_handle_t *poller = as_poller_handle (poller_);
    if (!poller)
        return ZLINK_CONFIG_INVALID_HANDLE;
    const int service_rc =
      zlink_service_poller_add_internal (poller, socket_, user_data_, events_);
    if (service_rc == 0)
        return ZLINK_CONFIG_OK;
    if (errno != EFAULT)
        return zlink::config_result_internal::from_errno (errno);
    socket_handle_t handle = as_socket_handle (socket_);
    if (!handle.socket)
        return ZLINK_CONFIG_INVALID_HANDLE;
    if (validate_socket_callback_poller_events (handle, events_) != 0)
        return ZLINK_CONFIG_INVALID_ARGUMENT;
    if (poller_add_registration (poller, handle.socket, user_data_, events_,
                                 socket_, poller_subject_none)
        != 0) {
        return zlink::config_result_internal::from_errno (errno);
    }

    const int type = socket_type (handle);
    if (type == ZLINK_CORE_SOCKET_DEALER || type == ZLINK_CORE_SOCKET_ROUTER) {
        std::shared_ptr<zlink::socket_reqrep_internal::socket_request_reply_state_t>
          state =
            zlink::socket_reqrep_internal::find_or_create_request_reply_state (
              handle);
        if (!state
            || zlink::socket_reqrep_internal::ensure_completion_queue_ready (
                 state)
                 != 0
            || poller_add_registration (
                 poller,
                 zlink::socket_reqrep_internal::completion_signal_socket (state),
                 NULL, ZLINK_POLLIN, socket_,
                 poller_subject_socket_request_completion)
                 != 0) {
            const int err = errno;
            (void) poller_remove_all_registrations_for_subject (poller, socket_);
            errno = err;
            return zlink::config_result_internal::from_errno (err);
        }
    }

    if (type == ZLINK_CORE_SOCKET_ROUTER) {
        std::shared_ptr<zlink::spot_reqrep_internal::router_spot_request_reply_state_t>
          state = zlink::spot_reqrep_internal::find_or_create_router_state (
            socket_);
        if (!state
            || zlink::spot_reqrep_internal::ensure_router_completion_queue_ready (
                 state)
                 != 0
            || poller_add_registration (
                 poller,
                 zlink::spot_reqrep_internal::router_completion_signal_socket (
                   state),
                 NULL, ZLINK_POLLIN, socket_,
                 poller_subject_router_spot_request_completion)
                 != 0) {
            const int err = errno;
            (void) poller_remove_all_registrations_for_subject (poller, socket_);
            errno = err;
            return zlink::config_result_internal::from_errno (err);
        }
    }

    return ZLINK_CONFIG_OK;
}

zlink_config_result_t zlink_poller_modify (void *poller_,
                                           void *socket_,
                                           short events_)
{
    poller_handle_t *poller = as_poller_handle (poller_);
    if (!poller)
        return ZLINK_CONFIG_INVALID_HANDLE;
    const int service_rc =
      zlink_service_poller_modify_internal (poller, socket_, events_);
    if (service_rc == 0)
        return ZLINK_CONFIG_OK;
    if (errno != EFAULT)
        return zlink::config_result_internal::from_errno (errno);
    socket_handle_t handle = as_socket_handle (socket_);
    if (!handle.socket)
        return ZLINK_CONFIG_INVALID_HANDLE;
    if (validate_socket_callback_poller_events (handle, events_) != 0)
        return ZLINK_CONFIG_INVALID_ARGUMENT;
    const int index = poller_find_registration_index (poller, socket_);
    if (index < 0) {
        errno = EINVAL;
        return ZLINK_CONFIG_INVALID_ARGUMENT;
    }
    return zlink::config_result_internal::from_rc (
      poller->poller.modify (
        static_cast<zlink::socket_base_t *> (poller->registrations[index].socket),
        events_));
}

zlink_config_result_t zlink_poller_remove (void *poller_, void *socket_)
{
    poller_handle_t *poller = as_poller_handle (poller_);
    if (!poller)
        return ZLINK_CONFIG_INVALID_HANDLE;
    const int service_rc = zlink_service_poller_remove_internal (poller, socket_);
    if (service_rc == 0)
        return ZLINK_CONFIG_OK;
    if (errno != EFAULT)
        return zlink::config_result_internal::from_errno (errno);

    socket_handle_t handle = as_socket_handle (socket_);
    if (!handle.socket)
        return ZLINK_CONFIG_INVALID_HANDLE;
    return zlink::config_result_internal::from_rc (
      poller_remove_all_registrations_for_subject (poller, socket_));
}
