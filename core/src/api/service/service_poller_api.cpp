/* SPDX-License-Identifier: MPL-2.0 */

#include "utils/precompiled.hpp"

#include "utils/err.hpp"
#include "api/service/service_handle_internal.hpp"
#include "api/service/service_mode_internal.hpp"
#include "api/socket/socket_request_reply_internal.hpp"
#include "api/spot/request_reply/service_spot_request_reply_internal.hpp"
#include "api/monitoring/timer_api_internal.hpp"

#include "services/spot/pubsub/spot_subject_access.hpp"

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
            break;
        case poller_subject_socket_request_completion: {
            std::shared_ptr<zlink::socket_reqrep_internal::socket_request_reply_state_t>
              state =
                std::static_pointer_cast<
                  zlink::socket_reqrep_internal::socket_request_reply_state_t> (
                  registration_.state_ref);
            if (state)
                zlink::request_completion::release_signal_poller_ref (
                  &state->completion);
            break;
        }
        case poller_subject_router_spot_request_completion: {
            std::shared_ptr<
              zlink::spot_reqrep_internal::router_spot_request_reply_state_t>
              state =
                std::static_pointer_cast<
                  zlink::spot_reqrep_internal::router_spot_request_reply_state_t> (
                  registration_.state_ref);
            if (state)
                zlink::request_completion::release_signal_poller_ref (
                  &state->completion);
            break;
        }
        case poller_subject_spot_request_completion: {
            std::shared_ptr<zlink::spot_reqrep_internal::spot_request_reply_state_t>
              state =
                std::static_pointer_cast<
                  zlink::spot_reqrep_internal::spot_request_reply_state_t> (
                  registration_.state_ref);
            if (state)
                zlink::request_completion::release_signal_poller_ref (
                  &state->completion_state.direct);
            break;
        }
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

struct spot_subject_ref_guard_t
{
    explicit spot_subject_ref_guard_t (void *subject_) :
        subject (subject_),
        pub (false),
        sub (false)
    {
    }

    ~spot_subject_ref_guard_t ()
    {
        release ();
    }

    int acquire_pub ()
    {
        if (increment_spot_subject_poller_ref (subject, ZLINK_POLLOUT) != 0)
            return -1;
        pub = true;
        return 0;
    }

    int acquire_sub ()
    {
        if (increment_spot_subject_poller_ref (subject, ZLINK_POLLIN) != 0)
            return -1;
        sub = true;
        return 0;
    }

    void commit_pub () { pub = false; }
    void commit_sub () { sub = false; }

    void release ()
    {
        if (pub) {
            poller_registration_t registration;
            registration.subject = subject;
            registration.subject_kind = poller_spot_pub_kind_for_subject (subject);
            registration.events = ZLINK_POLLOUT;
            release_poller_registration (registration);
            pub = false;
        }
        if (sub) {
            poller_registration_t registration;
            registration.subject = subject;
            registration.subject_kind = poller_spot_sub_kind_for_subject (subject);
            registration.events = ZLINK_POLLIN;
            release_poller_registration (registration);
            sub = false;
        }
    }

    void *subject;
    bool pub;
    bool sub;
};

int add_spot_completion_registration (
  poller_handle_t *poller_,
  void *spot_,
  const std::shared_ptr<zlink::spot_reqrep_internal::spot_request_reply_state_t>
    &state_)
{
    if (!poller_ || !spot_ || !state_) {
        errno = EFAULT;
        return -1;
    }

    return poller_add_hidden_completion_registration (
      poller_, zlink::spot_reqrep_internal::spot_completion_signal_socket (state_),
      spot_, poller_subject_spot_request_completion,
      &state_->completion_state.direct, state_);
}

int add_spot_completion_only_registration (poller_handle_t *poller_,
                                           void *spot_)
{
    std::shared_ptr<zlink::spot_reqrep_internal::spot_request_reply_state_t>
      state = zlink::spot_reqrep_internal::find_or_create_spot_state (spot_);
    if (!state
        || zlink::spot_reqrep_internal::ensure_spot_completion_queue_ready (
             state)
             != 0) {
        return -1;
    }
    return add_spot_completion_registration (poller_, spot_, state);
}

int add_spot_pub_ready_registration (poller_handle_t *poller_,
                                     void *spot_or_node_,
                                     void *user_data_,
                                     poller_subject_kind_t kind_)
{
    zlink_fd_t poll_fd = zlink::retired_fd;
    if (resolve_spot_pub_subject_poller_fd (spot_or_node_, &poll_fd) != 0)
        return -1;
    return poller_add_fd_registration (poller_, poll_fd, user_data_,
                                       ZLINK_POLLOUT, spot_or_node_, kind_);
}

int add_spot_sub_ready_registration (
  poller_handle_t *poller_,
  void *spot_or_node_,
  void *user_data_,
  poller_subject_kind_t kind_,
  zlink::service_handle_kind_t resolved_kind_)
{
    if (resolved_kind_ == zlink::service_handle_spot) {
        zlink_fd_t poll_fd = zlink::retired_fd;
        if (resolve_spot_sub_subject_poller_fd (spot_or_node_, &poll_fd) != 0)
            return -1;
        return poller_add_fd_registration (poller_, poll_fd, user_data_,
                                           ZLINK_POLLIN, spot_or_node_, kind_);
    }

    zlink::socket_base_t *poll_socket =
      resolve_spot_sub_subject_poller_socket (spot_or_node_);
    if (!poll_socket) {
        errno = EFAULT;
        return -1;
    }
    return poller_add_registration (poller_, poll_socket, user_data_,
                                    ZLINK_POLLIN, spot_or_node_, kind_);
}

int add_spot_routed_recv_registration (
  poller_handle_t *poller_,
  void *spot_,
  void *user_data_,
  const std::shared_ptr<zlink::spot_reqrep_internal::spot_request_reply_state_t>
    &state_)
{
    if (zlink::spot_reqrep_internal::ensure_spot_recv_ready (state_) != 0)
        return -1;

    zlink_fd_t routed_fd = zlink::retired_fd;
    if (zlink::spot_reqrep_internal::spot_routed_recv_fd (state_, &routed_fd)
        != 0) {
        errno = 0;
        return 0;
    }

    return poller_add_fd_registration (
      poller_, routed_fd, user_data_, ZLINK_POLLIN, spot_,
      poller_spot_routed_kind_for_subject (spot_));
}

int add_spot_request_reply_registrations (poller_handle_t *poller_,
                                          void *spot_,
                                          void *user_data_)
{
    std::shared_ptr<zlink::spot_reqrep_internal::spot_request_reply_state_t>
      state = zlink::spot_reqrep_internal::find_or_create_spot_state (spot_);
    if (!state)
        return -1;
    if (add_spot_routed_recv_registration (poller_, spot_, user_data_, state)
        != 0)
        return -1;
    return add_spot_completion_registration (poller_, spot_, state);
}

int add_spot_combined_readiness_registration (
  poller_handle_t *poller_,
  void *spot_or_node_,
  void *user_data_,
  const zlink::service_handle_resolution_t &resolved_,
  bool want_pub_,
  bool want_sub_)
{
    spot_subject_ref_guard_t refs (spot_or_node_);
    if (want_pub_ && refs.acquire_pub () != 0)
        return -1;
    if (want_sub_ && refs.acquire_sub () != 0)
        return -1;

    if (want_pub_) {
        const poller_subject_kind_t pub_kind =
          poller_spot_pub_kind_for_subject (spot_or_node_);
        if (add_spot_pub_ready_registration (
              poller_, spot_or_node_, user_data_, pub_kind)
            != 0)
            return -1;
        refs.commit_pub ();
    }

    if (want_sub_) {
        const poller_subject_kind_t sub_kind =
          poller_spot_sub_kind_for_subject (spot_or_node_);
        if (add_spot_sub_ready_registration (
              poller_, spot_or_node_, user_data_, sub_kind, resolved_.kind)
            != 0)
            return -1;
        refs.commit_sub ();
    }

    if (want_sub_ && resolved_.kind == zlink::service_handle_spot
        && add_spot_request_reply_registrations (
             poller_, spot_or_node_, user_data_)
             != 0) {
        return -1;
    }

    return 0;
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
        if (events_ == ZLINK_POLLCOMPLETION) {
            if (resolved.kind != zlink::service_handle_spot) {
                errno = EINVAL;
                return -1;
            }
            return add_spot_completion_only_registration (poller_, socket_);
        }
        if ((events_ & ZLINK_POLLCOMPLETION) != 0) {
            errno = EINVAL;
            return -1;
        }
        bool want_pub = false;
        bool want_sub = false;
        if (parse_spot_combined_poller_events (events_, &want_pub, &want_sub)
            != 0)
            return -1;
        const int rc = add_spot_combined_readiness_registration (
          poller_, socket_, user_data_, resolved, want_pub, want_sub);
        if (rc != 0) {
            const int err = errno ? errno : EFAULT;
            (void) poller_remove_all_registrations_for_subject (poller_,
                                                                socket_);
            errno = err;
        }
        return rc;
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
        if ((events_ & ZLINK_POLLCOMPLETION) != 0) {
            errno = EINVAL;
            return -1;
        }
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
