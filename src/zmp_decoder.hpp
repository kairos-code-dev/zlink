/* SPDX-License-Identifier: MPL-2.0 */

#ifndef __ZMQ_ZMP_DECODER_HPP_INCLUDED__
#define __ZMQ_ZMP_DECODER_HPP_INCLUDED__

#include "decoder.hpp"
#include "decoder_allocators.hpp"
#include "zmp_protocol.hpp"

namespace zmq
{
//  Decoder for ZMP framing protocol (v1).
class zmp_decoder_t ZMQ_FINAL
    : public decoder_base_t<zmp_decoder_t, shared_message_memory_allocator>
{
  public:
    zmp_decoder_t (size_t bufsize_, int64_t maxmsgsize_);
    ~zmp_decoder_t ();

    msg_t *msg () { return &_in_progress; }
    uint8_t error_code () const { return _error_code; }

  private:
    int header_ready (unsigned char const *read_from_);
    int body_ready (unsigned char const *read_from_);

    int size_ready (uint32_t size_, unsigned char const *read_from_);

    unsigned char _tmpbuf[zmp_header_size];
    unsigned char _msg_flags;
    uint8_t _error_code;
    msg_t _in_progress;
    const uint32_t _max_msg_size_effective;

    ZMQ_NON_COPYABLE_NOR_MOVABLE (zmp_decoder_t)
};
}

#endif
