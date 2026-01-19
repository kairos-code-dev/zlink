/* SPDX-License-Identifier: MPL-2.0 */

#include "precompiled.hpp"
#include "zmp_decoder.hpp"
#include "msg.hpp"
#include "wire.hpp"
#include "err.hpp"

zmq::zmp_decoder_t::zmp_decoder_t (size_t bufsize_, int64_t maxmsgsize_) :
    decoder_base_t<zmp_decoder_t, shared_message_memory_allocator> (bufsize_),
    _msg_flags (0),
    _max_msg_size (maxmsgsize_)
{
    int rc = _in_progress.init ();
    errno_assert (rc == 0);

    next_step (_tmpbuf, zmp_header_size, &zmp_decoder_t::header_ready);
}

zmq::zmp_decoder_t::~zmp_decoder_t ()
{
    const int rc = _in_progress.close ();
    errno_assert (rc == 0);
}

int zmq::zmp_decoder_t::header_ready (unsigned char const *read_from_)
{
    if (_tmpbuf[0] != zmp_magic || _tmpbuf[1] != zmp_version) {
        errno = EPROTO;
        return -1;
    }

    if (_tmpbuf[3] != 0) {
        errno = EPROTO;
        return -1;
    }

    const unsigned char flags = _tmpbuf[2];
    const unsigned char reserved =
      flags & ~(zmp_flag_more | zmp_flag_control | zmp_flag_identity
                | zmp_flag_subscribe | zmp_flag_cancel);
    if (reserved != 0) {
        errno = EPROTO;
        return -1;
    }

    if ((flags & zmp_flag_subscribe) && (flags & zmp_flag_cancel)) {
        errno = EPROTO;
        return -1;
    }
    if ((flags & zmp_flag_control) && (flags & zmp_flag_more)) {
        errno = EPROTO;
        return -1;
    }
    if ((flags & zmp_flag_control) && (flags & zmp_flag_identity)) {
        errno = EPROTO;
        return -1;
    }
    if ((flags & zmp_flag_control)
        && (flags & (zmp_flag_subscribe | zmp_flag_cancel))) {
        errno = EPROTO;
        return -1;
    }
    if ((flags & zmp_flag_identity) && (flags & zmp_flag_more)) {
        errno = EPROTO;
        return -1;
    }
    if ((flags & zmp_flag_identity)
        && (flags & (zmp_flag_subscribe | zmp_flag_cancel))) {
        errno = EPROTO;
        return -1;
    }
    if ((flags & (zmp_flag_subscribe | zmp_flag_cancel))
        && (flags & zmp_flag_more)) {
        errno = EPROTO;
        return -1;
    }

    _msg_flags = 0;
    if (flags & zmp_flag_more)
        _msg_flags |= msg_t::more;
    if (flags & zmp_flag_control)
        _msg_flags |= msg_t::command;
    if (flags & zmp_flag_identity)
        _msg_flags |= msg_t::routing_id;
    if (flags & zmp_flag_subscribe)
        _msg_flags |= msg_t::subscribe;
    if (flags & zmp_flag_cancel)
        _msg_flags |= msg_t::cancel;

    const uint32_t msg_size = get_uint32 (_tmpbuf + 4);

    if (msg_size > zmp_max_body_size) {
        errno = EMSGSIZE;
        return -1;
    }

    return size_ready (msg_size, read_from_);
}

int zmq::zmp_decoder_t::size_ready (uint32_t msg_size_,
                                    unsigned char const *read_from_)
{
    if (_max_msg_size >= 0
        && msg_size_ > static_cast<uint64_t> (_max_msg_size)) {
        errno = EMSGSIZE;
        return -1;
    }

    if (unlikely (msg_size_ != static_cast<size_t> (msg_size_))) {
        errno = EMSGSIZE;
        return -1;
    }

    int rc = _in_progress.close ();
    errno_assert (rc == 0);

    shared_message_memory_allocator &allocator = get_allocator ();
    if (unlikely (msg_size_ > static_cast<size_t> (allocator.data ()
                                                   + allocator.size ()
                                                   - read_from_))) {
        rc = _in_progress.init_size (static_cast<size_t> (msg_size_));
    } else {
        rc = _in_progress.init (const_cast<unsigned char *> (read_from_),
                                static_cast<size_t> (msg_size_),
                                shared_message_memory_allocator::call_dec_ref,
                                allocator.buffer (),
                                allocator.provide_content ());
        if (_in_progress.is_zcmsg ()) {
            allocator.advance_content ();
            allocator.inc_ref ();
        }
    }

    if (unlikely (rc)) {
        errno_assert (errno == ENOMEM);
        rc = _in_progress.init ();
        errno_assert (rc == 0);
        errno = ENOMEM;
        return -1;
    }

    _in_progress.set_flags (_msg_flags);
    next_step (_in_progress.data (), _in_progress.size (),
               &zmp_decoder_t::body_ready);

    return 0;
}

int zmq::zmp_decoder_t::body_ready (unsigned char const *)
{
    next_step (_tmpbuf, zmp_header_size, &zmp_decoder_t::header_ready);
    return 1;
}
