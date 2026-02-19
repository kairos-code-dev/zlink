/* SPDX-License-Identifier: MPL-2.0 */

#include "utils/precompiled.hpp"
#include "protocol/raw_decoder.hpp"
#include "utils/err.hpp"

zlink::raw_decoder_t::raw_decoder_t (size_t bufsize_, int64_t maxmsgsize_) :
    _allocator (bufsize_, 1),
    _max_msg_size (maxmsgsize_)
{
    const int rc = _in_progress.init ();
    errno_assert (rc == 0);
}

zlink::raw_decoder_t::~raw_decoder_t ()
{
    const int rc = _in_progress.close ();
    errno_assert (rc == 0);
}

void zlink::raw_decoder_t::get_buffer (unsigned char **data_, size_t *size_)
{
    *data_ = _allocator.allocate ();
    *size_ = _allocator.size ();
}

int zlink::raw_decoder_t::decode (const unsigned char *data_,
                                  size_t size_,
                                  size_t &bytes_used_)
{
    if (_max_msg_size >= 0
        && size_ > static_cast<size_t> (_max_msg_size)) {
        errno = EMSGSIZE;
        return -1;
    }

    const int rc =
      _in_progress.init (const_cast<unsigned char *> (data_), size_,
                         shared_message_memory_allocator::call_dec_ref,
                         _allocator.buffer (), _allocator.provide_content ());

    // If the message became zero-copy backed by allocator memory,
    // hand off the current content slot and release allocator ownership.
    if (_in_progress.is_zcmsg ()) {
        _allocator.advance_content ();
        _allocator.release ();
    }

    errno_assert (rc != -1);
    bytes_used_ = size_;
    return 1;
}
