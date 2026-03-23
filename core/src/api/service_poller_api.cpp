/* SPDX-License-Identifier: MPL-2.0 */

#include "utils/precompiled.hpp"

#include "utils/err.hpp"
#include "api/service_api_internal.hpp"

#include "services/gateway/gateway_access.hpp"
#include "services/spot/spot_internal_receiver.hpp"
#include "services/spot/spot_node_access.hpp"
#include "services/spot/spot_pub.hpp"
#include "services/spot/spot_sub.hpp"

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

zlink::spot_pub_t *resolve_spot_pub_subject (void *spot_or_node_)
{
    if (is_registered_spot_handle (spot_or_node_)) {
        spot_handle_t *spot = static_cast<spot_handle_t *> (spot_or_node_);
        zlink::service_public_api_scope_t admission (spot->public_api);
        if (!admission.acquired ())
            return NULL;
        return ensure_spot_pub (spot);
    }
    if (is_registered_spot_node_handle (spot_or_node_)) {
        zlink::spot_node_t *node = static_cast<zlink::spot_node_t *> (spot_or_node_);
        zlink::service_public_api_scope_t admission (node->public_api_guard ());
        if (!admission.acquired ())
            return NULL;
        return node->ensure_default_pub ();
    }
    errno = EFAULT;
    return NULL;
}

zlink::spot_sub_t *resolve_spot_sub_subject (void *spot_or_node_)
{
    if (is_registered_spot_handle (spot_or_node_)) {
        spot_handle_t *spot = static_cast<spot_handle_t *> (spot_or_node_);
        zlink::service_public_api_scope_t admission (spot->public_api);
        if (!admission.acquired ())
            return NULL;
        return ensure_spot_sub (spot);
    }
    if (is_registered_spot_node_handle (spot_or_node_)) {
        zlink::spot_node_t *node = static_cast<zlink::spot_node_t *> (spot_or_node_);
        zlink::service_public_api_scope_t admission (node->public_api_guard ());
        if (!admission.acquired ())
            return NULL;
        zlink::spot_internal_receiver_t *receiver =
          zlink::spot_node_access_t::ensure_internal_receiver (node);
        if (receiver && receiver->impl ())
            return receiver->impl ();
        return node->ensure_default_sub ();
    }
    errno = EFAULT;
    return NULL;
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
    if (zlink::spot_pub_t *pub = as_spot_pub_side_handle (socket_)) {
        bool is_pub = false;
        if (validate_spot_generic_poller_events (events_, &is_pub) != 0)
            return -1;
        if (!is_pub) {
            errno = EINVAL;
            return -1;
        }
        return poller_add_registration (poller_, pub->poller_socket (),
                                        user_data_, events_, socket_,
                                        poller_subject_none);
    }
    if (zlink::spot_sub_t *sub = as_spot_sub_side_handle (socket_)) {
        bool is_pub = false;
        if (validate_spot_generic_poller_events (events_, &is_pub) != 0)
            return -1;
        if (is_pub) {
            errno = EINVAL;
            return -1;
        }
        return poller_add_registration (poller_, sub->poller_socket (),
                                        user_data_, events_, socket_,
                                        poller_subject_none);
    }
    if (is_registered_spot_handle (socket_)
        || is_registered_spot_node_handle (socket_)) {
        bool is_pub = false;
        if (validate_spot_generic_poller_events (events_, &is_pub) != 0)
            return -1;
        if (increment_spot_subject_poller_ref (socket_, events_) != 0)
            return -1;

        if (is_pub) {
            zlink::spot_pub_t *pub = resolve_spot_pub_subject (socket_);
            if (!pub
                || poller_add_registration (
                     poller_, pub->poller_socket (), user_data_, events_,
                     socket_, poller_spot_pub_kind_for_subject (socket_))
                     != 0) {
                poller_registration_t registration;
                registration.subject = socket_;
                registration.subject_kind =
                  poller_spot_pub_kind_for_subject (socket_);
                registration.events = events_;
                release_poller_registration (registration);
                return -1;
            }
            return 0;
        }

        zlink::spot_sub_t *sub = resolve_spot_sub_subject (socket_);
        if (!sub
            || poller_add_registration (
                 poller_, sub->poller_socket (), user_data_, events_, socket_,
                 poller_spot_sub_kind_for_subject (socket_))
                 != 0) {
            poller_registration_t registration;
            registration.subject = socket_;
            registration.subject_kind =
              poller_spot_sub_kind_for_subject (socket_);
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
