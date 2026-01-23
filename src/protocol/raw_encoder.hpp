/* SPDX-License-Identifier: MPL-2.0 */

#ifndef __ZMQ_RAW_ENCODER_HPP_INCLUDED__
#define __ZMQ_RAW_ENCODER_HPP_INCLUDED__

#include "protocol/encoder.hpp"

namespace zmq
{
//  Encoder for raw STREAM framing (4-byte length prefix).
class raw_encoder_t ZMQ_FINAL : public encoder_base_t<raw_encoder_t>
{
  public:
    explicit raw_encoder_t (size_t bufsize_);
    ~raw_encoder_t ();

  private:
    void header_ready ();
    void body_ready ();

    unsigned char _tmp_buf[4];

    ZMQ_NON_COPYABLE_NOR_MOVABLE (raw_encoder_t)
};
}

#endif
