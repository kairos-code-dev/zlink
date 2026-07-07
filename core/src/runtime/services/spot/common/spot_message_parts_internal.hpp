/* SPDX-License-Identifier: MPL-2.0 */

#ifndef __ZLINK_SPOT_MESSAGE_PARTS_INTERNAL_HPP_INCLUDED__
#define __ZLINK_SPOT_MESSAGE_PARTS_INTERNAL_HPP_INCLUDED__

#include <zlink.h>

#include "core/msg.hpp"
#include "core/multipart_send_txn.hpp"
#include "sockets/common/socket_base.hpp"
#include "utils/err.hpp"

#include <string>
#include <string.h>
#include <vector>

namespace zlink
{
typedef std::vector<zlink_msg_t> spot_owned_msg_parts_t;

inline bool spot_msg_frame_valid (const zlink_msg_t &frame_)
{
    return reinterpret_cast<const msg_t *> (&frame_)->check ();
}

inline void spot_init_msg_frame (zlink_msg_t *frame_)
{
    if (!frame_)
        return;

    memset (frame_, 0, sizeof (*frame_));
    const int rc = zlink_msg_init (frame_);
    errno_assert (rc == 0);
}

inline void spot_close_msg_frame (zlink_msg_t *frame_)
{
    if (!frame_ || !spot_msg_frame_valid (*frame_))
        return;

    const int rc = zlink_msg_close (frame_);
    errno_assert (rc == 0);
}

inline void spot_clear_msg_parts (spot_owned_msg_parts_t *parts_)
{
    if (!parts_)
        return;

    for (spot_owned_msg_parts_t::iterator it = parts_->begin (); it != parts_->end (); ++it)
        spot_close_msg_frame (&(*it));
    parts_->clear ();
}

inline int spot_move_msg_parts (spot_owned_msg_parts_t *parts_, std::vector<zlink_msg_t> *out_)
{
    if (!parts_ || !out_) {
        errno = EINVAL;
        return -1;
    }

    out_->clear ();
    out_->resize (parts_->size ());
    if (!out_->empty ())
        memset (&(*out_)[0], 0, out_->size () * sizeof (zlink_msg_t));

    size_t index = 0;
    for (spot_owned_msg_parts_t::iterator it = parts_->begin (); it != parts_->end ();
         ++it, ++index) {
        spot_init_msg_frame (&(*out_)[index]);
        if (zlink_msg_move (&(*out_)[index], &(*it)) != 0) {
            const int err = errno;
            for (size_t i = 0; i <= index; ++i)
                spot_close_msg_frame (&(*out_)[i]);
            out_->clear ();
            spot_clear_msg_parts (parts_);
            errno = err;
            return -1;
        }
    }

    spot_clear_msg_parts (parts_);
    return 0;
}

inline std::string spot_msg_frame_to_string (const zlink_msg_t &frame_)
{
    if (!spot_msg_frame_valid (frame_))
        return std::string ();

    return std::string (
      static_cast<const char *> (zlink_msg_data (const_cast<zlink_msg_t *> (&frame_))),
      zlink_msg_size (&frame_));
}

inline void spot_copy_msg_parts_to_strings (const spot_owned_msg_parts_t &parts_,
                                            std::vector<std::string> *out_)
{
    if (!out_)
        return;

    out_->clear ();
    out_->reserve (parts_.size ());
    for (spot_owned_msg_parts_t::const_iterator it = parts_.begin (); it != parts_.end (); ++it)
        out_->push_back (spot_msg_frame_to_string (*it));
}

enum
{
    spot_publish_inline_frame_cap = 16
};

inline int spot_publish_msg_parts (socket_base_t *socket_,
                                   const std::string &topic_,
                                   const spot_owned_msg_parts_t &parts_)
{
    if (!socket_ || topic_.empty ()) {
        errno = EINVAL;
        return -1;
    }

    const size_t count = parts_.size ();
    zlink_msg_t inline_storage[spot_publish_inline_frame_cap];
    std::vector<zlink_msg_t> heap_storage;
    zlink_msg_t *send_frames = inline_storage;
    if (count > spot_publish_inline_frame_cap) {
        heap_storage.resize (count);
        send_frames = &heap_storage[0];
    }

    size_t prepared = 0;
    for (spot_owned_msg_parts_t::const_iterator it = parts_.begin (); prepared < count;
         ++prepared, ++it) {
        spot_init_msg_frame (&send_frames[prepared]);
        if (zlink_msg_copy (&send_frames[prepared], const_cast<zlink_msg_t *> (&(*it))) != 0) {
            const int err = errno;
            for (size_t i = 0; i <= prepared; ++i)
                spot_close_msg_frame (&send_frames[i]);
            errno = err;
            return -1;
        }
    }

    const int rc = logical_multipart_publish (
      socket_, topic_.c_str (), count > 0 ? send_frames : NULL, count, ZLINK_DONTWAIT);
    const int saved_errno = rc == 0 ? 0 : errno;
    for (size_t i = 0; i < count; ++i)
        spot_close_msg_frame (&send_frames[i]);
    if (saved_errno != 0) {
        errno = saved_errno;
        return -1;
    }
    return 0;
}

inline int spot_publish_msg_parts_consume (socket_base_t *socket_,
                                           const std::string &topic_,
                                           spot_owned_msg_parts_t *parts_)
{
    if (!socket_ || topic_.empty () || !parts_) {
        errno = EINVAL;
        return -1;
    }

    const size_t count = parts_->size ();
    zlink_msg_t inline_storage[spot_publish_inline_frame_cap];
    std::vector<zlink_msg_t> heap_storage;
    zlink_msg_t *send_frames = inline_storage;
    if (count > spot_publish_inline_frame_cap) {
        heap_storage.resize (count);
        send_frames = &heap_storage[0];
    }

    size_t prepared = 0;
    for (spot_owned_msg_parts_t::iterator it = parts_->begin (); prepared < count;
         ++prepared, ++it) {
        spot_init_msg_frame (&send_frames[prepared]);
        if (zlink_msg_move (&send_frames[prepared], &(*it)) != 0) {
            const int err = errno;
            for (size_t i = 0; i <= prepared; ++i)
                spot_close_msg_frame (&send_frames[i]);
            spot_clear_msg_parts (parts_);
            errno = err;
            return -1;
        }
    }
    spot_clear_msg_parts (parts_);

    const int rc = logical_multipart_publish (socket_, topic_.c_str (),
                                              count > 0 ? send_frames : NULL, count, 0);
    const int saved_errno = rc == 0 ? 0 : errno;
    for (size_t i = 0; i < count; ++i)
        spot_close_msg_frame (&send_frames[i]);
    if (saved_errno != 0) {
        errno = saved_errno;
        return -1;
    }
    return 0;
}

inline size_t spot_msg_parts_payload_bytes (const spot_owned_msg_parts_t &parts_)
{
    size_t bytes = 0;
    for (spot_owned_msg_parts_t::const_iterator it = parts_.begin (); it != parts_.end (); ++it)
        bytes += zlink_msg_size (const_cast<zlink_msg_t *> (&(*it)));
    return bytes;
}

inline uint32_t spot_msg_parts_encoded_bytes (const spot_owned_msg_parts_t &parts_)
{
    size_t bytes = sizeof (uint16_t);
    for (spot_owned_msg_parts_t::const_iterator it = parts_.begin (); it != parts_.end (); ++it)
        bytes += sizeof (uint32_t) + zlink_msg_size (const_cast<zlink_msg_t *> (&(*it)));
    return static_cast<uint32_t> (bytes > UINT32_MAX ? UINT32_MAX : static_cast<uint32_t> (bytes));
}

inline int spot_recv_logical_message_parts (socket_base_t *src_,
                                            bool dontwait_topic_,
                                            std::string *topic_out_,
                                            spot_owned_msg_parts_t *parts_out_,
                                            size_t *wire_bytes_out_)
{
    if (!src_ || !topic_out_ || !parts_out_ || !wire_bytes_out_) {
        errno = EINVAL;
        return -1;
    }

    topic_out_->clear ();
    spot_clear_msg_parts (parts_out_);
    *wire_bytes_out_ = 0;

    msg_t topic_msg;
    if (topic_msg.init () != 0)
        return -1;

    if (src_->recv (&topic_msg, dontwait_topic_ ? ZLINK_DONTWAIT : 0) != 0) {
        const int err = errno;
        topic_msg.close ();
        errno = err;
        return -1;
    }

    *topic_out_ = std::string (static_cast<const char *> (topic_msg.data ()), topic_msg.size ());
    *wire_bytes_out_ += topic_msg.size ();
    bool more = (topic_msg.flags () & msg_t::more) != 0;
    topic_msg.close ();

    while (more) {
        msg_t frame;
        if (frame.init () != 0) {
            spot_clear_msg_parts (parts_out_);
            return -1;
        }

        if (src_->recv (&frame, 0) != 0) {
            const int err = errno;
            frame.close ();
            spot_clear_msg_parts (parts_out_);
            errno = err;
            return -1;
        }

        parts_out_->push_back (zlink_msg_t ());
        zlink_msg_t &stored = parts_out_->back ();
        spot_init_msg_frame (&stored);
        if (zlink_msg_move (&stored, reinterpret_cast<zlink_msg_t *> (&frame)) != 0) {
            const int err = errno;
            frame.close ();
            spot_close_msg_frame (&stored);
            parts_out_->pop_back ();
            spot_clear_msg_parts (parts_out_);
            errno = err;
            return -1;
        }

        *wire_bytes_out_ += zlink_msg_size (&stored);
        more = (reinterpret_cast<msg_t *> (&stored)->flags () & msg_t::more) != 0;
    }

    return 0;
}
}

#endif
