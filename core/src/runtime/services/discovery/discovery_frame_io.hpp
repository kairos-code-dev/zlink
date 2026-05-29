/* SPDX-License-Identifier: MPL-2.0 */

#ifndef ZLINK_DISCOVERY_FRAME_IO_HPP_INCLUDED
#define ZLINK_DISCOVERY_FRAME_IO_HPP_INCLUDED

#include "core/send_internal.hpp"
#include "zlink.h"

#include <string>
#include <string.h>
#include <vector>

namespace zlink
{
namespace discovery_protocol
{

inline int send_frame (void *socket_,
                       const void *data_,
                       size_t size_,
                       int flags_)
{
    zlink_msg_t msg;
    if (zlink_msg_init_size (&msg, size_) != 0)
        return -1;
    if (size_ > 0 && data_)
        memcpy (zlink_msg_data (&msg), data_, size_);
    const int rc = zlink::send_msg_internal (socket_, &msg, flags_);
    if (rc == -1)
        zlink_msg_close (&msg);
    return rc;
}

inline int send_u16 (void *socket_, uint16_t value_, int flags_)
{
    return send_frame (socket_, &value_, sizeof (value_), flags_);
}

inline int send_u32 (void *socket_, uint32_t value_, int flags_)
{
    return send_frame (socket_, &value_, sizeof (value_), flags_);
}

inline int send_u64 (void *socket_, uint64_t value_, int flags_)
{
    return send_frame (socket_, &value_, sizeof (value_), flags_);
}

inline int send_i64 (void *socket_, int64_t value_, int flags_)
{
    return send_frame (socket_, &value_, sizeof (value_), flags_);
}

inline int send_string (void *socket_, const std::string &value_, int flags_)
{
    return send_frame (socket_, value_.empty () ? NULL : value_.data (),
                       value_.size (), flags_);
}

inline int send_routing_id (void *socket_,
                            const zlink_routing_id_t &rid_,
                            int flags_)
{
    return send_frame (socket_, rid_.size ? rid_.data : NULL, rid_.size,
                       flags_);
}

inline bool read_u16 (const zlink_msg_t &msg_, uint16_t *out_)
{
    if (!out_ || zlink_msg_size (&msg_) != sizeof (uint16_t))
        return false;
    memcpy (out_, zlink_msg_data (const_cast<zlink_msg_t *> (&msg_)),
            sizeof (uint16_t));
    return true;
}

inline bool read_u8 (const zlink_msg_t &msg_, uint8_t *out_)
{
    if (!out_ || zlink_msg_size (&msg_) != sizeof (uint8_t))
        return false;
    memcpy (out_, zlink_msg_data (const_cast<zlink_msg_t *> (&msg_)),
            sizeof (uint8_t));
    return true;
}

inline bool read_u32 (const zlink_msg_t &msg_, uint32_t *out_)
{
    if (!out_ || zlink_msg_size (&msg_) != sizeof (uint32_t))
        return false;
    memcpy (out_, zlink_msg_data (const_cast<zlink_msg_t *> (&msg_)),
            sizeof (uint32_t));
    return true;
}

inline bool read_u64 (const zlink_msg_t &msg_, uint64_t *out_)
{
    if (!out_ || zlink_msg_size (&msg_) != sizeof (uint64_t))
        return false;
    memcpy (out_, zlink_msg_data (const_cast<zlink_msg_t *> (&msg_)),
            sizeof (uint64_t));
    return true;
}

inline bool read_i64 (const zlink_msg_t &msg_, int64_t *out_)
{
    if (!out_ || zlink_msg_size (&msg_) != sizeof (int64_t))
        return false;
    memcpy (out_, zlink_msg_data (const_cast<zlink_msg_t *> (&msg_)),
            sizeof (int64_t));
    return true;
}

inline std::string read_string (const zlink_msg_t &msg_)
{
    const size_t size = zlink_msg_size (&msg_);
    if (size == 0)
        return std::string ();
    const char *data =
      static_cast<const char *> (zlink_msg_data (const_cast<zlink_msg_t *> (&msg_)));
    return std::string (data, data + size);
}

inline bool read_routing_id (const zlink_msg_t &msg_, zlink_routing_id_t *out_)
{
    if (!out_)
        return false;
    const size_t size = zlink_msg_size (&msg_);
    if (size > sizeof (out_->data))
        return false;
    out_->size = static_cast<uint8_t> (size);
    if (size > 0)
        memcpy (out_->data,
                zlink_msg_data (const_cast<zlink_msg_t *> (&msg_)), size);
    return true;
}

inline void read_bytes (const zlink_msg_t &msg_,
                        std::vector<unsigned char> *out_)
{
    if (!out_)
        return;
    const size_t size = zlink_msg_size (&msg_);
    out_->resize (size);
    if (size > 0)
        memcpy (&(*out_)[0], zlink_msg_data (const_cast<zlink_msg_t *> (&msg_)),
                size);
}

}
}

#endif
