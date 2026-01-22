/* SPDX-License-Identifier: MPL-2.0 */

#ifndef __ZMQ_ZMP_ENCODER_HPP_INCLUDED__
#define __ZMQ_ZMP_ENCODER_HPP_INCLUDED__

#include "protocol/encoder.hpp"
#include "protocol/zmp_protocol.hpp"

namespace zmq
{
//  Encoder for ZMP framing protocol (v1).
class zmp_encoder_t ZMQ_FINAL : public encoder_base_t<zmp_encoder_t>
{
  public:
    explicit zmp_encoder_t (size_t bufsize_);
    ~zmp_encoder_t ();

  private:
    void header_ready ();
    void body_ready ();

    unsigned char _tmp_buf[zmp_header_size];

    ZMQ_NON_COPYABLE_NOR_MOVABLE (zmp_encoder_t)
};
}

#endif
