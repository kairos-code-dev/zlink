/* SPDX-License-Identifier: MPL-2.0 */

#include "utils/precompiled.hpp"
#include "protocol/raw_encoder.hpp"
#include "core/msg.hpp"
#include "protocol/wire.hpp"

zlink::raw_encoder_t::raw_encoder_t (size_t bufsize_) :
    encoder_base_t<raw_encoder_t> (bufsize_)
{
    next_step (NULL, 0, &raw_encoder_t::header_ready, true);
}

zlink::raw_encoder_t::~raw_encoder_t ()
{
}

void zlink::raw_encoder_t::header_ready ()
{
    const msg_t *msg = in_progress ();
    const size_t size = msg->size ();

    put_uint32 (_tmp_buf, static_cast<uint32_t> (size));
    next_step (_tmp_buf, sizeof (_tmp_buf), &raw_encoder_t::body_ready, false);
}

void zlink::raw_encoder_t::body_ready ()
{
    next_step (in_progress ()->data (), in_progress ()->size (),
               &raw_encoder_t::header_ready, true);
}
