/* SPDX-License-Identifier: MPL-2.0 */

#ifndef __ZLINK_STREAM_FAST_ENCODER_HPP_INCLUDED__
#define __ZLINK_STREAM_FAST_ENCODER_HPP_INCLUDED__

#include "protocol/encoder.hpp"

namespace zlink
{
class stream_fast_encoder_t ZLINK_FINAL
    : public encoder_base_t<stream_fast_encoder_t>
{
  public:
    explicit stream_fast_encoder_t (size_t bufsize_);
    ~stream_fast_encoder_t ();

  private:
    void header_ready ();
    void body_ready ();

    unsigned char _tmp_buf[12];

    ZLINK_NON_COPYABLE_NOR_MOVABLE (stream_fast_encoder_t)
};
}

#endif
