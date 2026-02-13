/* SPDX-License-Identifier: MPL-2.0 */

#include "utils/precompiled.hpp"
#include "protocol/stream_fast_decoder.hpp"
#include "protocol/stream_fast_protocol.hpp"
#include "protocol/wire.hpp"
#include "utils/err.hpp"
#include "utils/likely.hpp"

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

zlink::stream_fast_decoder_t::stream_fast_decoder_t (size_t bufsize_,
                                                     int64_t maxmsgsize_) :
    decoder_base_t<stream_fast_decoder_t, shared_message_memory_allocator> (
      bufsize_),
    _max_msg_size_effective (compute_effective_max (maxmsgsize_)),
    _wire_body_size (0),
    _routing_id (0),
    _type (stream_fast_protocol::type_data)
{
    int rc = _in_progress.init ();
    errno_assert (rc == 0);

    next_step (_tmp_len, sizeof (_tmp_len), &stream_fast_decoder_t::length_ready);
}

zlink::stream_fast_decoder_t::~stream_fast_decoder_t ()
{
    const int rc = _in_progress.close ();
    errno_assert (rc == 0);
}

int zlink::stream_fast_decoder_t::length_ready (unsigned char const *read_from_)
{
    const uint32_t body_size = get_uint32 (_tmp_len);
    if (unlikely (body_size > _max_msg_size_effective)) {
        errno = EMSGSIZE;
        return -1;
    }
    if (unlikely (body_size < stream_fast_protocol::header_size)) {
        errno = EINVAL;
        return -1;
    }

    _wire_body_size = body_size;
    next_step (_stream_header, stream_fast_protocol::header_size,
               &stream_fast_decoder_t::stream_header_ready);
    LIBZLINK_UNUSED (read_from_);
    return 0;
}

int zlink::stream_fast_decoder_t::stream_header_ready (
  unsigned char const *read_from_)
{
    if (unlikely (_stream_header[0] != stream_fast_protocol::version
                  || _stream_header[2] != stream_fast_protocol::magic0
                  || _stream_header[3] != stream_fast_protocol::magic1)) {
        errno = EINVAL;
        return -1;
    }

    _type = _stream_header[1];
    _routing_id = get_uint32 (_stream_header + 4);
    const uint32_t payload_size =
      _wire_body_size - static_cast<uint32_t> (stream_fast_protocol::header_size);

    if (_type == stream_fast_protocol::type_data)
        return payload_size_ready (payload_size, read_from_);

    if (_type == stream_fast_protocol::type_connect
        || _type == stream_fast_protocol::type_disconnect) {
        if (unlikely (payload_size != 0)) {
            errno = EINVAL;
            return -1;
        }

        int rc = _in_progress.close ();
        errno_assert (rc == 0);
        rc = _in_progress.init_size (1);
        if (unlikely (rc != 0))
            return -1;

        unsigned char *dst =
          static_cast<unsigned char *> (_in_progress.data ());
        *dst = _type == stream_fast_protocol::type_connect ? 0x01 : 0x00;
        if (_routing_id != 0) {
            rc = _in_progress.set_routing_id (_routing_id);
            errno_assert (rc == 0);
        } else {
            rc = _in_progress.reset_routing_id ();
            errno_assert (rc == 0);
        }

        next_step (_tmp_len, sizeof (_tmp_len),
                   &stream_fast_decoder_t::length_ready);
        return 1;
    }

    errno = EINVAL;
    return -1;
}

int zlink::stream_fast_decoder_t::payload_size_ready (uint32_t size_,
                                                      unsigned char const *read_from_)
{
    int rc = _in_progress.close ();
    errno_assert (rc == 0);

    shared_message_memory_allocator &allocator = get_allocator ();
    if (unlikely (size_ > static_cast<size_t> (allocator.data ()
                                               + allocator.size ()
                                               - read_from_))) {
        rc = _in_progress.init_size (static_cast<size_t> (size_));
    } else {
        rc = _in_progress.init (const_cast<unsigned char *> (read_from_),
                                static_cast<size_t> (size_),
                                shared_message_memory_allocator::call_dec_ref,
                                allocator.buffer (),
                                allocator.provide_content ());
        if (_in_progress.is_zcmsg ()) {
            allocator.advance_content ();
            allocator.inc_ref ();
        }
    }

    if (unlikely (rc != 0)) {
        errno_assert (errno == ENOMEM);
        rc = _in_progress.init ();
        errno_assert (rc == 0);
        errno = ENOMEM;
        return -1;
    }

    rc = _in_progress.reset_routing_id ();
    errno_assert (rc == 0);
    if (_routing_id != 0) {
        rc = _in_progress.set_routing_id (_routing_id);
        errno_assert (rc == 0);
    }
    _in_progress.set_flags (0);

    next_step (_in_progress.data (), _in_progress.size (),
               &stream_fast_decoder_t::payload_ready);
    return 0;
}

int zlink::stream_fast_decoder_t::payload_ready (unsigned char const *)
{
    next_step (_tmp_len, sizeof (_tmp_len), &stream_fast_decoder_t::length_ready);
    return 1;
}
