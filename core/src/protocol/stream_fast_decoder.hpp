/* SPDX-License-Identifier: MPL-2.0 */

#ifndef __ZLINK_STREAM_FAST_DECODER_HPP_INCLUDED__
#define __ZLINK_STREAM_FAST_DECODER_HPP_INCLUDED__

#include "protocol/decoder.hpp"
#include "protocol/decoder_allocators.hpp"
#include "core/msg.hpp"
#include "utils/stdint.hpp"

namespace zlink
{
class stream_fast_decoder_t ZLINK_FINAL
    : public decoder_base_t<stream_fast_decoder_t, shared_message_memory_allocator>
{
  public:
    stream_fast_decoder_t (size_t bufsize_, int64_t maxmsgsize_);
    ~stream_fast_decoder_t ();

    msg_t *msg () { return &_in_progress; }

  private:
    int header_ready (unsigned char const *read_from_);
    int payload_ready (unsigned char const *read_from_);
    int payload_size_ready (uint32_t size_, unsigned char const *read_from_);

    unsigned char _tmp_len[4];
    msg_t _in_progress;
    const uint32_t _max_msg_size_effective;

    ZLINK_NON_COPYABLE_NOR_MOVABLE (stream_fast_decoder_t)
};
}

#endif
