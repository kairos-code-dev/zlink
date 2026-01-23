/* SPDX-License-Identifier: MPL-2.0 */

#include "utils/precompiled.hpp"
#include "protocol/raw_decoder.hpp"
#include "protocol/wire.hpp"
#include "utils/err.hpp"

namespace
{
uint32_t compute_effective_max (int64_t maxmsgsize_)
{
    uint64_t limit = 0xFFFFFFFFull;
    if (maxmsgsize_ >= 0
        && static_cast<uint64_t> (maxmsgsize_) < limit)
        limit = static_cast<uint64_t> (maxmsgsize_);
    return static_cast<uint32_t> (limit);
}
}

zmq::raw_decoder_t::raw_decoder_t (size_t bufsize_, int64_t maxmsgsize_) :
    decoder_base_t<raw_decoder_t, shared_message_memory_allocator> (bufsize_),
    _max_msg_size_effective (compute_effective_max (maxmsgsize_))
{
    int rc = _in_progress.init ();
    errno_assert (rc == 0);

    next_step (_tmpbuf, sizeof (_tmpbuf), &raw_decoder_t::header_ready);
}

zmq::raw_decoder_t::~raw_decoder_t ()
{
    const int rc = _in_progress.close ();
    errno_assert (rc == 0);
}

int zmq::raw_decoder_t::header_ready (unsigned char const *read_from_)
{
    const uint32_t msg_size = get_uint32 (_tmpbuf);
    return size_ready (msg_size, read_from_);
}

int zmq::raw_decoder_t::size_ready (uint32_t msg_size_,
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

    _in_progress.set_flags (0);
    next_step (_in_progress.data (), _in_progress.size (),
               &raw_decoder_t::body_ready);

    return 0;
}

int zmq::raw_decoder_t::body_ready (unsigned char const *)
{
    next_step (_tmpbuf, sizeof (_tmpbuf), &raw_decoder_t::header_ready);
    return 1;
}
