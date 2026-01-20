/* SPDX-License-Identifier: MPL-2.0 */

#include "precompiled.hpp"
#include "zmp_decoder.hpp"
#include "msg.hpp"
#include "wire.hpp"
#include "err.hpp"

namespace
{
static uint32_t compute_effective_max (int64_t maxmsgsize_)
{
    uint64_t limit = zmq::zmp_max_body_size;
    if (maxmsgsize_ >= 0
        && static_cast<uint64_t> (maxmsgsize_) < limit)
        limit = static_cast<uint64_t> (maxmsgsize_);
    return static_cast<uint32_t> (limit);
}

static const unsigned char zmp_flag_to_msg_flags[32] = {
  0,                 zmq::msg_t::more,       zmq::msg_t::command,    0,
  zmq::msg_t::routing_id, 0,                 0,                     0,
  zmq::msg_t::subscribe,  0,                 0,                     0,
  0,                0,               0,               0,
  zmq::msg_t::cancel,     0,               0,               0,
  0,                0,               0,               0,
  0,                0,               0,               0,
  0,                0,               0,               0
};
}

zmq::zmp_decoder_t::zmp_decoder_t (size_t bufsize_, int64_t maxmsgsize_) :
    decoder_base_t<zmp_decoder_t, shared_message_memory_allocator> (bufsize_),
    _msg_flags (0),
    _max_msg_size_effective (compute_effective_max (maxmsgsize_))
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
    if (flags == 0) {
        _msg_flags = 0;
    } else if (flags == zmp_flag_more) {
        _msg_flags = msg_t::more;
    } else {
        if (flags & ~zmp_flag_mask) {
            errno = EPROTO;
            return -1;
        }
        if (flags & (flags - 1)) {
            errno = EPROTO;
            return -1;
        }
        _msg_flags = zmp_flag_to_msg_flags[flags];
    }

    const uint32_t msg_size = get_uint32 (_tmpbuf + 4);

    return size_ready (msg_size, read_from_);
}

int zmq::zmp_decoder_t::size_ready (uint32_t msg_size_,
                                    unsigned char const *read_from_)
{
    if (unlikely (msg_size_ > _max_msg_size_effective)) {
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
