/* SPDX-License-Identifier: MPL-2.0 */

#include "utils/precompiled.hpp"

#include "utils/err.hpp"
#include "api/service_api_internal.hpp"

#include "services/gateway/gateway_access.hpp"

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
        case poller_subject_gateway:
            decrement_gateway_poller_ref (
              static_cast<zlink::gateway_t *> (registration_.subject),
              registration_.events);
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
    if (is_registered_spot_handle (spot_or_node_))
        return increment_spot_poller_ref (
          static_cast<spot_handle_t *> (spot_or_node_), events_);
    if (is_registered_spot_node_handle (spot_or_node_))
        return increment_spot_node_poller_ref (
          static_cast<zlink::spot_node_t *> (spot_or_node_), events_);
    errno = EFAULT;
    return -1;
}

poller_subject_kind_t poller_spot_pub_kind_for_subject (void *spot_or_node_)
{
    if (is_registered_spot_handle (spot_or_node_))
        return poller_subject_spot_pub;
    if (is_registered_spot_node_handle (spot_or_node_))
        return poller_subject_spot_node_pub;
    return poller_subject_none;
}

poller_subject_kind_t poller_spot_sub_kind_for_subject (void *spot_or_node_)
{
    if (is_registered_spot_handle (spot_or_node_))
        return poller_subject_spot_sub;
    if (is_registered_spot_node_handle (spot_or_node_))
        return poller_subject_spot_node_sub;
    return poller_subject_none;
}

int poller_add_gateway_registration (poller_handle_t *poller_,
                                     void *gateway_,
                                     void *user_data_,
                                     short events_)
{
    if (!is_registered_gateway_handle (gateway_)) {
        errno = EFAULT;
        return -1;
    }
    if ((events_ & ~(ZLINK_POLLIN | ZLINK_POLLOUT)) != 0 || events_ == 0) {
        errno = EINVAL;
        return -1;
    }
    zlink::gateway_t *gateway = static_cast<zlink::gateway_t *> (gateway_);
    if (increment_gateway_poller_ref (gateway, events_) != 0)
        return -1;
    zlink::socket_base_t *socket = zlink::gateway_access_t::router_socket (gateway);
    if (!socket
        || poller_add_registration (poller_, socket, user_data_, events_,
                                    gateway_, poller_subject_gateway)
             != 0) {
        decrement_gateway_poller_ref (gateway, events_);
        if (!socket)
            errno = ENOTSUP;
        return -1;
    }
    return 0;
}

int poller_modify_gateway_registration (poller_handle_t *poller_,
                                        void *gateway_,
                                        short events_)
{
    if (!is_registered_gateway_handle (gateway_)) {
        errno = EFAULT;
        return -1;
    }
    if ((events_ & ~(ZLINK_POLLIN | ZLINK_POLLOUT)) != 0 || events_ == 0) {
        errno = EINVAL;
        return -1;
    }
    const int index = poller_find_registration_index (poller_, gateway_);
    if (index < 0) {
        errno = EINVAL;
        return -1;
    }
    zlink::gateway_t *gateway = static_cast<zlink::gateway_t *> (gateway_);
    const short old_events = poller_->registrations[index].events;
    if (increment_gateway_poller_ref (gateway, events_) != 0)
        return -1;
    if (poller_->poller.modify (
          static_cast<zlink::socket_base_t *> (
            poller_->registrations[index].socket),
          events_)
        != 0) {
        decrement_gateway_poller_ref (gateway, events_);
        return -1;
    }
    poller_->registrations[index].events = events_;
    decrement_gateway_poller_ref (gateway, old_events);
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

    if (is_registered_gateway_handle (socket_))
        return poller_add_gateway_registration (poller_, socket_, user_data_,
                                                events_);
    if (as_spot_pub_side_handle (socket_)) {
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
    if (as_spot_sub_side_handle (socket_)) {
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
    if (is_registered_spot_handle (socket_)
        || is_registered_spot_node_handle (socket_)) {
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

    if (is_registered_gateway_handle (socket_))
        return poller_modify_gateway_registration (poller_, socket_, events_);
    if (as_spot_pub_side_handle (socket_) || as_spot_sub_side_handle (socket_)) {
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
    if (is_registered_spot_handle (socket_)
        || is_registered_spot_node_handle (socket_)) {
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
            if (is_registered_spot_handle (socket_))
                decrement_spot_poller_ref (
                  static_cast<spot_handle_t *> (socket_), events_);
            else
                decrement_spot_node_poller_ref (
                  static_cast<zlink::spot_node_t *> (socket_), events_);
            return -1;
        }
        if (is_registered_spot_handle (socket_))
            decrement_spot_poller_ref (
              static_cast<spot_handle_t *> (socket_), old_events);
        else
            decrement_spot_node_poller_ref (
              static_cast<zlink::spot_node_t *> (socket_), old_events);
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

    if (as_spot_pub_side_handle (socket_) || as_spot_sub_side_handle (socket_)
        || is_registered_gateway_handle (socket_)
        || is_registered_spot_handle (socket_)
        || is_registered_spot_node_handle (socket_))
        return poller_remove_all_registrations_for_subject (poller_, socket_);

    errno = EFAULT;
    return -1;
}
