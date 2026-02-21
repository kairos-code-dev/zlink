/* SPDX-License-Identifier: MPL-2.0 */

#ifndef __ZLINK_RAW_ENCODER_HPP_INCLUDED__
#define __ZLINK_RAW_ENCODER_HPP_INCLUDED__

#include "protocol/encoder.hpp"

namespace zlink
{
//  Encoder for raw STREAM payload (no extra framing).
class raw_encoder_t ZLINK_FINAL : public encoder_base_t<raw_encoder_t>
{
  public:
    raw_encoder_t (size_t bufsize_, bool len32be_);
    ~raw_encoder_t ();

  private:
    void raw_message_ready ();
    void raw_message_body ();

    bool _len32be;
    unsigned char _header[4];

    ZLINK_NON_COPYABLE_NOR_MOVABLE (raw_encoder_t)
};
}

#endif
