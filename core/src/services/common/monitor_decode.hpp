/* SPDX-License-Identifier: MPL-2.0 */

#ifndef __ZLINK_SERVICES_COMMON_MONITOR_DECODE_HPP_INCLUDED__
#define __ZLINK_SERVICES_COMMON_MONITOR_DECODE_HPP_INCLUDED__

#include "core/recv_internal.hpp"

#include <zlink.h>

#include <string.h>

namespace zlink
{
static inline bool monitor_part_has_more (const zlink_msg_t *part_)
{
    return part_
           && (reinterpret_cast<const msg_t *> (part_)->flags () & msg_t::more)
                != 0;
}

static inline bool read_monitor_u64_part (const zlink_msg_t *part_,
                                          uint64_t *value_out_)
{
    if (!part_ || !value_out_ || zlink_msg_size (part_) < sizeof (uint64_t))
        return false;

    memcpy (value_out_, zlink_msg_data (const_cast<zlink_msg_t *> (part_)),
            sizeof (uint64_t));
    return true;
}

static inline int recv_socket_monitor_event (void *monitor_socket_,
                                             zlink_monitor_event_t *event_,
                                             int flags_)
{
    if (!monitor_socket_ || !event_) {
        errno = EINVAL;
        return -1;
    }

    zlink_msg_t parts[16];
    size_t part_count = 0;
    bool more = true;
    while (more && part_count < sizeof (parts) / sizeof (parts[0])) {
        zlink_msg_init (&parts[part_count]);
        const int rc = recv_msg_internal (
          monitor_socket_, &parts[part_count],
          part_count == 0 ? flags_ : (flags_ & ~ZLINK_DONTWAIT));
        if (rc < 0) {
            zlink_msg_close (&parts[part_count]);
            for (size_t i = 0; i < part_count; ++i)
                zlink_msg_close (&parts[i]);
            return -1;
        }
        more = monitor_part_has_more (&parts[part_count]);
        ++part_count;
    }

    if (part_count == 1 && zlink_msg_size (&parts[0]) == sizeof (*event_)) {
        memcpy (event_, zlink_msg_data (&parts[0]), sizeof (*event_));
        zlink_msg_close (&parts[0]);
        return 0;
    }

    if (more || part_count < 5) {
        for (size_t i = 0; i < part_count; ++i)
            zlink_msg_close (&parts[i]);
        errno = EPROTO;
        return -1;
    }

    memset (event_, 0, sizeof (*event_));

    uint64_t value_count = 0;
    if (!read_monitor_u64_part (&parts[0], &event_->event)
        || !read_monitor_u64_part (&parts[1], &value_count)) {
        for (size_t i = 0; i < part_count; ++i)
            zlink_msg_close (&parts[i]);
        errno = EPROTO;
        return -1;
    }

    const size_t expected_part_count = static_cast<size_t> (value_count) + 5;
    if (part_count != expected_part_count) {
        for (size_t i = 0; i < part_count; ++i)
            zlink_msg_close (&parts[i]);
        errno = EPROTO;
        return -1;
    }

    if (value_count > 0
        && !read_monitor_u64_part (&parts[2], &event_->value)) {
        for (size_t i = 0; i < part_count; ++i)
            zlink_msg_close (&parts[i]);
        errno = EPROTO;
        return -1;
    }

    const size_t routing_id_index = static_cast<size_t> (value_count) + 2;
    const size_t routing_id_size = zlink_msg_size (&parts[routing_id_index]);
    const size_t routing_id_copy =
      routing_id_size > sizeof (event_->routing_id.data)
        ? sizeof (event_->routing_id.data)
        : routing_id_size;
    event_->routing_id.size = static_cast<uint8_t> (routing_id_copy);
    if (routing_id_copy > 0) {
        memcpy (event_->routing_id.data,
                zlink_msg_data (&parts[routing_id_index]), routing_id_copy);
    }

    const size_t local_index = routing_id_index + 1;
    const size_t local_size = zlink_msg_size (&parts[local_index]);
    const size_t local_copy =
      local_size >= sizeof (event_->local_addr)
        ? sizeof (event_->local_addr) - 1
        : local_size;
    if (local_copy > 0)
        memcpy (event_->local_addr, zlink_msg_data (&parts[local_index]),
                local_copy);
    event_->local_addr[local_copy] = '\0';

    const size_t remote_index = local_index + 1;
    const size_t remote_size = zlink_msg_size (&parts[remote_index]);
    const size_t remote_copy =
      remote_size >= sizeof (event_->remote_addr)
        ? sizeof (event_->remote_addr) - 1
        : remote_size;
    if (remote_copy > 0)
        memcpy (event_->remote_addr, zlink_msg_data (&parts[remote_index]),
                remote_copy);
    event_->remote_addr[remote_copy] = '\0';

    for (size_t i = 0; i < part_count; ++i)
        zlink_msg_close (&parts[i]);
    return 0;
}

static inline int recv_service_monitor_event (void *monitor_,
                                              zlink_service_event_t *event_,
                                              int flags_)
{
    if (!monitor_ || !event_) {
        errno = EINVAL;
        return -1;
    }

    zlink_msg_t msg;
    zlink_msg_init (&msg);
    const int rc = recv_msg_internal (monitor_, &msg, flags_);
    if (rc < 0) {
        zlink_msg_close (&msg);
        return -1;
    }
    if (zlink_msg_size (&msg) != sizeof (*event_)) {
        zlink_msg_close (&msg);
        errno = EPROTO;
        return -1;
    }
    memcpy (event_, zlink_msg_data (&msg), sizeof (*event_));
    zlink_msg_close (&msg);
    return 0;
}
}

#endif
