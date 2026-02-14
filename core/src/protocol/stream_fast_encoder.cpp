/* SPDX-License-Identifier: MPL-2.0 */

#include "utils/precompiled.hpp"
#include "protocol/stream_fast_encoder.hpp"
#include "core/msg.hpp"
#include "protocol/wire.hpp"

zlink::stream_fast_encoder_t::stream_fast_encoder_t (size_t bufsize_) :
    encoder_base_t<stream_fast_encoder_t> (bufsize_)
{
    next_step (NULL, 0, &stream_fast_encoder_t::header_ready, true);
}

zlink::stream_fast_encoder_t::~stream_fast_encoder_t ()
{
}

void zlink::stream_fast_encoder_t::header_ready ()
{
    const msg_t *msg = in_progress ();
    put_uint32 (_tmp_buf, static_cast<uint32_t> (msg->size ()));

    next_step (_tmp_buf, sizeof (_tmp_buf), &stream_fast_encoder_t::body_ready,
               false);
}

void zlink::stream_fast_encoder_t::body_ready ()
{
    next_step (in_progress ()->data (), in_progress ()->size (),
               &stream_fast_encoder_t::header_ready, true);
}
