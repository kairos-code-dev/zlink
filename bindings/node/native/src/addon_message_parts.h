/* SPDX-License-Identifier: MPL-2.0 */

#pragma once

#include "addon_common_api.h"

inline void copy_routing_id (zlink_routing_id_t *out, const zlink_routing_id_t *in)
{
    if (!out)
        return;
    if (in)
        memcpy (out, in, sizeof (*out));
    else
        memset (out, 0, sizeof (*out));
}

inline bool append_msg_move (std::vector<zlink_msg_t> *parts, zlink_msg_t *part)
{
    if (!parts || !part)
        return false;

    parts->emplace_back ();
    zlink_msg_t *slot = &parts->back ();
    if (zlink_msg_init (slot) != 0) {
        parts->pop_back ();
        return false;
    }
    if (zlink_msg_move (slot, part) != 0) {
        zlink_msg_close (slot);
        parts->pop_back ();
        return false;
    }
    return true;
}

inline int collect_recv_parts (void *socket,
                               zlink_msg_t *first_part,
                               zlink_part_flag_t has_more,
                               std::vector<zlink_msg_t> *parts)
{
    if (!parts) {
        if (first_part)
            zlink_msg_close (first_part);
        errno = EFAULT;
        return ZLINK_RECV_INTERNAL_ERROR;
    }

    parts->clear ();
    if (!append_msg_move (parts, first_part)) {
        if (first_part)
            zlink_msg_close (first_part);
        errno = ENOMEM;
        return ZLINK_RECV_INTERNAL_ERROR;
    }

    while (has_more) {
        const zlink_routing_id_t *source_rid = NULL;
        zlink_msg_t next_part;
        if (zlink_msg_init (&next_part) != 0) {
            close_msg_vector (*parts);
            parts->clear ();
            return ZLINK_RECV_INTERNAL_ERROR;
        }
        zlink_part_flag_t more = ZLINK_PART_FINAL;
        int rc =
          zlink_recv_part (socket, &source_rid, &next_part, &more, ZLINK_RECV_FLAGS_DONTWAIT);
        if (rc != ZLINK_RECV_OK) {
            zlink_msg_close (&next_part);
            close_msg_vector (*parts);
            parts->clear ();
            return rc;
        }
        if (!append_msg_move (parts, &next_part)) {
            zlink_msg_close (&next_part);
            close_msg_vector (*parts);
            parts->clear ();
            errno = ENOMEM;
            return ZLINK_RECV_INTERNAL_ERROR;
        }
        has_more = more;
    }

    return ZLINK_RECV_OK;
}

inline void close_recv_parts (zlink_msg_t *parts, size_t part_count)
{
    if (!parts)
        return;
    zlink_multipart_close (parts, part_count);
}
