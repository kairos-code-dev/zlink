/* SPDX-License-Identifier: MPL-2.0 */

#include "utils/precompiled.hpp"

#include "utils/err.hpp"
#include "api/service/service_handle_internal.hpp"
#include "api/service/service_mode_internal.hpp"
#include "api/spot/service_spot_request_reply_internal.hpp"
#include "api/monitoring/timer_api_internal.hpp"

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

// Accepts ZLINK_POLLIN, ZLINK_POLLOUT, or both. Used by the spot/spot_node
// generic add path which can wire both directions on a single socket so
// bi-directional workloads (e.g. spot↔spot send/recv) can wait once on
// either edge.
int parse_spot_combined_poller_events (short events_,
                                       bool *want_pub_out_,
                                       bool *want_sub_out_)
{
    if (!want_pub_out_ || !want_sub_out_) {
        errno = EFAULT;
        return -1;
    }
    *want_pub_out_ = (events_ & ZLINK_POLLOUT) != 0;
    *want_sub_out_ = (events_ & ZLINK_POLLIN) != 0;
    const short allowed =
      static_cast<short> (ZLINK_POLLIN | ZLINK_POLLOUT);
    if (events_ == 0 || (events_ & ~allowed) != 0) {
        errno = EINVAL;
        return -1;
    }
    return 0;
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
    if (resolved.kind == zlink::service_handle_spot_node)
        return increment_spot_node_poller_ref (
          static_cast<zlink::spot_node_t *> (spot_or_node_), events_);
    errno = EFAULT;
    return -1;
}

poller_subject_kind_t poller_spot_pub_kind_for_subject (void *spot_or_node_)
{
    if (zlink::resolve_service_handle (spot_or_node_).kind
        == zlink::service_handle_spot)
        return poller_subject_spot_pub;
    if (zlink::resolve_service_handle (spot_or_node_).kind
        == zlink::service_handle_spot_node)
        return poller_subject_spot_node_pub;
    return poller_subject_none;
}

poller_subject_kind_t poller_spot_sub_kind_for_subject (void *spot_or_node_)
{
    if (zlink::resolve_service_handle (spot_or_node_).kind
        == zlink::service_handle_spot)
        return poller_subject_spot_sub;
    if (zlink::resolve_service_handle (spot_or_node_).kind
        == zlink::service_handle_spot_node)
        return poller_subject_spot_node_sub;
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
    if (resolved.kind == zlink::service_handle_spot
        || resolved.kind == zlink::service_handle_spot_node) {
        bool want_pub = false;
        bool want_sub = false;
        if (parse_spot_combined_poller_events (events_, &want_pub, &want_sub)
            != 0)
            return -1;

        // Track the side(s) we successfully reffed so we can roll back on
        // a partial failure. increment_spot_subject_poller_ref is called
        // separately for each direction since the underlying ref counters
        // are kept per-side.
        bool reffed_pub = false;
        bool reffed_sub = false;
        if (want_pub) {
            if (increment_spot_subject_poller_ref (socket_, ZLINK_POLLOUT) != 0)
                return -1;
            reffed_pub = true;
        }
        if (want_sub) {
            if (increment_spot_subject_poller_ref (socket_, ZLINK_POLLIN) != 0) {
                if (reffed_pub) {
                    poller_registration_t rollback;
                    rollback.socket = socket_;
                    rollback.fd = zlink::retired_fd;
                    rollback.subject = socket_;
                    rollback.subject_kind =
                      poller_spot_pub_kind_for_subject (socket_);
                    rollback.user_data = NULL;
                    rollback.events = ZLINK_POLLOUT;
                    release_poller_registration (rollback);
                }
                return -1;
            }
            reffed_sub = true;
        }
        (void) reffed_pub;
        (void) reffed_sub;

        const poller_subject_kind_t pub_kind =
          want_pub ? poller_spot_pub_kind_for_subject (socket_)
                   : poller_subject_none;
        const poller_subject_kind_t sub_kind =
          want_sub ? poller_spot_sub_kind_for_subject (socket_)
                   : poller_subject_none;

        if (want_pub) {
            zlink_fd_t poll_fd = zlink::retired_fd;
            if (resolve_spot_pub_subject_poller_fd (socket_, &poll_fd) != 0
                || poller_add_fd_registration (
                     poller_, poll_fd, user_data_, ZLINK_POLLOUT, socket_,
                     pub_kind)
                     != 0) {
                const int err = errno ? errno : EFAULT;
                (void) poller_remove_all_registrations_for_subject (
                  poller_, socket_);
                errno = err;
                return -1;
            }
        }

        if (want_sub) {
            int sub_add_rc = -1;
            if (resolved.kind == zlink::service_handle_spot) {
                zlink_fd_t poll_fd = zlink::retired_fd;
                if (resolve_spot_sub_subject_poller_fd (socket_, &poll_fd)
                    == 0) {
                    sub_add_rc = poller_add_fd_registration (
                      poller_, poll_fd, user_data_, ZLINK_POLLIN, socket_,
                      sub_kind);
                }
            } else {
                zlink::socket_base_t *poll_socket =
                  resolve_spot_sub_subject_poller_socket (socket_);
                if (poll_socket) {
                    sub_add_rc = poller_add_registration (
                      poller_, poll_socket, user_data_, ZLINK_POLLIN, socket_,
                      sub_kind);
                }
            }
            if (sub_add_rc != 0) {
                const int err = errno ? errno : EFAULT;
                (void) poller_remove_all_registrations_for_subject (
                  poller_, socket_);
                errno = err;
                return -1;
            }
        }

        const bool wire_request_reply_state =
          want_sub && resolved.kind == zlink::service_handle_spot;
        if (wire_request_reply_state) {
            std::shared_ptr<zlink::spot_reqrep_internal::spot_request_reply_state_t>
              state =
                zlink::spot_reqrep_internal::find_or_create_spot_state (socket_);
            if (state) {
                if (zlink::spot_reqrep_internal::ensure_spot_recv_ready (state)
                    != 0) {
                    const int err = errno;
                    (void) poller_remove_all_registrations_for_subject (
                      poller_, socket_);
                    errno = err;
                    return -1;
                }
                zlink_fd_t routed_fd = zlink::retired_fd;
                if (zlink::spot_reqrep_internal::spot_routed_recv_fd (
                      state, &routed_fd)
                    == 0
                    && poller_add_fd_registration (
                         poller_, routed_fd, user_data_, ZLINK_POLLIN,
                         socket_, poller_spot_routed_kind_for_subject (socket_))
                         != 0) {
                    const int err = errno;
                    (void) poller_remove_all_registrations_for_subject (
                      poller_, socket_);
                    errno = err;
                    return -1;
                }
                zlink::socket_base_t *completion_socket =
                  zlink::spot_reqrep_internal::spot_completion_signal_socket (
                    state);
                if (!completion_socket
                    || poller_add_registration (
                         poller_, completion_socket, NULL, ZLINK_POLLIN,
                         socket_, poller_subject_spot_request_completion)
                         != 0) {
                    const int err = errno ? errno : EFAULT;
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
    if (resolved.kind == zlink::service_handle_spot
        || resolved.kind == zlink::service_handle_spot_node) {
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
        const poller_registration_t &registration =
          poller_->registrations[index];
        const int modify_rc =
          registration.socket
            ? poller_->poller.modify (
                static_cast<zlink::socket_base_t *> (registration.socket),
                events_)
            : poller_->poller.modify_fd (registration.fd, events_);
        if (modify_rc != 0) {
            poller_registration_t failed_registration;
            failed_registration.subject = socket_;
            failed_registration.subject_kind =
              is_pub ? poller_spot_pub_kind_for_subject (socket_)
                     : poller_spot_sub_kind_for_subject (socket_);
            failed_registration.events = events_;
            release_poller_registration (failed_registration);
            return -1;
        }
        poller_registration_t old_registration;
        old_registration.subject = socket_;
        old_registration.subject_kind =
          is_pub ? poller_spot_pub_kind_for_subject (socket_)
                 : poller_spot_sub_kind_for_subject (socket_);
        old_registration.events = old_events;
        release_poller_registration (old_registration);
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
        || resolved.kind == zlink::service_handle_spot
        || resolved.kind == zlink::service_handle_spot_node)
        return poller_remove_all_registrations_for_subject (poller_, socket_);

    errno = EFAULT;
    return -1;
}
