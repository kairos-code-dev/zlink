/* SPDX-License-Identifier: MPL-2.0 */

#include "utils/precompiled.hpp"

#include "utils/err.hpp"
#include "api/service_handle_internal.hpp"
#include "api/service_mode_internal.hpp"
#include "api/service_spot_request_reply_internal.hpp"
#include "api/timer_api_internal.hpp"

#include "services/spot/spot_subject_access.hpp"

int validate_spot_generic_poller_events (short events_, bool *is_pub_out_)
{
    if (!is_pub_out_) {
        errno = EFAULT;
        return -1;
    }
    if (events_ == ZLINK_POLLOUT) {
        *is_pub_out_ = true;
        return 0;
    }
    if (events_ == ZLINK_POLLIN) {
        *is_pub_out_ = false;
        return 0;
    }

    errno = EINVAL;
    return -1;
}

void release_poller_registration (const poller_registration_t &registration_)
{
    switch (registration_.subject_kind) {
        case poller_subject_timer:
            timer_handle_release_poller_ref (
              as_timer_handle (registration_.subject));
            break;
        case poller_subject_fd:
        case poller_subject_socket_request_completion:
        case poller_subject_router_spot_request_completion:
        case poller_subject_spot_request_completion:
            break;
        case poller_subject_spot_pub:
        case poller_subject_spot_sub:
            decrement_spot_poller_ref (
              static_cast<spot_handle_t *> (registration_.subject),
              registration_.events);
            break;
        case poller_subject_spot_node_pub:
        case poller_subject_spot_node_sub:
            decrement_spot_node_poller_ref (
              static_cast<zlink::spot_node_t *> (registration_.subject),
              registration_.events);
            break;
        default:
            break;
    }
}

int increment_spot_subject_poller_ref (void *spot_or_node_, short events_)
{
    const zlink::service_handle_resolution_t resolved =
      zlink::resolve_service_handle (spot_or_node_);

    if (resolved.kind == zlink::service_handle_spot)
        return increment_spot_poller_ref (
          static_cast<spot_handle_t *> (spot_or_node_), events_);
    if (resolved.kind == zlink::service_handle_spot_node) {
        errno = ENOTSUP;
        return -1;
    }
    errno = EFAULT;
    return -1;
}

poller_subject_kind_t poller_spot_pub_kind_for_subject (void *spot_or_node_)
{
    if (zlink::resolve_service_handle (spot_or_node_).kind
        == zlink::service_handle_spot)
        return poller_subject_spot_pub;
    return poller_subject_none;
}

poller_subject_kind_t poller_spot_sub_kind_for_subject (void *spot_or_node_)
{
    if (zlink::resolve_service_handle (spot_or_node_).kind
        == zlink::service_handle_spot)
        return poller_subject_spot_sub;
    return poller_subject_none;
}

poller_subject_kind_t poller_spot_routed_kind_for_subject (void *spot_or_node_)
{
    if (zlink::resolve_service_handle (spot_or_node_).kind
        == zlink::service_handle_spot)
        return poller_subject_spot_routed;
    return poller_subject_none;
}

int zlink_service_poller_add_internal (poller_handle_t *poller_,
                                       void *socket_,
                                       void *user_data_,
                                       short events_)
{
    if (!poller_) {
        errno = EFAULT;
        return -1;
    }

    const zlink::service_handle_resolution_t resolved =
      zlink::resolve_service_handle (socket_);

    if (resolved.kind == zlink::service_handle_spot_pub_side) {
        bool is_pub = false;
        if (validate_spot_generic_poller_events (events_, &is_pub) != 0)
            return -1;
        if (!is_pub) {
            errno = EINVAL;
            return -1;
        }
        zlink::socket_base_t *socket = spot_pub_poller_socket (socket_);
        return socket
                 ? poller_add_registration (poller_, socket, user_data_,
                                            events_, socket_,
                                            poller_subject_none)
                 : -1;
    }
    if (resolved.kind == zlink::service_handle_spot_sub_side) {
        bool is_pub = false;
        if (validate_spot_generic_poller_events (events_, &is_pub) != 0)
            return -1;
        if (is_pub) {
            errno = EINVAL;
            return -1;
        }
        zlink::socket_base_t *socket = spot_sub_poller_socket (socket_);
        return socket
                 ? poller_add_registration (poller_, socket, user_data_,
                                            events_, socket_,
                                            poller_subject_none)
                 : -1;
    }
    if (resolved.kind == zlink::service_handle_spot_node) {
        errno = ENOTSUP;
        return -1;
    }
    if (resolved.kind == zlink::service_handle_spot) {
        bool is_pub = false;
        if (validate_spot_generic_poller_events (events_, &is_pub) != 0)
            return -1;
        if (increment_spot_subject_poller_ref (socket_, events_) != 0)
            return -1;

        zlink::socket_base_t *poll_socket =
          is_pub ? resolve_spot_pub_subject_poller_socket (socket_)
                 : resolve_spot_sub_subject_poller_socket (socket_);
        poller_subject_kind_t subject_kind =
          is_pub ? poller_spot_pub_kind_for_subject (socket_)
                 : poller_spot_sub_kind_for_subject (socket_);
        if (!poll_socket
            || poller_add_registration (poller_, poll_socket, user_data_,
                                        events_, socket_, subject_kind)
                 != 0) {
            poller_registration_t registration;
            registration.subject = socket_;
            registration.subject_kind = subject_kind;
            registration.events = events_;
            release_poller_registration (registration);
            return -1;
        }

        if (!is_pub) {
            std::shared_ptr<zlink::spot_reqrep_internal::spot_request_reply_state_t>
              state = zlink::spot_reqrep_internal::try_find_spot_state (socket_);
            if (state) {
                if (zlink::spot_reqrep_internal::ensure_spot_recv_ready (state)
                    != 0) {
                    const int err = errno;
                    (void) poller_remove_all_registrations_for_subject (
                      poller_, socket_);
                    errno = err;
                    return -1;
                }
                zlink::socket_base_t *routed_socket = NULL;
                zlink::socket_base_t *completion_socket = NULL;
                {
                    std::lock_guard<std::mutex> lock (state->mutex);
                    routed_socket = state->routed_recv_socket;
                }
                completion_socket =
                  zlink::spot_reqrep_internal::spot_completion_signal_socket (
                    state);

                if ((routed_socket
                     && poller_add_registration (poller_, routed_socket,
                                                 user_data_, ZLINK_POLLIN,
                                                 socket_,
                                                 poller_spot_routed_kind_for_subject (
                                                   socket_))
                          != 0)
                    || (completion_socket
                        && poller_add_registration (
                             poller_, completion_socket, user_data_, ZLINK_POLLIN,
                             socket_, poller_subject_spot_request_completion)
                             != 0)) {
                    const int err = errno;
                    (void) poller_remove_all_registrations_for_subject (
                      poller_, socket_);
                    errno = err;
                    return -1;
                }
            }
        }
        return 0;
    }

    errno = EFAULT;
    return -1;
}

int zlink_service_poller_modify_internal (poller_handle_t *poller_,
                                          void *socket_,
                                          short events_)
{
    if (!poller_) {
        errno = EFAULT;
        return -1;
    }

    const zlink::service_handle_resolution_t resolved =
      zlink::resolve_service_handle (socket_);

    if (resolved.kind == zlink::service_handle_spot_pub_side
        || resolved.kind == zlink::service_handle_spot_sub_side) {
        bool is_pub = false;
        if (validate_spot_generic_poller_events (events_, &is_pub) != 0)
            return -1;
        const int index = poller_find_registration_index (poller_, socket_);
        if (index < 0) {
            errno = EINVAL;
            return -1;
        }
        return poller_->poller.modify (
          static_cast<zlink::socket_base_t *> (
            poller_->registrations[index].socket),
          events_);
    }
    if (resolved.kind == zlink::service_handle_spot_node) {
        errno = ENOTSUP;
        return -1;
    }
    if (resolved.kind == zlink::service_handle_spot) {
        bool is_pub = false;
        if (validate_spot_generic_poller_events (events_, &is_pub) != 0)
            return -1;
        const int index = poller_find_registration_index (
          poller_, socket_,
          is_pub ? poller_spot_pub_kind_for_subject (socket_)
                 : poller_spot_sub_kind_for_subject (socket_));
        if (index < 0) {
            errno = EINVAL;
            return -1;
        }
        if (increment_spot_subject_poller_ref (socket_, events_) != 0)
            return -1;
        const short old_events = poller_->registrations[index].events;
        zlink::socket_base_t *socket = static_cast<zlink::socket_base_t *> (
          poller_->registrations[index].socket);
        if (poller_->poller.modify (socket, events_) != 0) {
            decrement_spot_poller_ref (
              static_cast<spot_handle_t *> (socket_), events_);
            return -1;
        }
        decrement_spot_poller_ref (
          static_cast<spot_handle_t *> (socket_), old_events);
        poller_->registrations[index].events = events_;
        return 0;
    }

    errno = EFAULT;
    return -1;
}

int zlink_service_poller_remove_internal (poller_handle_t *poller_,
                                          void *socket_)
{
    if (!poller_) {
        errno = EFAULT;
        return -1;
    }

    const zlink::service_handle_resolution_t resolved =
      zlink::resolve_service_handle (socket_);

    if (resolved.kind == zlink::service_handle_spot_pub_side
        || resolved.kind == zlink::service_handle_spot_sub_side
        || resolved.kind == zlink::service_handle_spot)
        return poller_remove_all_registrations_for_subject (poller_, socket_);

    if (resolved.kind == zlink::service_handle_spot_node) {
        errno = ENOTSUP;
        return -1;
    }

    errno = EFAULT;
    return -1;
}
