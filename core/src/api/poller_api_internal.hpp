/* SPDX-License-Identifier: MPL-2.0 */

#ifndef __ZLINK_API_POLLER_API_INTERNAL_HPP_INCLUDED__
#define __ZLINK_API_POLLER_API_INTERNAL_HPP_INCLUDED__

#include "utils/precompiled.hpp"

#include <vector>

#include "api/socket_api_internal.hpp"
#include "core/socket_poller.hpp"

enum poller_subject_kind_t
{
    poller_subject_none = 0,
    poller_subject_spot_pub,
    poller_subject_spot_sub,
    poller_subject_spot_node_pub,
    poller_subject_spot_node_sub
};

struct poller_registration_t
{
    poller_registration_t () :
        socket (NULL),
        subject (NULL),
        subject_kind (poller_subject_none),
        events (0)
    {
    }

    void *socket;
    void *subject;
    poller_subject_kind_t subject_kind;
    short events;
};

struct poller_handle_t
{
    poller_handle_t () : tag (0x706f6c6c) {}

    bool check_tag () const { return tag == 0x706f6c6c; }

    uint32_t tag;
    zlink::socket_poller_t poller;
    std::vector<poller_registration_t> registrations;
};

static inline poller_handle_t *as_poller_handle (void *poller_)
{
    if (!poller_) {
        errno = EFAULT;
        return NULL;
    }

    poller_handle_t *poller = static_cast<poller_handle_t *> (poller_);
    if (!poller->check_tag ()) {
        errno = EFAULT;
        return NULL;
    }
    return poller;
}

int validate_socket_callback_poller_events (socket_handle_t handle_,
                                            short events_);
void release_poller_registration (const poller_registration_t &registration_);
int poller_add_registration (poller_handle_t *poller_,
                             zlink::socket_base_t *socket_,
                             void *user_data_,
                             short events_,
                             void *subject_,
                             poller_subject_kind_t subject_kind_);
int poller_find_registration_index (poller_handle_t *poller_, void *subject_);
int poller_find_registration_index (poller_handle_t *poller_,
                                    void *subject_,
                                    poller_subject_kind_t subject_kind_);
int poller_remove_registration_at (poller_handle_t *poller_, int index_);
int poller_remove_all_registrations_for_subject (poller_handle_t *poller_,
                                                 void *subject_);

#endif
