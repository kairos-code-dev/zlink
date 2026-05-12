/* SPDX-License-Identifier: MPL-2.0 */

#include "utils/precompiled.hpp"

#include <new>
#include <stdint.h>
#include <unordered_map>
#include <vector>

#include "api/close_result_internal.hpp"
#include "api/config_result_internal.hpp"
#include "api/poller_api_internal.hpp"
#include "api/service_spot_request_reply_internal.hpp"
#include "api/socket_request_reply_internal.hpp"
#include "api/timer_api_internal.hpp"
#include "services/spot/spot_subject_access.hpp"
#include "utils/clock.hpp"

namespace
{
void *poller_index_user_data (size_t index_);
bool poller_index_from_user_data (void *user_data_,
                                  size_t item_count_,
                                  size_t *index_out_);
bool registration_matches_native (
  const poller_registration_t &registration_,
  const zlink::socket_poller_t::event_t &native_);

const poller_registration_t *find_registration_for_native (
  poller_handle_t *poller_,
  const zlink::socket_poller_t::event_t &native_)
{
    if (!poller_)
        return NULL;
    size_t native_index = 0;
    if (poller_index_from_user_data (native_.user_data,
                                     poller_->registrations.size (),
                                     &native_index)) {
        const poller_registration_t &registration =
          poller_->registrations[native_index];
        if (registration_matches_native (registration, native_))
            return &registration;
    }
    if (native_.socket) {
        std::unordered_map<void *, size_t>::const_iterator it =
          poller_->socket_registration_indices.find (native_.socket);
        if (it != poller_->socket_registration_indices.end ()
            && it->second < poller_->registrations.size ()) {
            return &poller_->registrations[it->second];
        }
    } else {
        std::unordered_map<zlink_fd_t, size_t>::const_iterator it =
          poller_->fd_registration_indices.find (native_.fd);
        if (it != poller_->fd_registration_indices.end ()
            && it->second < poller_->registrations.size ()) {
            return &poller_->registrations[it->second];
        }
    }
    for (size_t i = 0; i < poller_->registrations.size (); ++i) {
        const poller_registration_t &registration = poller_->registrations[i];
        if (registration.socket && registration.socket == native_.socket)
            return &registration;
        if (!registration.socket && registration.fd == native_.fd)
            return &registration;
    }
    return NULL;
}

bool is_hidden_completion_registration (const poller_registration_t *registration_)
{
    if (!registration_)
        return false;
    return registration_->subject_kind == poller_subject_socket_request_completion
           || registration_->subject_kind
                == poller_subject_router_spot_request_completion
           || registration_->subject_kind == poller_subject_spot_request_completion;
}

bool is_spot_recv_fd_registration (poller_subject_kind_t kind_)
{
    return kind_ == poller_subject_spot_sub
           || kind_ == poller_subject_spot_routed;
}

bool is_spot_send_ready_fd_registration (poller_subject_kind_t kind_)
{
    return kind_ == poller_subject_spot_pub
           || kind_ == poller_subject_spot_node_pub;
}

int drain_hidden_completion_registration (
  const poller_registration_t *registration_)
{
    if (!registration_) {
        errno = EFAULT;
        return -1;
    }

    switch (registration_->subject_kind) {
        case poller_subject_socket_request_completion: {
            socket_handle_t handle = as_socket_handle (registration_->subject);
            std::shared_ptr<zlink::socket_reqrep_internal::socket_request_reply_state_t>
              state =
                zlink::socket_reqrep_internal::find_request_reply_state (handle);
            return state
                     ? zlink::socket_reqrep_internal::drain_reply_completions (
                         state, registration_->subject)
                     : 0;
        }
        case poller_subject_router_spot_request_completion: {
            socket_handle_t handle = as_socket_handle (registration_->subject);
            if (!handle.socket)
                return -1;
            std::shared_ptr<zlink::spot_reqrep_internal::router_spot_request_reply_state_t>
              state = handle.socket->router_spot_request_reply_state ();
            return state
                     ? zlink::spot_reqrep_internal::drain_router_reply_completions (
                         state, registration_->subject)
                     : 0;
        }
        case poller_subject_spot_request_completion: {
            std::shared_ptr<zlink::spot_reqrep_internal::spot_request_reply_state_t>
              state =
                zlink::spot_reqrep_internal::try_find_spot_state (
                  registration_->subject);
            return state
                     ? zlink::spot_reqrep_internal::drain_spot_reply_completions (
                         state, registration_->subject)
                     : 0;
        }
        default:
            return 0;
    }
}

int fill_hidden_completion_event (const poller_registration_t *registration_,
                                  zlink_poller_event_t *event_out_)
{
    if (!registration_ || !event_out_) {
        errno = EFAULT;
        return -1;
    }

    memset (event_out_, 0, sizeof (*event_out_));
    event_out_->source_kind = ZLINK_POLLER_SOURCE_SOCKET;
    event_out_->socket = registration_->subject;
    event_out_->fd = 0;
    event_out_->timer = NULL;
    event_out_->user_data = registration_->user_data;
    event_out_->events = ZLINK_POLLIN;
    return 0;
}

long remaining_timeout_ms (long timeout_ms_,
                           zlink::clock_t &clock_,
                           uint64_t deadline_ms_)
{
    if (timeout_ms_ < 0)
        return -1;
    if (timeout_ms_ == 0)
        return 0;
    const uint64_t now_ms = clock_.now_ms ();
    if (now_ms >= deadline_ms_)
        return 0;
    return static_cast<long> (deadline_ms_ - now_ms);
}

void *poller_index_user_data (size_t index_)
{
    return reinterpret_cast<void *> (static_cast<uintptr_t> (index_) + 1u);
}

bool poller_index_from_user_data (void *user_data_,
                                  size_t item_count_,
                                  size_t *index_out_)
{
    if (!user_data_ || !index_out_)
        return false;
    const uintptr_t encoded = reinterpret_cast<uintptr_t> (user_data_);
    if (encoded == 0u)
        return false;
    const uintptr_t index_value = encoded - 1u;
    if (index_value >= static_cast<uintptr_t> (item_count_)) {
        return false;
    }
    *index_out_ = static_cast<size_t> (index_value);
    return true;
}

bool registration_matches_native (
  const poller_registration_t &registration_,
  const zlink::socket_poller_t::event_t &native_)
{
    return (registration_.socket && registration_.socket == native_.socket)
           || (!registration_.socket && !native_.socket
               && registration_.fd == native_.fd);
}

void set_pollitem_revents_by_identity (
  zlink_pollitem_t *items_,
  int nitems_,
  const zlink::socket_poller_t::event_t &event_)
{
    for (int j = 0; j < nitems_; ++j) {
        if ((items_[j].socket && items_[j].socket == event_.socket)
            || (!items_[j].socket && items_[j].fd == event_.fd)) {
            items_[j].revents = event_.events;
            return;
        }
    }
}

int fill_public_poller_event_from_registration (
  const poller_registration_t *registration_,
  const zlink::socket_poller_t::event_t &native_,
  zlink_poller_event_t *event_out_)
{
    if (!event_out_) {
        errno = EFAULT;
        return -1;
    }

    memset (event_out_, 0, sizeof (*event_out_));
    event_out_->fd = 0;

    if (!registration_) {
        event_out_->source_kind =
          native_.socket ? ZLINK_POLLER_SOURCE_SOCKET : ZLINK_POLLER_SOURCE_FD;
        event_out_->socket = native_.socket;
        event_out_->fd = native_.fd;
        event_out_->timer = NULL;
        event_out_->user_data = native_.user_data;
        event_out_->events = native_.events;
        return 0;
    }

    if (registration_->socket) {
        event_out_->source_kind = ZLINK_POLLER_SOURCE_SOCKET;
        event_out_->socket =
          is_spot_recv_fd_registration (registration_->subject_kind)
            ? registration_->subject
            : native_.socket;
        event_out_->fd = native_.fd;
        event_out_->timer = NULL;
        event_out_->user_data = registration_->user_data;
        event_out_->events = native_.events;
        return 0;
    }

    if (registration_->subject_kind == poller_subject_timer) {
        event_out_->source_kind = ZLINK_POLLER_SOURCE_TIMER;
        event_out_->socket = NULL;
        event_out_->fd = native_.fd;
        event_out_->timer = registration_->subject;
        event_out_->user_data = registration_->user_data;
        event_out_->events = native_.events;
        return 0;
    }

    if (is_spot_recv_fd_registration (registration_->subject_kind)
        || is_spot_send_ready_fd_registration (registration_->subject_kind)) {
        if (is_spot_send_ready_fd_registration (registration_->subject_kind))
            drain_spot_send_ready_signal (registration_->subject);
        event_out_->source_kind = ZLINK_POLLER_SOURCE_SOCKET;
        event_out_->socket = registration_->subject;
        event_out_->fd = native_.fd;
        event_out_->timer = NULL;
        event_out_->user_data = registration_->user_data;
        event_out_->events =
          is_spot_send_ready_fd_registration (registration_->subject_kind)
            ? ZLINK_POLLOUT
            : native_.events;
        return 0;
    }

    event_out_->source_kind = ZLINK_POLLER_SOURCE_FD;
    event_out_->socket = NULL;
    event_out_->fd = native_.fd;
    event_out_->timer = NULL;
    event_out_->user_data = registration_->user_data;
    event_out_->events = native_.events;
    return 0;
}
}

int poller_add_registration (poller_handle_t *poller_,
                             zlink::socket_base_t *socket_,
                             void *user_data_,
                             short events_,
                             void *subject_,
                             poller_subject_kind_t subject_kind_)
{
    if (!poller_ || !socket_) {
        errno = EFAULT;
        return -1;
    }
    const size_t registration_index = poller_->registrations.size ();
    if (poller_->poller.add (socket_, poller_index_user_data (registration_index),
                             events_)
        != 0)
        return -1;

    poller_registration_t registration;
    registration.socket = static_cast<void *> (socket_);
    registration.fd = zlink::retired_fd;
    registration.subject = subject_;
    registration.subject_kind = subject_kind_;
    registration.user_data = user_data_;
    registration.events = events_;
    poller_->registrations.push_back (registration);
    poller_->socket_registration_indices[registration.socket] =
      poller_->registrations.size () - 1;
    return 0;
}

int poller_add_fd_registration (poller_handle_t *poller_,
                                zlink_fd_t fd_,
                                void *user_data_,
                                short events_,
                                void *subject_,
                                poller_subject_kind_t subject_kind_)
{
    if (!poller_) {
        errno = EFAULT;
        return -1;
    }
    const size_t registration_index = poller_->registrations.size ();
    if (poller_->poller.add_fd (fd_, poller_index_user_data (registration_index),
                                events_)
        != 0)
        return -1;

    poller_registration_t registration;
    registration.socket = NULL;
    registration.fd = fd_;
    registration.subject = subject_;
    registration.subject_kind = subject_kind_;
    registration.user_data = user_data_;
    registration.events = events_;
    poller_->registrations.push_back (registration);
    poller_->fd_registration_indices[registration.fd] =
      poller_->registrations.size () - 1;
    return 0;
}

int poller_find_registration_index (poller_handle_t *poller_, void *subject_)
{
    if (!poller_)
        return -1;
    for (size_t i = 0; i < poller_->registrations.size (); ++i) {
        if (poller_->registrations[i].subject == subject_)
            return static_cast<int> (i);
    }
    return -1;
}

int poller_find_registration_index (poller_handle_t *poller_,
                                    void *subject_,
                                    poller_subject_kind_t subject_kind_)
{
    if (!poller_)
        return -1;
    for (size_t i = 0; i < poller_->registrations.size (); ++i) {
        if (poller_->registrations[i].subject == subject_
            && poller_->registrations[i].subject_kind == subject_kind_) {
            return static_cast<int> (i);
        }
    }
    return -1;
}

int poller_find_fd_registration_index (poller_handle_t *poller_,
                                       zlink_fd_t fd_,
                                       poller_subject_kind_t subject_kind_)
{
    if (!poller_)
        return -1;
    for (size_t i = 0; i < poller_->registrations.size (); ++i) {
        if (!poller_->registrations[i].socket
            && poller_->registrations[i].fd == fd_
            && poller_->registrations[i].subject_kind == subject_kind_) {
            return static_cast<int> (i);
        }
    }
    return -1;
}

int poller_remove_registration_at (poller_handle_t *poller_, int index_)
{
    if (!poller_ || index_ < 0
        || static_cast<size_t> (index_) >= poller_->registrations.size ()) {
        errno = EINVAL;
        return -1;
    }

    const poller_registration_t registration =
      poller_->registrations[static_cast<size_t> (index_)];
    const int rc = registration.socket
                     ? poller_->poller.remove (
                         static_cast<zlink::socket_base_t *> (
                           registration.socket))
                     : poller_->poller.remove_fd (registration.fd);
    if (rc == 0) {
        release_poller_registration (registration);
        if (registration.socket)
            poller_->socket_registration_indices.erase (registration.socket);
        else
            poller_->fd_registration_indices.erase (registration.fd);
        const size_t index = static_cast<size_t> (index_);
        const size_t last = poller_->registrations.size () - 1;
        if (index != last) {
            poller_->registrations[index] = poller_->registrations[last];
            const poller_registration_t &moved =
              poller_->registrations[index];
            if (moved.socket) {
                poller_->socket_registration_indices[moved.socket] = index;
                (void) poller_->poller.modify_user_data (
                  static_cast<zlink::socket_base_t *> (moved.socket),
                  poller_index_user_data (index));
            } else {
                poller_->fd_registration_indices[moved.fd] = index;
                (void) poller_->poller.modify_fd_user_data (
                  moved.fd, poller_index_user_data (index));
            }
        }
        poller_->registrations.pop_back ();
    }
    return rc;
}

int poller_remove_all_registrations_for_subject (poller_handle_t *poller_,
                                                 void *subject_)
{
    if (!poller_) {
        errno = EFAULT;
        return -1;
    }

    bool removed = false;
    while (true) {
        const int index = poller_find_registration_index (poller_, subject_);
        if (index < 0)
            break;
        if (poller_remove_registration_at (poller_, index) != 0)
            return -1;
        removed = true;
    }

    if (!removed) {
        errno = EINVAL;
        return -1;
    }
    return 0;
}

int zlink_poll (zlink_pollitem_t *items_,
                int nitems_,
                long timeout_,
                zlink_config_result_t *error_out_)
{
    if (nitems_ < 0 || (nitems_ > 0 && !items_)) {
        errno = EINVAL;
        if (error_out_)
            *error_out_ = ZLINK_CONFIG_INVALID_ARGUMENT;
        return -1;
    }
    if (nitems_ == 0) {
        if (error_out_)
            *error_out_ = ZLINK_CONFIG_OK;
        return 0;
    }

    zlink::socket_poller_t poller;
    for (int i = 0; i < nitems_; ++i) {
        items_[i].revents = 0;
        void *index_user_data =
          poller_index_user_data (static_cast<size_t> (i));
        if (items_[i].socket) {
            socket_handle_t handle = as_socket_handle (items_[i].socket);
            if (!handle.socket) {
                if (error_out_)
                    *error_out_ = ZLINK_CONFIG_INVALID_HANDLE;
                return -1;
            }
            if (validate_socket_callback_poller_events (handle,
                                                        items_[i].events)
                != 0) {
                if (error_out_)
                    *error_out_ = ZLINK_CONFIG_INVALID_ARGUMENT;
                return -1;
            }
            if (poller.add (handle.socket, index_user_data, items_[i].events)
                != 0) {
                if (error_out_)
                    *error_out_ = zlink::config_result_internal::from_errno (errno);
                return -1;
            }
        } else if (poller.add_fd (items_[i].fd, index_user_data,
                                  items_[i].events)
                   != 0) {
            if (error_out_)
                *error_out_ = zlink::config_result_internal::from_errno (errno);
            return -1;
        }
    }

    std::vector<zlink::socket_poller_t::event_t> events (
      static_cast<size_t> (nitems_));
    const int rc = poller.wait (events.data (), nitems_, timeout_);
    if (rc < 0) {
        if (error_out_)
            *error_out_ = zlink::config_result_internal::from_errno (errno);
        return rc;
    }
    if (rc == 0) {
        if (error_out_)
            *error_out_ = ZLINK_CONFIG_OK;
        return 0;
    }
    if (nitems_ == 1) {
        items_[0].revents = events[0].events;
        if (error_out_)
            *error_out_ = ZLINK_CONFIG_OK;
        return rc;
    }

    for (int i = 0; i < rc; ++i) {
        size_t index = 0;
        if (poller_index_from_user_data (
              events[i].user_data, static_cast<size_t> (nitems_), &index)) {
            items_[static_cast<int> (index)].revents = events[i].events;
            continue;
        }
        set_pollitem_revents_by_identity (items_, nitems_, events[i]);
    }
    if (error_out_)
        *error_out_ = ZLINK_CONFIG_OK;
    return rc;
}

void *zlink_poller_new (void)
{
    poller_handle_t *poller = new (std::nothrow) poller_handle_t;
    if (!poller) {
        errno = ENOMEM;
        return NULL;
    }
    return static_cast<void *> (poller);
}

zlink_close_result_t zlink_poller_destroy (void **poller_p_)
{
    if (!poller_p_ || !*poller_p_) {
        errno = EFAULT;
        return ZLINK_CLOSE_INVALID_HANDLE;
    }
    poller_handle_t *poller = as_poller_handle (*poller_p_);
    if (!poller)
        return ZLINK_CLOSE_INVALID_HANDLE;
    for (size_t i = 0; i < poller->registrations.size (); ++i)
        release_poller_registration (poller->registrations[i]);
    poller->tag = 0xdeadbeef;
    delete poller;
    *poller_p_ = NULL;
    return ZLINK_CLOSE_OK;
}

int zlink_poller_size (void *poller_, zlink_config_result_t *error_out_)
{
    poller_handle_t *poller = as_poller_handle (poller_);
    if (!poller) {
        if (error_out_)
            *error_out_ = ZLINK_CONFIG_INVALID_HANDLE;
        return -1;
    }
    if (error_out_)
        *error_out_ = ZLINK_CONFIG_OK;
    return poller->poller.size ();
}

zlink_config_result_t zlink_poller_add_fd (void *poller_,
                                           zlink_fd_t fd_,
                                           void *user_data_,
                                           short events_)
{
    poller_handle_t *poller = as_poller_handle (poller_);
    if (!poller)
        return ZLINK_CONFIG_INVALID_HANDLE;
    return zlink::config_result_internal::from_rc (
      poller_add_fd_registration (poller, fd_, user_data_, events_, NULL,
                                  poller_subject_fd));
}

zlink_config_result_t zlink_poller_modify_fd (void *poller_,
                                              zlink_fd_t fd_,
                                              short events_)
{
    poller_handle_t *poller = as_poller_handle (poller_);
    if (!poller)
        return ZLINK_CONFIG_INVALID_HANDLE;
    const int index =
      poller_find_fd_registration_index (poller, fd_, poller_subject_fd);
    if (index < 0) {
        errno = EINVAL;
        return ZLINK_CONFIG_INVALID_ARGUMENT;
    }
    if (poller->poller.modify_fd (fd_, events_) != 0)
        return zlink::config_result_internal::from_errno (errno);
    poller->registrations[static_cast<size_t> (index)].events = events_;
    return ZLINK_CONFIG_OK;
}

zlink_config_result_t zlink_poller_remove_fd (void *poller_, zlink_fd_t fd_)
{
    poller_handle_t *poller = as_poller_handle (poller_);
    if (!poller)
        return ZLINK_CONFIG_INVALID_HANDLE;
    const int index =
      poller_find_fd_registration_index (poller, fd_, poller_subject_fd);
    if (index < 0) {
        errno = EINVAL;
        return ZLINK_CONFIG_INVALID_ARGUMENT;
    }
    return zlink::config_result_internal::from_rc (
      poller_remove_registration_at (poller, index));
}

zlink_config_result_t zlink_poller_add_timer (void *poller_,
                                              void *timer_,
                                              void *user_data_)
{
    poller_handle_t *poller = as_poller_handle (poller_);
    timer_handle_t *timer = as_timer_handle (timer_);
    if (!poller || !timer)
        return ZLINK_CONFIG_INVALID_HANDLE;

    if (timer_handle_acquire_poller_ref (timer) != 0)
        return zlink::config_result_internal::from_errno (errno);

    zlink_fd_t fd = 0;
    if (timer_handle_signaler_fd (timer, &fd) != 0
        || poller_add_fd_registration (poller, fd, user_data_, ZLINK_POLLIN,
                                       timer_, poller_subject_timer)
             != 0) {
        const int err = errno;
        timer_handle_release_poller_ref (timer);
        errno = err;
        return zlink::config_result_internal::from_errno (err);
    }
    return ZLINK_CONFIG_OK;
}

zlink_config_result_t zlink_poller_remove_timer (void *poller_, void *timer_)
{
    poller_handle_t *poller = as_poller_handle (poller_);
    timer_handle_t *timer = as_timer_handle (timer_);
    if (!poller || !timer)
        return ZLINK_CONFIG_INVALID_HANDLE;

    const int index =
      poller_find_registration_index (poller, timer_, poller_subject_timer);
    if (index < 0) {
        errno = ENOENT;
        return ZLINK_CONFIG_INVALID_ARGUMENT;
    }
    return zlink::config_result_internal::from_rc (
      poller_remove_registration_at (poller, index));
}

int zlink_poller_wait (void *poller_,
                       zlink_poller_event_t *events_,
                       int n_events_,
                       long timeout_,
                       zlink_config_result_t *error_out_)
{
    poller_handle_t *poller = as_poller_handle (poller_);
    if (!poller) {
        if (error_out_)
            *error_out_ = ZLINK_CONFIG_INVALID_HANDLE;
        return -1;
    }
    if (n_events_ < 0 || (n_events_ > 0 && !events_)) {
        errno = EINVAL;
        if (error_out_)
            *error_out_ = ZLINK_CONFIG_INVALID_ARGUMENT;
        return -1;
    }
    zlink::clock_t clock;
    const uint64_t deadline_ms =
      timeout_ > 0 ? clock.now_ms () + static_cast<uint64_t> (timeout_) : 0;
    while (true) {
        poller->native_events.resize (static_cast<size_t> (n_events_));
        const int rc = poller->poller.wait (
          poller->native_events.empty () ? NULL : poller->native_events.data (),
          n_events_,
          remaining_timeout_ms (timeout_, clock, deadline_ms));
        if (rc < 0) {
            if (errno == EAGAIN) {
                if (error_out_)
                    *error_out_ = ZLINK_CONFIG_OK;
                return 0;
            }
            if (error_out_)
                *error_out_ = zlink::config_result_internal::from_errno (errno);
            return rc;
        }
        if (rc == 0) {
            if (error_out_)
                *error_out_ = ZLINK_CONFIG_OK;
            return 0;
        }

        int public_count = 0;
        bool drained_any = false;
        for (int i = 0; i < rc; ++i) {
            const poller_registration_t *registration =
              find_registration_for_native (poller, poller->native_events[i]);
            if (is_hidden_completion_registration (registration)) {
                const int drain_rc =
                  drain_hidden_completion_registration (registration);
                if (drain_rc < 0) {
                    if (error_out_)
                        *error_out_ =
                          zlink::config_result_internal::from_errno (errno);
                    return -1;
                }
                if (drain_rc > 0)
                    drained_any = true;
                if (drain_rc > 0 && registration && registration->user_data) {
                    if (fill_hidden_completion_event (registration,
                                                      &events_[public_count])
                        != 0) {
                        if (error_out_)
                            *error_out_ = zlink::config_result_internal::from_errno (
                              errno);
                        return -1;
                    }
                    ++public_count;
                }
                continue;
            }
            if (fill_public_poller_event_from_registration (
                  registration,
                  poller->native_events[i],
                  &events_[public_count])
                != 0) {
                if (error_out_)
                    *error_out_ = zlink::config_result_internal::from_errno (
                      errno);
                return -1;
            }
            ++public_count;
        }

        if (public_count > 0) {
            if (error_out_)
                *error_out_ = ZLINK_CONFIG_OK;
            return public_count;
        }
        // Hidden completion drains fire user callbacks (e.g. reply handlers)
        // that flip caller-visible state. Even when no public event is
        // produced, the caller must be allowed to inspect that state and
        // submit follow-up work. Looping back into wait() here would block
        // until the next timeout, capping throughput at 1/timeout per slot.
        if (drained_any) {
            if (error_out_)
                *error_out_ = ZLINK_CONFIG_OK;
            return 0;
        }
    }
}
