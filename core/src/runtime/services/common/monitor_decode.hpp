/* SPDX-License-Identifier: MPL-2.0 */

#ifndef __ZLINK_SERVICES_COMMON_MONITOR_DECODE_HPP_INCLUDED__
#define __ZLINK_SERVICES_COMMON_MONITOR_DECODE_HPP_INCLUDED__

#include "core/c_api_copy_internal.hpp"
#include "core/recv_internal.hpp"

#include <zlink.h>

#include <string.h>

namespace zlink
{
static inline bool monitor_part_has_more (const zlink_msg_t *part_)
{
    return part_ && msg_frame_has_more (*part_);
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
    copy_routing_id_from_msg (parts[routing_id_index], &event_->routing_id);

    const size_t local_index = routing_id_index + 1;
    copy_fixed_c_string_from_bytes (
      event_->local_addr, sizeof (event_->local_addr),
      zlink_msg_data (&parts[local_index]), zlink_msg_size (&parts[local_index]));

    const size_t remote_index = local_index + 1;
    copy_fixed_c_string_from_bytes (
      event_->remote_addr, sizeof (event_->remote_addr),
      zlink_msg_data (&parts[remote_index]),
      zlink_msg_size (&parts[remote_index]));

    for (size_t i = 0; i < part_count; ++i)
        zlink_msg_close (&parts[i]);
    return 0;
}

}

#endif
